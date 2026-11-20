// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

#include "GameHook.hpp"
#include "Host.hpp"
#include "HostConfig.hpp"
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
#include <limits>
#include <string>

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
using LoadLibraryExWFn = HMODULE (WINAPI*)(LPCWSTR, HANDLE, DWORD);
LoadLibraryWFn g_originalLoadLibraryW = nullptr;
LoadLibraryExWFn g_originalLoadLibraryExW = nullptr;
using InLoaderCalloutFn = BOOLEAN (NTAPI*)();
InLoaderCalloutFn g_inLoaderCallout = nullptr;
HANDLE g_startupBoundarySeen = nullptr;
HANDLE g_startupWorkerFinished = nullptr;
std::atomic<bool> g_startupBoundaryArmed{false};
std::atomic<bool> g_startupBoundaryClaimed{false};
std::atomic<bool> g_startupWorkerPrepared{false};
std::atomic<bool> g_startupNoWindowWarned{false};
std::atomic<bool> g_startupCalloutWarned{false};
// Hard-block by default until all plugins finish activate + onRuntimeReady
// (or shutdown is requested). Configurable via [startup] boundary_wait_ms.
DWORD g_startupBoundaryWaitMs = INFINITE;

DWORD ReadStartupBoundaryWaitMs() {
    const auto path = ConfigPath();
    if (path.empty()) return INFINITE;
    const std::wstring raw = ReadConfigValue(path, L"startup", L"boundary_wait_ms");
    const auto first = raw.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return INFINITE;
    const auto last = raw.find_last_not_of(L" \t\r\n");
    const std::wstring trimmed = raw.substr(first, last - first + 1);
    if (_wcsicmp(trimmed.c_str(), L"infinite") == 0) return INFINITE;
    wchar_t* end = nullptr;
    const unsigned long parsed = wcstoul(trimmed.c_str(), &end, 10);
    if (end == trimmed.c_str() || *end != L'\0') return INFINITE;
    return parsed >= (std::numeric_limits<DWORD>::max)()
        ? INFINITE
        : static_cast<DWORD>(parsed);
}

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

void EnsureRenderFrameHook();

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
    EnsureRenderFrameHook();
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

bool IsVuplexLibraryPath(LPCWSTR path) {
    if (!path || !path[0]) return false;
    const wchar_t* name = path;
    for (const wchar_t* p = path; *p; ++p) {
        if (*p == L'\\' || *p == L'/') name = p + 1;
    }
    return _wcsicmp(name, L"VuplexWebViewWindows.dll") == 0;
}

void HoldPreVuplexStartupBoundary(LPCWSTR path) {
    if (!IsVuplexLibraryPath(path) || !g_startupBoundarySeen || !g_startupWorkerFinished) return;
    if (g_startupBoundaryClaimed.load(std::memory_order_acquire)) return;
    if (g_stopThread.load(std::memory_order_acquire) || IsShuttingDown()) return;

    // This is the same pre-load boundary used by standalone Localify. Only
    // the game's independently identified Unity window thread may hold it.
    const HWND window = FindPumpWindow();
    if (!window) {
        if (!g_startupNoWindowWarned.exchange(true, std::memory_order_acq_rel)) {
            LogWarn("[StartupTiming] Vuplex load observed before UnityWndClass existed uptime_ms=%llu thread=%lu",
                Trace::UptimeMs(), GetCurrentThreadId());
        }
        return;
    }
    if (!ConfirmPumpThread(window)) return;
    if (!g_inLoaderCallout || g_inLoaderCallout()) {
        if (!g_startupCalloutWarned.exchange(true, std::memory_order_acq_rel)) {
            LogWarn("Pre-Vuplex synchronization skipped inside an unknown/active loader callout; using loaded-module fallback");
        }
        return;
    }
    if (g_startupBoundaryClaimed.exchange(true, std::memory_order_acq_rel)) return;

    g_pumpWindow.store(window, std::memory_order_release);
    const auto started = Trace::UptimeMs();
    const DWORD waitBudget = g_startupBoundaryWaitMs;
    if (waitBudget == INFINITE) {
        LogInfo("[StartupTiming] pre-Vuplex boundary entered uptime_ms=%llu thread=%lu wait_budget=infinite",
            started, GetCurrentThreadId());
    } else {
        LogInfo("[StartupTiming] pre-Vuplex boundary entered uptime_ms=%llu thread=%lu wait_budget_ms=%lu",
            started, GetCurrentThreadId(), static_cast<unsigned long>(waitBudget));
    }
    SetEvent(g_startupBoundarySeen);

    // Do not dispatch messages while the worker is resolving metadata
    // and patching code. Its callback phase remains on that worker.
    DWORD waited = WAIT_TIMEOUT;
    if (waitBudget == 0) {
        waited = WaitForSingleObject(g_startupWorkerFinished, 0);
    } else {
        const ULONGLONG waitStarted = GetTickCount64();
        ULONGLONG lastProgressLog = waitStarted;
        while (!g_stopThread.load(std::memory_order_acquire) && !IsShuttingDown()) {
            DWORD sliceMs = 50;
            if (waitBudget != INFINITE) {
                const ULONGLONG elapsed = GetTickCount64() - waitStarted;
                if (elapsed >= waitBudget) {
                    waited = WAIT_TIMEOUT;
                    break;
                }
                const ULONGLONG remaining = waitBudget - elapsed;
                if (remaining < sliceMs) sliceMs = static_cast<DWORD>(remaining);
            }
            waited = WaitForSingleObject(g_startupWorkerFinished, sliceMs);
            if (waited != WAIT_TIMEOUT) break;
            const ULONGLONG now = GetTickCount64();
            if (waitBudget == INFINITE && now - lastProgressLog >= 10000) {
                lastProgressLog = now;
                LogInfo("[StartupTiming] pre-Vuplex boundary waiting for plugin startup elapsed_ms=%llu uptime_ms=%llu",
                    static_cast<unsigned long long>(now - waitStarted), Trace::UptimeMs());
            }
        }
    }

    if (waited == WAIT_OBJECT_0 && g_startupWorkerPrepared.load(std::memory_order_acquire)) {
        // Drain the existing main-thread queue at the original safe
        // standalone point, before first UI construction can continue.
        DispatchMainThreadTasks();
        LogInfo("[StartupTiming] pre-Vuplex boundary released after main-thread dispatch uptime_ms=%llu thread=%lu elapsed_ms=%llu",
            Trace::UptimeMs(), GetCurrentThreadId(), Trace::UptimeMs() - started);
    } else {
        LogWarn("[StartupTiming] pre-Vuplex synchronization unavailable wait=%lu prepared=%u shutdown=%u uptime_ms=%llu thread=%lu; first-UI ordering is not guaranteed",
            waited, g_startupWorkerPrepared.load(std::memory_order_acquire) ? 1u : 0u,
            (g_stopThread.load(std::memory_order_acquire) || IsShuttingDown()) ? 1u : 0u,
            Trace::UptimeMs(), GetCurrentThreadId());
    }
}

HMODULE WINAPI Detour_LoadLibraryW(LPCWSTR path) {
    HoldPreVuplexStartupBoundary(path);
    return g_originalLoadLibraryW(path);
}

HMODULE WINAPI Detour_LoadLibraryExW(LPCWSTR path, HANDLE file, DWORD flags) {
    constexpr DWORD kDataLoadFlags =
        LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE;
    if ((flags & kDataLoadFlags) == 0) {
        HoldPreVuplexStartupBoundary(path);
    }
    return g_originalLoadLibraryExW(path, file, flags);
}

// Shared RenderPipelineManager::DoRenderLoop_Internal observer hook, with
// non-conflicting fallback boundaries (Canvas::SendWillRenderCanvases,
// Application::InvokeOnBeforeRender) when another plugin owns DoRenderLoop_Internal.
// Installed lazily on the Unity main thread after plugin onRuntimeReady setup so
// plugins that install private SRP detours during onRuntimeReady run first.
using DoRenderLoopFn = void (*)(void*, intptr_t, void*, void*);
using VoidStaticFn = void (*)(void*);
DoRenderLoopFn s_origDoRenderLoop = nullptr;
void* s_doRenderLoopTarget = nullptr;
VoidStaticFn s_origSendWillRenderCanvases = nullptr;
void* s_sendWillRenderCanvasesTarget = nullptr;
VoidStaticFn s_origInvokeOnBeforeRender = nullptr;
void* s_invokeOnBeforeRenderTarget = nullptr;
std::atomic<bool> s_doRenderLoopAttempted{false};

void Detour_DoRenderLoop_Internal(void* asset, intptr_t loop, void* request, void* methodInfo) {
    static thread_local uint32_t t_renderDepth = 0;
    const bool outerNormal = (!request && t_renderDepth == 0);
    const HWND window = g_pumpWindow.load(std::memory_order_acquire);
    const bool onMain = outerNormal && ConfirmPumpThread(window);
    if (onMain) RenderFrame::InvokePhase(false, 1);
    struct DepthGuard {
        uint32_t& depth;
        explicit DepthGuard(uint32_t& d) : depth(d) { ++depth; }
        ~DepthGuard() { --depth; }
    } depthGuard(t_renderDepth);
    if (s_origDoRenderLoop) s_origDoRenderLoop(asset, loop, request, methodInfo);
    if (onMain && t_renderDepth == 1) RenderFrame::InvokePhase(true, 1);
}

bool ShouldDispatchFallbackRenderBoundary(int32_t& lastFrame, bool& hasFrame) {
    const HWND window = g_pumpWindow.load(std::memory_order_acquire);
    if (!ConfirmPumpThread(window)) return false;
    const auto readFrame = g_readFrameCount.load(std::memory_order_acquire);
    if (!readFrame) return true;
    const int32_t frame = readFrame(nullptr);
    if (hasFrame && frame == lastFrame) return false;
    hasFrame = true;
    lastFrame = frame;
    return true;
}

void RunFallbackRenderBoundary(VoidStaticFn orig, void* methodInfo,
    uint32_t& depth, int32_t& lastFrame, bool& hasFrame) {
    const bool dispatch = (depth == 0) && ShouldDispatchFallbackRenderBoundary(lastFrame, hasFrame);
    if (dispatch) RenderFrame::InvokePhase(false, 1);
    struct DepthGuard {
        uint32_t& d;
        explicit DepthGuard(uint32_t& v) : d(v) { ++d; }
        ~DepthGuard() { --d; }
    } guard(depth);
    if (orig) orig(methodInfo);
    if (dispatch) RenderFrame::InvokePhase(true, 1);
}

void Detour_SendWillRenderCanvases(void* methodInfo) {
    static thread_local uint32_t t_depth = 0;
    static int32_t s_lastFrame = 0;
    static bool s_hasFrame = false;
    RunFallbackRenderBoundary(s_origSendWillRenderCanvases, methodInfo, t_depth, s_lastFrame, s_hasFrame);
}

void Detour_InvokeOnBeforeRender(void* methodInfo) {
    static thread_local uint32_t t_depth = 0;
    static int32_t s_lastFrame = 0;
    static bool s_hasFrame = false;
    RunFallbackRenderBoundary(s_origInvokeOnBeforeRender, methodInfo, t_depth, s_lastFrame, s_hasFrame);
}

void* FindClassWithOptionalDll(const char* assemblyBase, const char* namespaze, const char* className) {
    std::string withDll = std::string(assemblyBase) + ".dll";
    if (void* klass = FindClass(withDll.c_str(), namespaze, className)) return klass;
    return FindClass(assemblyBase, namespaze, className);
}

bool TryHookRenderBoundaryTarget(void* target, void* detour, void** original,
    void*& targetSlot, const char* label) {
    if (!target) {
        GakuLoader::LogWarn("[RenderFrame] %s unavailable", label);
        return false;
    }
    const auto status = AcquireHostHook(target, detour, original);
    if (status == GAKU_OK) {
        targetSlot = target;
        RenderFrame::SetHookState(RenderFrame::HookState::Active);
        GakuLoader::LogInfo("[RenderFrame] Hooked %s (%p)", label, target);
        return true;
    }
    GakuLoader::LogWarn("[RenderFrame] Owned hook acquisition for %s failed: %d", label, status);
    return false;
}

void EnsureRenderFrameHook() {
    if (RenderFrame::Count() == 0) return;
    if (s_doRenderLoopAttempted.exchange(true, std::memory_order_acq_rel)) return;

    void* managerKlass = FindClassWithOptionalDll("UnityEngine.CoreModule", "UnityEngine.Rendering", "RenderPipelineManager");
    void* doRenderTarget = managerKlass ? FindMethodPointer(managerKlass, "DoRenderLoop_Internal", 3) : nullptr;
    if (TryHookRenderBoundaryTarget(doRenderTarget, reinterpret_cast<void*>(&Detour_DoRenderLoop_Internal),
            reinterpret_cast<void**>(&s_origDoRenderLoop), s_doRenderLoopTarget,
            "RenderPipelineManager::DoRenderLoop_Internal")) {
        return;
    }

    void* canvasKlass = FindClassWithOptionalDll("UnityEngine.UIModule", "UnityEngine", "Canvas");
    void* canvasTarget = canvasKlass ? FindMethodPointer(canvasKlass, "SendWillRenderCanvases", 0) : nullptr;
    if (TryHookRenderBoundaryTarget(canvasTarget, reinterpret_cast<void*>(&Detour_SendWillRenderCanvases),
            reinterpret_cast<void**>(&s_origSendWillRenderCanvases), s_sendWillRenderCanvasesTarget,
            "UnityEngine.Canvas::SendWillRenderCanvases")) {
        return;
    }

    void* appKlass = FindClassWithOptionalDll("UnityEngine.CoreModule", "UnityEngine", "Application");
    void* appTarget = appKlass ? FindMethodPointer(appKlass, "InvokeOnBeforeRender", 0) : nullptr;
    if (TryHookRenderBoundaryTarget(appTarget, reinterpret_cast<void*>(&Detour_InvokeOnBeforeRender),
            reinterpret_cast<void**>(&s_origInvokeOnBeforeRender), s_invokeOnBeforeRenderTarget,
            "UnityEngine.Application::InvokeOnBeforeRender")) {
        return;
    }

    RenderFrame::SetHookState(RenderFrame::HookState::Unavailable);
    GakuLoader::LogWarn("[RenderFrame] All render-frame hooks unavailable; post-render observers inactive");
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
    if (g_stopThread.load(std::memory_order_acquire) || IsShuttingDown()) {
        il2cpp_thread_detach(gcThread);
        g_runtimeThreadDetached.store(true, std::memory_order_release);
        LogInfo("[GameHook] runtime setup cancelled; worker detached before host hook installation");
        return 0;
    }

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

void ArmStartupBoundary(bool installHooks) {
    g_installHooks.store(installHooks, std::memory_order_release);
    if (!installHooks || HooksDisabledForDiagnosis()) return;
    if (g_startupBoundaryArmed.exchange(true, std::memory_order_acq_rel)) return;

    // Pure native interception, installed outside DllMain and before plugin
    // activation so Unity cannot reach VuplexWebViewWindows.dll first.
    g_startupBoundaryWaitMs = ReadStartupBoundaryWaitMs();
    g_startupBoundarySeen = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_startupWorkerFinished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_inLoaderCallout = reinterpret_cast<InLoaderCalloutFn>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlIsThreadWithinLoaderCallout"));

    auto* targetW = reinterpret_cast<void*>(&LoadLibraryW);
    const bool hookedW = g_startupBoundarySeen && g_startupWorkerFinished &&
        AcquireHostHook(targetW, reinterpret_cast<void*>(&Detour_LoadLibraryW),
            reinterpret_cast<void**>(&g_originalLoadLibraryW)) == GAKU_OK;
    bool hookedExW = false;
    auto* targetExW = reinterpret_cast<void*>(&LoadLibraryExW);
    if (hookedW && targetExW != targetW) {
        hookedExW = AcquireHostHook(targetExW, reinterpret_cast<void*>(&Detour_LoadLibraryExW),
            reinterpret_cast<void**>(&g_originalLoadLibraryExW)) == GAKU_OK;
    }

    if (hookedW) {
        if (g_startupBoundaryWaitMs == INFINITE) {
            LogInfo("[StartupTiming] pre-Vuplex native boundary armed (LoadLibraryW=1 LoadLibraryExW=%d wait=infinite) uptime_ms=%llu thread=%lu",
                hookedExW ? 1 : 0, Trace::UptimeMs(), GetCurrentThreadId());
        } else {
            LogInfo("[StartupTiming] pre-Vuplex native boundary armed (LoadLibraryW=1 LoadLibraryExW=%d wait_ms=%lu) uptime_ms=%llu thread=%lu",
                hookedExW ? 1 : 0, static_cast<unsigned long>(g_startupBoundaryWaitMs),
                Trace::UptimeMs(), GetCurrentThreadId());
        }
        if (GetModuleHandleW(L"VuplexWebViewWindows.dll")) {
            LogWarn("[StartupTiming] VuplexWebViewWindows.dll was already loaded before pre-Vuplex boundary armed uptime_ms=%llu",
                Trace::UptimeMs());
        }
    } else {
        LogWarn("Pre-Vuplex native boundary unavailable; retaining Vuplex-loaded runtime fallback");
    }
}

void StartHookThread(bool installHooks) {
    g_stopThread.store(false);
    ArmStartupBoundary(installHooks);
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
    // Runtime-ready callbacks may be waiting for cancellable native work.
    // Publish the stop signal before joining that worker or waiting for its gate.
    RequestShutdown();
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
    if (s_doRenderLoopTarget) ReleaseHostHook(s_doRenderLoopTarget);
    if (s_sendWillRenderCanvasesTarget) ReleaseHostHook(s_sendWillRenderCanvasesTarget);
    if (s_invokeOnBeforeRenderTarget) ReleaseHostHook(s_invokeOnBeforeRenderTarget);
    g_readFrameCount.store(nullptr, std::memory_order_release);
    g_readDeltaTime.store(nullptr, std::memory_order_release);
}

} // namespace GakuLoader
