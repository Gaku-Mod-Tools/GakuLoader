#include "GameHook.hpp"
#include "Host.hpp"
#include "ActorEntry.hpp"
#include "ExitDiagnosis.hpp"
#include "DiagnosticBoundary.hpp"
#include "DesktopUi.hpp"
#include "Trace.hpp"
#include <minhook.h>
#include <windows.h>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <cwchar>

namespace GakuLoader {

namespace {

HMODULE g_gameAssembly = nullptr;
std::atomic<bool> g_stopThread{false};
std::atomic<bool> g_installHooks{true};
// True once the runtime-ready worker released its IL2CPP attachment (it does so
// right after hook setup so both exit paths are safe).
std::atomic<bool> g_runtimeThreadDetached{false};
HANDLE g_hookThreadHandle = nullptr;
using LoadLibraryWFn = HMODULE (WINAPI*)(LPCWSTR);
LoadLibraryWFn g_originalLoadLibraryW = nullptr;
using InLoaderCalloutFn = BOOLEAN (NTAPI*)();
InLoaderCalloutFn g_inLoaderCallout = nullptr;
HANDLE g_startupBoundarySeen = nullptr;
HANDLE g_startupWorkerFinished = nullptr;
std::atomic<bool> g_startupBoundaryClaimed{false};
std::atomic<bool> g_startupWorkerPrepared{false};
DWORD g_startupBoundaryWaitMs = 30000;

// IL2CPP APIs
typedef void* (*Il2CppDomainGetFn)();
typedef void* (*Il2CppThreadAttachFn)(void* domain);   // GC thread attach
typedef void  (*Il2CppThreadDetachFn)(void* thread);   // GC thread detach
typedef void* (*Il2CppDomainAssemblyOpenFn)(void* domain, const char* name);
typedef void* (*Il2CppAssemblyGetImageFn)(void* assembly);
typedef void* (*Il2CppClassFromNameFn)(void* image, const char* namespaze, const char* name);
typedef void* (*Il2CppClassGetParentFn)(void* klass);
typedef void* (*Il2CppClassGetMethodsFn)(void* klass, void** iter);
typedef const char* (*Il2CppMethodGetNameFn)(void* method);
typedef int (*Il2CppMethodGetParamCountFn)(void* method);

struct Il2CppMethodInfo {
    void* methodPointer;
};
typedef Il2CppMethodInfo* (*Il2CppClassGetMethodFromNameFn)(void* klass, const char* name, int argsCount);

Il2CppDomainGetFn il2cpp_domain_get = nullptr;
Il2CppThreadAttachFn il2cpp_thread_attach = nullptr;
Il2CppThreadDetachFn il2cpp_thread_detach = nullptr;
Il2CppDomainAssemblyOpenFn il2cpp_domain_assembly_open = nullptr;
Il2CppAssemblyGetImageFn il2cpp_assembly_get_image = nullptr;
Il2CppClassFromNameFn il2cpp_class_from_name = nullptr;
Il2CppClassGetParentFn il2cpp_class_get_parent = nullptr;
Il2CppClassGetMethodsFn il2cpp_class_get_methods = nullptr;
Il2CppMethodGetNameFn il2cpp_method_get_name = nullptr;
Il2CppMethodGetParamCountFn il2cpp_method_get_param_count = nullptr;
Il2CppClassGetMethodFromNameFn il2cpp_class_get_method_from_name = nullptr;

void* FindClass(const char* assemblyName, const char* namespaze, const char* className) {
    if (!il2cpp_domain_get || !il2cpp_domain_assembly_open || !il2cpp_assembly_get_image || !il2cpp_class_from_name) {
        return nullptr;
    }
    void* domain = il2cpp_domain_get();
    if (!domain) return nullptr;
    void* assembly = il2cpp_domain_assembly_open(domain, assemblyName);
    if (!assembly) return nullptr;
    void* image = il2cpp_assembly_get_image(assembly);
    if (!image) return nullptr;
    return il2cpp_class_from_name(image, namespaze, className);
}

void* FindMethodPointer(void* klass, const char* methodName, int argsCount = -1) {
    if (!klass) return nullptr;
    if (argsCount >= 0 && il2cpp_class_get_method_from_name) {
        Il2CppMethodInfo* m = il2cpp_class_get_method_from_name(klass, methodName, argsCount);
        if (m && m->methodPointer) return m->methodPointer;
    }
    if (il2cpp_class_get_methods && il2cpp_method_get_name) {
        void* iter = nullptr;
        while (void* method = il2cpp_class_get_methods(klass, &iter)) {
            const char* name = il2cpp_method_get_name(method);
            if (name && strcmp(name, methodName) == 0) {
                if (argsCount < 0 || (il2cpp_method_get_param_count && il2cpp_method_get_param_count(method) == argsCount)) {
                    Il2CppMethodInfo* m = reinterpret_cast<Il2CppMethodInfo*>(method);
                    return m->methodPointer;
                }
            }
        }
    }
    return nullptr;
}

// --- Main-thread pump -------------------------------------------------------
// A getter hook does not establish a thread's identity or guarantee a call in
// actor-free scenes. Wake the Unity window instead, and read the actual frame
// number only on its owning thread. Actor callbacks use the same frame gate.
using FrameCountFn = int32_t (*)(void* methodInfo);
using DeltaTimeFn = float (*)(void* methodInfo);
std::atomic<FrameCountFn> g_readFrameCount{nullptr};
std::atomic<DeltaTimeFn> g_readDeltaTime{nullptr};
std::atomic<HWND> g_pumpWindow{nullptr};
std::atomic<bool> g_wakePending{false};
ULONGLONG g_lastWakeRetry = 0; // worker thread only
bool g_inFramePump = false;  // confirmed Unity window thread only
bool g_hasFrame = false;
int32_t g_lastFrame = 0;
float g_lastDelta = 0.0333f;

UINT FramePumpMessage() {
    static const UINT message = RegisterWindowMessageW(L"GakuLoader.FramePump.v1");
    return message;
}

HWND FindPumpWindow() {
    HWND window = nullptr;
    while ((window = FindWindowExW(nullptr, window, L"UnityWndClass", nullptr)) != nullptr) {
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);
        if (process == GetCurrentProcessId()) return window;
    }
    return nullptr;
}

bool ConfirmPumpThread(HWND window) {
    DWORD process = 0;
    const DWORD thread = window ? GetWindowThreadProcessId(window, &process) : 0;
    return process == GetCurrentProcessId() && thread == GetCurrentThreadId() &&
        BindMainThread(thread);
}

void PumpFrame(HWND window) {
    // Reject foreign callers before reading any Unity API or pump state.
    if (!ConfirmPumpThread(window) || g_inFramePump) return;
    g_inFramePump = true;
    struct Reset { ~Reset() { g_inFramePump = false; } } reset;
    // A shutdown request must still drain when Unity's frame number has stopped.
    if (IsShuttingDown()) { TickFrame(g_lastDelta); return; }
    const auto readFrame = g_readFrameCount.load(std::memory_order_acquire);
    if (!readFrame) return;
    const int32_t frame = readFrame(nullptr);
    if (g_hasFrame && frame == g_lastFrame) return;
    g_hasFrame = true;
    g_lastFrame = frame; // publish before callbacks can reenter through messages
    const auto readDelta = g_readDeltaTime.load(std::memory_order_acquire);
    float delta = readDelta ? readDelta(nullptr) : 0.0333f;
    if (!std::isfinite(delta) || delta < 0.0f) delta = 0.0333f;
    g_lastDelta = delta;
    TickFrame(delta);
}

void WakeFramePump(HWND window) {
    if (!window || !FramePumpMessage()) return;
    const ULONGLONG now = GetTickCount64();
    if (!g_wakePending.exchange(true, std::memory_order_acq_rel)) {
        g_lastWakeRetry = now;
        if (!PostMessageW(window, FramePumpMessage(), 0, 0))
            g_wakePending.store(false, std::memory_order_release);
    } else if (now - g_lastWakeRetry >= 100) {
        g_lastWakeRetry = now;
        // The first post can predate QuitGuard's WndProc installation. Retry
        // without filling a hung window's queue with unacknowledged posts.
        DWORD_PTR ignored = 0;
        SendMessageTimeoutW(window, FramePumpMessage(), 0, 0,
            SMTO_ABORTIFHUNG | SMTO_BLOCK, 20, &ignored);
    }
}

HMODULE WINAPI Detour_LoadLibraryW(LPCWSTR path) {
    const wchar_t* name = path ? std::wcsrchr(path, L'\\') : nullptr;
    name = name ? name + 1 : path;
    if (name && _wcsicmp(name, L"VuplexWebViewWindows.dll") == 0 &&
        g_startupBoundarySeen && g_startupWorkerFinished) {
        // This is the same pre-load boundary used by standalone Localify. Only
        // the game's independently identified Unity window thread may hold it.
        const HWND window = FindPumpWindow();
        if (ConfirmPumpThread(window) && (!g_inLoaderCallout || g_inLoaderCallout())) {
            LogWarn("Pre-Vuplex synchronization skipped inside an unknown/active loader callout; using loaded-module fallback");
        } else if (ConfirmPumpThread(window) && !g_startupBoundaryClaimed.exchange(true)) {
            g_pumpWindow.store(window, std::memory_order_release);
            const auto started = Trace::UptimeMs();
            LogInfo("[StartupTiming] pre-Vuplex boundary entered uptime_ms=%llu thread=%lu", started, GetCurrentThreadId());
            SetEvent(g_startupBoundarySeen);
            // Do not dispatch messages while the worker is resolving metadata
            // and patching code. Its callback phase remains on that worker.
            const DWORD waited = WaitForSingleObject(g_startupWorkerFinished, g_startupBoundaryWaitMs);
            if (waited == WAIT_OBJECT_0 && g_startupWorkerPrepared.load(std::memory_order_acquire)) {
                // Drain the existing main-thread queue at the original safe
                // standalone point, before first UI construction can continue.
                DispatchMainThreadTasks();
                LogInfo("[StartupTiming] pre-Vuplex boundary released after main-thread dispatch uptime_ms=%llu thread=%lu elapsed_ms=%llu",
                    Trace::UptimeMs(), GetCurrentThreadId(), Trace::UptimeMs() - started);
            } else {
                LogWarn("[StartupTiming] pre-Vuplex synchronization unavailable wait=%lu prepared=%u uptime_ms=%llu thread=%lu; first-UI ordering is not guaranteed",
                    waited, g_startupWorkerPrepared.load() ? 1u : 0u, Trace::UptimeMs(), GetCurrentThreadId());
            }
        }
    }
    return g_originalLoadLibraryW(path);
}

// Per-actor driver for the same pump.
using LateUpdateFn = void (*)(void*, void*);
LateUpdateFn s_origLateUpdate = nullptr;
void* s_lateUpdateTarget = nullptr;

void Detour_LateUpdate(void* thisPtr, void* methodInfo) {
    // This detour is the single owner of the shared game entry. Plugins that
    // need the same point register before/after participants instead of taking
    // a colliding lease; the original runs exactly once between the phases.
    ActorEntry::Invoke(thisPtr, s_origLateUpdate, methodInfo);

    // QuitGuard uses its own WndProc and worker thread; shutdown must not depend
    // on plugin frame dispatch being installed or active.
    static bool s_firstLogged = false;
    if (!s_firstLogged) {
        s_firstLogged = true;
        GakuLoader::LogInfo("First LateUpdate intercepted on actor %p; actor tick active.", thisPtr);
    }

    // A frame without a prior Time tick still needs queue/hotkey progress.
    const HWND window = g_pumpWindow.load(std::memory_order_acquire);
    PumpFrame(window);
    if (ConfirmPumpThread(window)) GakuLoader::TickActor(thisPtr, g_lastDelta);
}

DWORD WINAPI WorkerLoop(LPVOID) {
    struct FinishStartup {
        ~FinishStartup() { if (g_startupWorkerFinished) SetEvent(g_startupWorkerFinished); }
    } finishStartup;
    constexpr DWORD runtimePollMs = 5;
    GakuLoader::LogInfo("GameHook thread started, waiting for GameAssembly.dll...");

    while (!g_stopThread.load()) {
        g_gameAssembly = GetModuleHandleW(L"GameAssembly.dll");
        if (g_gameAssembly) {
            if (GetProcAddress(g_gameAssembly, "il2cpp_domain_get")) break;
        }
        Sleep(runtimePollMs);
    }
    if (g_stopThread.load() || !g_gameAssembly) return 0;

    GakuLoader::LogInfo("[StartupTiming] GameAssembly exports observed uptime_ms=%llu thread=%lu", Trace::UptimeMs(), GetCurrentThreadId());
    // Do not call IL2CPP, including its domain getter, before the known safe
    // standalone boundary. A mapped export is not an init-complete barrier.
    bool preLoadBoundary = false;
    while (!g_stopThread.load()) {
        if (g_startupBoundarySeen && WaitForSingleObject(g_startupBoundarySeen, 0) == WAIT_OBJECT_0) {
            preLoadBoundary = true;
            break;
        }
        if (GetModuleHandleW(L"VuplexWebViewWindows.dll")) break;
        Sleep(runtimePollMs);
    }
    if (g_stopThread.load()) return 0;

    GakuLoader::LogInfo("[StartupTiming] runtime bootstrap anchor=%s uptime_ms=%llu thread=%lu",
        preLoadBoundary ? "pre-Vuplex synchronous boundary" : "Vuplex-loaded fallback", Trace::UptimeMs(), GetCurrentThreadId());
    il2cpp_domain_get             = reinterpret_cast<Il2CppDomainGetFn>(GetProcAddress(g_gameAssembly, "il2cpp_domain_get"));
    il2cpp_thread_attach          = reinterpret_cast<Il2CppThreadAttachFn>(GetProcAddress(g_gameAssembly, "il2cpp_thread_attach"));
    il2cpp_thread_detach          = reinterpret_cast<Il2CppThreadDetachFn>(GetProcAddress(g_gameAssembly, "il2cpp_thread_detach"));
    il2cpp_domain_assembly_open   = reinterpret_cast<Il2CppDomainAssemblyOpenFn>(GetProcAddress(g_gameAssembly, "il2cpp_domain_assembly_open"));
    il2cpp_assembly_get_image     = reinterpret_cast<Il2CppAssemblyGetImageFn>(GetProcAddress(g_gameAssembly, "il2cpp_assembly_get_image"));
    il2cpp_class_from_name        = reinterpret_cast<Il2CppClassFromNameFn>(GetProcAddress(g_gameAssembly, "il2cpp_class_from_name"));
    il2cpp_class_get_parent       = reinterpret_cast<Il2CppClassGetParentFn>(GetProcAddress(g_gameAssembly, "il2cpp_class_get_parent"));
    il2cpp_class_get_methods      = reinterpret_cast<Il2CppClassGetMethodsFn>(GetProcAddress(g_gameAssembly, "il2cpp_class_get_methods"));
    il2cpp_method_get_name        = reinterpret_cast<Il2CppMethodGetNameFn>(GetProcAddress(g_gameAssembly, "il2cpp_method_get_name"));
    il2cpp_method_get_param_count = reinterpret_cast<Il2CppMethodGetParamCountFn>(GetProcAddress(g_gameAssembly, "il2cpp_method_get_param_count"));
    il2cpp_class_get_method_from_name = reinterpret_cast<Il2CppClassGetMethodFromNameFn>(GetProcAddress(g_gameAssembly, "il2cpp_class_get_method_from_name"));

    void* domain = nullptr;
    const ULONGLONG domainWaitStarted = GetTickCount64();
    while (!g_stopThread.load() && GetTickCount64() - domainWaitStarted < 60000) {
        domain = il2cpp_domain_get ? il2cpp_domain_get() : nullptr;
        if (domain) break;
        Sleep(runtimePollMs);
    }
    if (g_stopThread.load()) return 0;
    if (!domain) {
        GakuLoader::LogError("il2cpp_domain_get returned null after the Vuplex milestone; runtime callbacks stay disabled");
        return 0;
    }
    LogInfo("[StartupTiming] IL2CPP domain observed at safe anchor uptime_ms=%llu thread=%lu", Trace::UptimeMs(), GetCurrentThreadId());

    if (!il2cpp_thread_attach || !il2cpp_thread_detach) {
        GakuLoader::LogError("IL2CPP thread attach/detach exports unavailable; runtime callbacks stay disabled");
        return 0;
    }
    void* gcThread = il2cpp_thread_attach(domain);
    if (!gcThread) {
        GakuLoader::LogError("IL2CPP worker thread attachment failed; runtime callbacks stay disabled");
        return 0;
    }
    GakuLoader::LogInfo("[StartupTiming] runtime prerequisite ready uptime_ms=%llu thread=%lu domain=%p gc_thread=%p",
        Trace::UptimeMs(), GetCurrentThreadId(), domain, gcThread);

    // Distinct phase, distinct thread: plugins may resolve exports/metadata and
    // install hooks here, but must defer managed objects to the main thread.
    NotifyRuntimeReady();

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        GakuLoader::LogError("MH_Initialize failed: %d", status);
        return 0;
    }

    if (GakuLoader::HooksDisabledForDiagnosis()) {
        GakuLoader::LogExitDiagnosisMode();
        GakuLoader::LogWarn("[GameHook] skipping all MinHook installation; "
                            "plugins receive no frame or actor callbacks");
        while (!g_stopThread.load()) Sleep(500);
        if (gcThread && il2cpp_thread_detach) il2cpp_thread_detach(gcThread);
        return 0;
    }

    if (!g_installHooks.load(std::memory_order_acquire)) {
        GakuLoader::LogInfo("[GameHook] detour installation skipped; IL2CPP resolver remains available.");
        while (!g_stopThread.load()) Sleep(500);
        if (gcThread && il2cpp_thread_detach) il2cpp_thread_detach(gcThread);
        return 0;
    }

    // Diagnostics are a policy of their own: a failure here is reported and
    // never blocks plugins that were already activated.
    if (!InstallDiagnosticBoundary()) {
        GakuLoader::LogWarn("[GameHook] diagnostic boundary unavailable; plugins keep running unfiltered");
    }

    // 1. Resolve readers; never detour passive Time getters or call them here.
    // Window-thread wake messages provide progress even without any actors.
    void* timeClass = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Time");
    if (!timeClass) timeClass = FindClass("UnityEngine.dll", "UnityEngine", "Time");
    if (timeClass) {
        g_readFrameCount.store(reinterpret_cast<FrameCountFn>(FindMethodPointer(timeClass, "get_frameCount", 0)),
            std::memory_order_release);
        g_readDeltaTime.store(reinterpret_cast<DeltaTimeFn>(FindMethodPointer(timeClass, "get_deltaTime", 0)),
            std::memory_order_release);
        if (g_readFrameCount.load(std::memory_order_acquire))
            GakuLoader::LogInfo("Frame readers ready; Unity window messages will drive actor-independent work");
        else
            GakuLoader::LogError("No UnityEngine.Time.get_frameCount reader; frame dispatch stays disabled");
    } else {
        GakuLoader::LogError("UnityEngine.Time not found; frame dispatch stays disabled");
    }

    // 2. Actor LateUpdate for per-actor pose work.
    const char* campusAsmNames[] = {
        "campus-submodule.Runtime.dll",
        "campus-submodule.Runtime",
        "campus-submodule",
        nullptr
    };
    void* actorKlass = nullptr;
    for (int i = 0; campusAsmNames[i] && !actorKlass; ++i) {
        actorKlass = FindClass(campusAsmNames[i], "Campus.Common", "CampusActorController");
        if (!actorKlass)
            actorKlass = FindClass(campusAsmNames[i], "Campus.Common", "CampusVirtualActorController");
        if (actorKlass)
            GakuLoader::LogInfo("Found actor class in assembly: %s", campusAsmNames[i]);
    }

    if (actorKlass) {
        void* lateUpdatePtr = FindMethodPointer(actorKlass, "LateUpdate");
        if (!lateUpdatePtr) lateUpdatePtr = FindMethodPointer(actorKlass, "OnLateUpdate");
        if (lateUpdatePtr) {
            const auto s = AcquireHostHook(lateUpdatePtr, reinterpret_cast<void*>(&Detour_LateUpdate), reinterpret_cast<void**>(&s_origLateUpdate));
            if (s == GAKU_OK) {
                s_lateUpdateTarget = lateUpdatePtr;
                GakuLoader::LogInfo("Successfully hooked CampusActorController::LateUpdate (%p)", lateUpdatePtr);
            } else {
                GakuLoader::LogWarn("Owned hook acquisition for LateUpdate failed: %d", s);
            }
        } else {
            GakuLoader::LogWarn("Could not find LateUpdate method on actor class");
        }
    } else {
        GakuLoader::LogWarn("Could not find CampusActorController class in any assembly variant");
    }

    // These methods return a 16-byte UniTask through a hidden return buffer.
    // The former void* detours did not implement that ABI and cannot safely
    // suppress the call. Leave voice methods untouched until that ABI is supported.
    GakuLoader::LogInfo("Photography voice detours disabled (UniTask return ABI).");

    GakuLoader::LogInfo("GameHook setup completed successfully.");

    // Everything this thread resolved is published, and the keep-alive loop
    // below only posts window messages. Release the IL2CPP attachment now
    // instead of at process exit: a thread that stays attached blocks
    // il2cpp_shutdown, and the player can start its own exit (in-game menu)
    // without ever going through the window-close path that calls
    // ShutdownHooks(). Detaching here makes both exit paths behave the same.
    if (gcThread && il2cpp_thread_detach) {
        il2cpp_thread_detach(gcThread);
        gcThread = nullptr;
        g_runtimeThreadDetached.store(true, std::memory_order_release);
        GakuLoader::LogInfo("[GameHook] runtime-ready thread detached from IL2CPP after hook setup");
    }
    g_startupWorkerPrepared.store(true, std::memory_order_release);
    if (g_startupWorkerFinished) SetEvent(g_startupWorkerFinished);

    while (!g_stopThread.load()) {
        HWND window = g_pumpWindow.load(std::memory_order_acquire);
        if (!window || !IsWindow(window)) {
            window = FindPumpWindow();
            g_pumpWindow.store(window, std::memory_order_release);
            g_wakePending.store(false, std::memory_order_release);
        }
        WakeFramePump(window);
        Sleep(10);
    }

    if (gcThread && il2cpp_thread_detach) {
        il2cpp_thread_detach(gcThread);
    }
    return 0;
}

} // namespace

bool HandleFramePumpMessage(HWND window, UINT message, WPARAM, LPARAM) {
    const UINT expected = FramePumpMessage();
    if (!expected || message != expected) return false;
    g_wakePending.store(false, std::memory_order_release);
    if (window == g_pumpWindow.load(std::memory_order_acquire)) PumpFrame(window);
    return true;
}

// 供 UnityQuit.cpp 使用的 IL2CPP 解析入口。
//
// 内部的 FindClass / FindMethodPointer 位于匿名命名空间，无法跨翻译单元链接，
// 因此在这里转发一层。IL2CPP 导出在 GameHook 的工作线程里已全部解析完毕
// （见 WorkerLoop），所以这两个函数是纯查表，可在任意线程安全调用；
// 真正的调用方 UnityQuit 会把它们限制在窗口定时器回调（Unity 主线程）里执行。
void* ResolveIl2cppClass(const char* assembly, const char* namespaze, const char* name) {
    return FindClass(assembly, namespaze, name);
}

void* ResolveIl2cppMethod(void* klass, const char* methodName, int argsCount) {
    return FindMethodPointer(klass, methodName, argsCount);
}

void StartHookThread(bool installHooks) {
    g_stopThread.store(false);
    g_installHooks.store(installHooks, std::memory_order_release);
    if (installHooks && !HooksDisabledForDiagnosis()) {
        // Pure native interception, installed outside DllMain and before Unity
        // reaches the same LoadLibraryW call used by standalone Localify.
        g_startupBoundarySeen = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        g_startupWorkerFinished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        g_inLoaderCallout = reinterpret_cast<InLoaderCalloutFn>(
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlIsThreadWithinLoaderCallout"));
        auto* target = reinterpret_cast<void*>(&LoadLibraryW);
        if (g_startupBoundarySeen && g_startupWorkerFinished &&
            AcquireHostHook(target, reinterpret_cast<void*>(&Detour_LoadLibraryW),
                reinterpret_cast<void**>(&g_originalLoadLibraryW)) == GAKU_OK) {
            LogInfo("[StartupTiming] pre-Vuplex native boundary armed uptime_ms=%llu thread=%lu", Trace::UptimeMs(), GetCurrentThreadId());
        } else {
            LogWarn("Pre-Vuplex native boundary unavailable; retaining Vuplex-loaded runtime fallback");
        }
    }
    g_hookThreadHandle = CreateThread(nullptr, 0, WorkerLoop, nullptr, 0, nullptr);
    if (!g_hookThreadHandle && g_startupWorkerFinished) SetEvent(g_startupWorkerFinished);
}

void DisableAllHooksForExit() {
    // Kept as a source-compatible QuitGuard entry; scope is now strictly owned.
    // Retain trampolines for in-flight calls; never disable another host's hooks.
    if (!CanShutdownOnCurrentThread()) {
        Shutdown(); // request only; do not remove the actor pump before teardown
        LogWarn("Exit teardown deferred to main thread; runtime hook leases remain active");
        return;
    }
    DesktopUi::Shutdown(); // drain render callbacks before taking lifecycle mutex
    // This thread attached itself to the IL2CPP domain so plugins could resolve
    // metadata at runtime-ready. il2cpp_shutdown waits for every attached thread
    // to detach, so leaving it alive keeps the player in shutdown forever:
    // window intact, no CPU, no further log lines. Stop it before Application.Quit.
    ShutdownHooks();
    BeginExitTeardown();
    Shutdown();
    DisableOwnedHooksForExit();
}

void ShutdownHooks() {
    g_stopThread.store(true);
    if (g_hookThreadHandle) {
        const auto started = GetTickCount64();
        const auto wait = WaitForSingleObject(g_hookThreadHandle, 2000);
        CloseHandle(g_hookThreadHandle);
        g_hookThreadHandle = nullptr;
        const auto elapsed = static_cast<unsigned long long>(GetTickCount64() - started);
        if (wait == WAIT_OBJECT_0) {
            LogInfo("[GameHook] hook thread stopped in %llu ms (%s)", elapsed,
                g_runtimeThreadDetached.load(std::memory_order_acquire)
                    ? "IL2CPP attachment already released after setup"
                    : "detached on stop");
        } else {
            LogError("[GameHook] hook thread still attached after %llu ms; "
                     "il2cpp_shutdown will wait for it", elapsed);
        }
    }
    if (s_lateUpdateTarget) ReleaseHostHook(s_lateUpdateTarget);
    g_readFrameCount.store(nullptr, std::memory_order_release);
    g_readDeltaTime.store(nullptr, std::memory_order_release);
}

} // namespace GakuLoader
