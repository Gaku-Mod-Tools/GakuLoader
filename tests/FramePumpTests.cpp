// Exercise the production GameHook message path with real Win32 dispatch and
// fake Unity readers; no game, actor, renderer or managed objects are involved.
#include "../src/GameHook.cpp"
#include "../src/ActorEntry.cpp"
#include "../src/Trace.cpp"
#include <cassert>
#include <string>
#include <thread>
#include <vector>

namespace {
DWORD unityThread = 0;
std::atomic<DWORD> boundThread{0};
std::atomic<int> frameReads{0};
int32_t frameNumber = 1;
int ticks = 0;
bool shutdownRequested = false;
bool routeWake = true;
HWND gameWindow = nullptr;
bool actorTicksAllowed = false;
int actorTicks = 0;
std::vector<std::string> detourEvents;
int originalCalls = 0;
std::atomic<int> nativeLoads{0};
int startupDrains = 0;
int drainsAtNativeLoad = -1;
std::filesystem::path testConfigPath;
void* g_fakeConflictTarget = nullptr;
bool g_failAllHostHooks = false;
void* g_lastAcquiredTarget = nullptr;
int g_canvasOrigCalls = 0;
void FakeCanvasOrig(void*) { ++g_canvasOrigCalls; }
__declspec(noinline) int FakeDoRenderLoopTarget() { volatile int v = 101; return v; }
__declspec(noinline) int FakeCanvasTarget() { volatile int v = 202; return v; }
__declspec(noinline) int FakeAppBeforeRenderTarget() { volatile int v = 303; return v; }
int g_fakeManagerKlass = 1;
int g_fakeCanvasKlass = 2;
int g_fakeAppKlass = 3;
int g_fakeDomain = 4;
int g_fakeCoreAsm = 5;
int g_fakeUiAsm = 6;
GakuLoader::Il2CppMethodInfo g_doRenderInfo{reinterpret_cast<void*>(&FakeDoRenderLoopTarget)};
GakuLoader::Il2CppMethodInfo g_canvasInfo{reinterpret_cast<void*>(&FakeCanvasTarget)};
GakuLoader::Il2CppMethodInfo g_appInfo{reinterpret_cast<void*>(&FakeAppBeforeRenderTarget)};
void* FakeDomainGet() { return &g_fakeDomain; }
void* FakeDomainAsmOpen(void*, const char* name) {
    if (!name) return nullptr;
    if (strcmp(name, "UnityEngine.CoreModule.dll") == 0) return &g_fakeCoreAsm;
    if (strcmp(name, "UnityEngine.UIModule.dll") == 0) return &g_fakeUiAsm;
    return nullptr;
}
void* FakeAsmGetImage(void* asmPtr) { return asmPtr; }
void* FakeClassFromName(void* image, const char* ns, const char* name) {
    if (!ns || !name) return nullptr;
    if (image == &g_fakeCoreAsm && strcmp(ns, "UnityEngine.Rendering") == 0 && strcmp(name, "RenderPipelineManager") == 0)
        return &g_fakeManagerKlass;
    if (image == &g_fakeUiAsm && strcmp(ns, "UnityEngine") == 0 && strcmp(name, "Canvas") == 0)
        return &g_fakeCanvasKlass;
    if (image == &g_fakeCoreAsm && strcmp(ns, "UnityEngine") == 0 && strcmp(name, "Application") == 0)
        return &g_fakeAppKlass;
    return nullptr;
}
GakuLoader::Il2CppMethodInfo* FakeGetMethodFromName(void* klass, const char* name, int args) {
    if (klass == &g_fakeManagerKlass && strcmp(name, "DoRenderLoop_Internal") == 0 && args == 3) return &g_doRenderInfo;
    if (klass == &g_fakeCanvasKlass && strcmp(name, "SendWillRenderCanvases") == 0 && args == 0) return &g_canvasInfo;
    if (klass == &g_fakeAppKlass && strcmp(name, "InvokeOnBeforeRender") == 0 && args == 0) return &g_appInfo;
    return nullptr;
}
BOOLEAN inLoaderCallout = FALSE;
BOOLEAN NTAPI IsLoaderCallout() { return inLoaderCallout; }
HMODULE WINAPI NativeLoad(LPCWSTR) {
    ++nativeLoads;
    drainsAtNativeLoad = startupDrains;
    return reinterpret_cast<HMODULE>(uintptr_t{1});
}
HMODULE WINAPI NativeLoadEx(LPCWSTR, HANDLE, DWORD) {
    ++nativeLoads;
    drainsAtNativeLoad = startupDrains;
    return reinterpret_cast<HMODULE>(uintptr_t{1});
}

void RecordBefore(void*, void*) { detourEvents.emplace_back("before"); }
void RecordAfter(void*, void*) { detourEvents.emplace_back("after"); }
void GameOriginal(void*, void*) { ++originalCalls; detourEvents.emplace_back("original"); }

int32_t ReadFrame(void*) {
    assert(GetCurrentThreadId() == unityThread);
    ++frameReads;
    return frameNumber;
}
float ReadDelta(void*) {
    assert(GetCurrentThreadId() == unityThread);
    return 0.025f;
}
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (routeWake && GakuLoader::HandleFramePumpMessage(window, message, wParam, lParam)) return 0;
    return DefWindowProcW(window, message, wParam, lParam);
}
void DispatchMessages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
}

extern "C" MH_STATUS WINAPI MH_Initialize() { return MH_OK; }
namespace GakuLoader {
bool BindMainThread(DWORD thread) {
    assert(thread == unityThread);
    DWORD expected = 0;
    return boundThread.compare_exchange_strong(expected, thread) || expected == thread;
}
void TickFrame(float delta) {
    assert(GetCurrentThreadId() == unityThread);
    assert(delta == 0.025f);
    ++ticks;
    // An ABI callback may pump messages and read Time again after several ms.
    // Neither elapsed time nor nested wake delivery may create another tick.
    Sleep(3);
    HandleFramePumpMessage(gameWindow, FramePumpMessage(), 0, 0);
}
void TickActor(void*, float) {
    assert(actorTicksAllowed && "actor-free fixture reached actor dispatch");
    ++actorTicks;
    detourEvents.emplace_back("actor-tick");
}
bool IsShuttingDown() { return shutdownRequested; }
void RequestShutdown() { shutdownRequested = true; }
void NotifyRuntimeReady() {}
void DispatchMainThreadTasks() {
    assert(GetCurrentThreadId() == unityThread);
    assert(g_startupWorkerPrepared.load(std::memory_order_acquire));
    ++startupDrains;
}
GakuStatus AcquireHostHook(void* target, void*, void** original) {
    if (g_failAllHostHooks || (g_fakeConflictTarget && target == g_fakeConflictTarget)) return GAKU_E_CONFLICT;
    g_lastAcquiredTarget = target;
    if (original && target == reinterpret_cast<void*>(&FakeCanvasTarget))
        *original = reinterpret_cast<void*>(&FakeCanvasOrig);
    return GAKU_OK;
}
GakuStatus ReleaseHostHook(void*) { return GAKU_OK; }
bool CanShutdownOnCurrentThread() { return true; }
void Shutdown() {}
void DisableOwnedHooksForExit() {}
// Added to Host.hpp after this test double was written. No-op teardown matches the
// stub Shutdown() above; the gate must still run the body, because ActorEntry relies
// on it to bracket before/original/after.
void BeginExitTeardown() {}
void RunInCallbackGate(void (*body)(void*), void* userData) { body(userData); }
void LogInfo(const char*, ...) {}
void LogWarn(const char*, ...) {}
void LogError(const char*, ...) {}
void LogDebug(const char*, ...) {}
std::filesystem::path ConfigPath() { return testConfigPath; }
bool HooksDisabledForDiagnosis() { return false; }
void LogExitDiagnosisMode() {}
bool InstallDiagnosticBoundary() { return true; }
namespace DesktopUi {
void Shutdown() {
    assert(shutdownRequested && "exit waited for workers before publishing cancellation");
}
}
}

int main() {
    unityThread = GetCurrentThreadId();
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"UnityWndClass";
    assert(RegisterClassW(&windowClass));
    gameWindow = CreateWindowExW(0, windowClass.lpszClassName, L"Frame pump fixture", 0,
        0, 0, 100, 100, nullptr, nullptr, windowClass.hInstance, nullptr);
    assert(gameWindow && GakuLoader::FindPumpWindow() == gameWindow);
    GakuLoader::g_pumpWindow.store(gameWindow);
    GakuLoader::g_readFrameCount.store(ReadFrame);
    GakuLoader::g_readDeltaTime.store(ReadDelta);

    std::thread foreign([] {
        GakuLoader::HandleFramePumpMessage(gameWindow, GakuLoader::FramePumpMessage(), 0, 0);
    });
    foreign.join();
    assert(boundThread == 0 && frameReads == 0 && ticks == 0);

    // The actual wake path supplies progress with no actor/getter detour calls.
    GakuLoader::WakeFramePump(gameWindow);
    DispatchMessages();
    assert(ticks == 1 && boundThread == unityThread);
    Sleep(3);
    GakuLoader::WakeFramePump(gameWindow);
    DispatchMessages();
    assert(ticks == 1); // same Unity frame, a different wall-clock millisecond
    ++frameNumber;
    GakuLoader::WakeFramePump(gameWindow);
    DispatchMessages();
    assert(ticks == 2);

    // Simulate the first post arriving before QuitGuard was installed. The
    // pending wake must recover; repeated worker polls must not flood the queue.
    routeWake = false;
    GakuLoader::WakeFramePump(gameWindow);
    DispatchMessages();
    assert(GakuLoader::g_wakePending.load());
    routeWake = true;
    ++frameNumber;
    std::atomic<bool> retryComplete{false};
    std::thread retry([&] {
        Sleep(110);
        GakuLoader::WakeFramePump(gameWindow);
        retryComplete.store(true);
    });
    while (!retryComplete.load()) { DispatchMessages(); Sleep(1); }
    retry.join();
    DispatchMessages();
    assert(ticks == 3 && !GakuLoader::g_wakePending.load());

    // Queue admission stays bounded while the window is not dispatching.
    GakuLoader::WakeFramePump(gameWindow);
    for (int i = 0; i < 100; ++i) GakuLoader::WakeFramePump(gameWindow);
    MSG message{};
    int queued = 0;
    while (PeekMessageW(&message, gameWindow, GakuLoader::FramePumpMessage(),
            GakuLoader::FramePumpMessage(), PM_REMOVE)) ++queued;
    assert(queued == 1);
    GakuLoader::g_wakePending.store(false);

    // Shutdown is allowed to drain even when the game frame number has frozen.
    shutdownRequested = true;
    GakuLoader::WakeFramePump(gameWindow);
    DispatchMessages();
    assert(ticks == 4);

    // The production detour: shared-entry before participants, then the game
    // original exactly once, then after participants, then the loader's own
    // per-actor dispatch. No participant can take over or skip the original.
    actorTicksAllowed = true;
    GakuLoader::s_origLateUpdate = GameOriginal;
    int ownerToken = 0;
    GakuActorEntryBinding before{GAKU_SERVICE_VERSION, sizeof(GakuActorEntryBinding),
        GAKU_ACTOR_ENTRY_BEFORE, "fixture.before", RecordBefore, nullptr};
    GakuActorEntryBinding after{GAKU_SERVICE_VERSION, sizeof(GakuActorEntryBinding),
        GAKU_ACTOR_ENTRY_AFTER, "fixture.after", RecordAfter, nullptr};
    assert(GakuLoader::ActorEntry::Register(&ownerToken, &before) == GAKU_OK);
    assert(GakuLoader::ActorEntry::Register(&ownerToken, &after) == GAKU_OK);
    void* actor = reinterpret_cast<void*>(uintptr_t{7});
    GakuLoader::Detour_LateUpdate(actor, nullptr);
    assert(originalCalls == 1);
    assert((detourEvents == std::vector<std::string>{"before", "original", "after", "actor-tick"}));
    GakuLoader::ActorEntry::RetireOwner(&ownerToken);
    GakuLoader::s_origLateUpdate = nullptr;

    // Production pre-load synchronization: metadata stays on the worker;
    // queued managed work finishes on Unity's thread before the native load.
    assert(GakuLoader::g_startupBoundaryWaitMs == INFINITE);
    assert(GakuLoader::ReadStartupBoundaryWaitMs() == INFINITE);
    {
        wchar_t tempDir[MAX_PATH]{};
        const DWORD tempLen = GetTempPathW(MAX_PATH, tempDir);
        assert(tempLen > 0 && tempLen < MAX_PATH);
        testConfigPath = std::filesystem::path(tempDir) /
            (L"gakuloader-framepump-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
        DeleteFileW(testConfigPath.c_str());
        assert(GakuLoader::ReadStartupBoundaryWaitMs() == INFINITE);
        assert(WritePrivateProfileStringW(L"startup", L"boundary_wait_ms", L"45000", testConfigPath.c_str()));
        assert(GakuLoader::ReadStartupBoundaryWaitMs() == 45000);
        assert(WritePrivateProfileStringW(L"startup", L"boundary_wait_ms", L"infinite", testConfigPath.c_str()));
        assert(GakuLoader::ReadStartupBoundaryWaitMs() == INFINITE);
        assert(WritePrivateProfileStringW(L"startup", L"boundary_wait_ms", L"0", testConfigPath.c_str()));
        assert(GakuLoader::ReadStartupBoundaryWaitMs() == 0);
        assert(WritePrivateProfileStringW(L"startup", L"boundary_wait_ms", L"invalid", testConfigPath.c_str()));
        assert(GakuLoader::ReadStartupBoundaryWaitMs() == INFINITE);
        DeleteFileW(testConfigPath.c_str());
        testConfigPath.clear();
    }

    shutdownRequested = false;
    GakuLoader::g_originalLoadLibraryW = NativeLoad;
    GakuLoader::g_originalLoadLibraryExW = NativeLoadEx;
    GakuLoader::g_inLoaderCallout = IsLoaderCallout;
    GakuLoader::g_startupBoundarySeen = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    GakuLoader::g_startupWorkerFinished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    assert(GakuLoader::g_startupBoundarySeen && GakuLoader::g_startupWorkerFinished);
    GakuLoader::g_startupBoundaryWaitMs = 1000;
    const auto beforeStartupTicks = ticks;
    GakuLoader::Detour_LoadLibraryW(L"unrelated.dll");
    assert(nativeLoads == 1 && !GakuLoader::g_startupBoundaryClaimed.load());
    std::thread foreignLoad([] { GakuLoader::Detour_LoadLibraryW(L"VuplexWebViewWindows.dll"); });
    foreignLoad.join();
    assert(nativeLoads == 2 && !GakuLoader::g_startupBoundaryClaimed.load());
    inLoaderCallout = TRUE;
    GakuLoader::Detour_LoadLibraryW(L"VuplexWebViewWindows.dll");
    assert(nativeLoads == 3 && !GakuLoader::g_startupBoundaryClaimed.load());
    inLoaderCallout = FALSE;
    std::thread startupWorker([] {
        assert(WaitForSingleObject(GakuLoader::g_startupBoundarySeen, 1000) == WAIT_OBJECT_0);
        assert(GetCurrentThreadId() != unityThread);
        GakuLoader::g_startupWorkerPrepared.store(true, std::memory_order_release);
        SetEvent(GakuLoader::g_startupWorkerFinished);
    });
    GakuLoader::Detour_LoadLibraryW(L"plugin\\VuplexWebViewWindows.dll");
    startupWorker.join();
    assert(nativeLoads == 4 && startupDrains == 1 && drainsAtNativeLoad == 1);
    assert(ticks == beforeStartupTicks); // no synthetic frame or plugin onTick
    GakuLoader::Detour_LoadLibraryW(L"VuplexWebViewWindows.dll");
    assert(nativeLoads == 5 && startupDrains == 1); // one-time boundary
    ResetEvent(GakuLoader::g_startupWorkerFinished);
    ResetEvent(GakuLoader::g_startupBoundarySeen);
    GakuLoader::g_startupBoundaryClaimed.store(false);
    GakuLoader::g_startupWorkerPrepared.store(false);
    GakuLoader::g_startupBoundaryWaitMs = 10;
    GakuLoader::Detour_LoadLibraryW(L"VuplexWebViewWindows.dll");
    assert(nativeLoads == 6 && startupDrains == 1); // timeout never runs managed work
    assert(ticks == beforeStartupTicks);

    // Hard-block (INFINITE) via LoadLibraryExW + forward-slash path must wait
    // across multiple slices until a slow worker finishes, while datafile loads
    // never claim the boundary.
    ResetEvent(GakuLoader::g_startupWorkerFinished);
    ResetEvent(GakuLoader::g_startupBoundarySeen);
    GakuLoader::g_startupBoundaryClaimed.store(false);
    GakuLoader::g_startupWorkerPrepared.store(false);
    GakuLoader::g_startupBoundaryWaitMs = INFINITE;
    GakuLoader::Detour_LoadLibraryExW(L"plugin/VuplexWebViewWindows.dll", nullptr, LOAD_LIBRARY_AS_DATAFILE);
    assert(nativeLoads == 7 && !GakuLoader::g_startupBoundaryClaimed.load());
    std::atomic<bool> slowWorkerDone{false};
    std::thread slowWorker([&] {
        assert(WaitForSingleObject(GakuLoader::g_startupBoundarySeen, 1000) == WAIT_OBJECT_0);
        Sleep(160); // > 50ms slice to exercise multi-slice hard wait
        GakuLoader::g_startupWorkerPrepared.store(true, std::memory_order_release);
        slowWorkerDone.store(true, std::memory_order_release);
        SetEvent(GakuLoader::g_startupWorkerFinished);
    });
    GakuLoader::Detour_LoadLibraryExW(L"F:/game/Plugins/x86_64/VuplexWebViewWindows.dll", nullptr, 0);
    assert(slowWorkerDone.load(std::memory_order_acquire));
    slowWorker.join();
    assert(nativeLoads == 8 && startupDrains == 2 && drainsAtNativeLoad == 2);

    // Early ArmStartupBoundary + delayed StartHookThread ordering: main thread
    // enters boundary before worker exists, then releases once worker finishes.
    ResetEvent(GakuLoader::g_startupWorkerFinished);
    ResetEvent(GakuLoader::g_startupBoundarySeen);
    GakuLoader::g_startupBoundaryClaimed.store(false);
    GakuLoader::g_startupWorkerPrepared.store(false);
    GakuLoader::g_startupBoundaryWaitMs = INFINITE;
    std::thread delayedActivationWorker([&] {
        assert(WaitForSingleObject(GakuLoader::g_startupBoundarySeen, 1000) == WAIT_OBJECT_0);
        Sleep(80);
        GakuLoader::g_startupWorkerPrepared.store(true, std::memory_order_release);
        SetEvent(GakuLoader::g_startupWorkerFinished);
    });
    GakuLoader::Detour_LoadLibraryW(L"VuplexWebViewWindows.dll");
    delayedActivationWorker.join();
    assert(nativeLoads == 9 && startupDrains == 3 && drainsAtNativeLoad == 3);

    // Hard-block (INFINITE) must still release promptly when shutdown is requested.
    ResetEvent(GakuLoader::g_startupWorkerFinished);
    ResetEvent(GakuLoader::g_startupBoundarySeen);
    GakuLoader::g_startupBoundaryClaimed.store(false);
    GakuLoader::g_startupWorkerPrepared.store(false);
    GakuLoader::g_startupBoundaryWaitMs = INFINITE;
    std::thread cancelThread([&] {
        assert(WaitForSingleObject(GakuLoader::g_startupBoundarySeen, 1000) == WAIT_OBJECT_0);
        Sleep(30);
        GakuLoader::RequestShutdown();
    });
    GakuLoader::Detour_LoadLibraryW(L"VuplexWebViewWindows.dll");
    cancelThread.join();
    assert(nativeLoads == 10 && startupDrains == 3); // cancelled wait never drains tasks
    shutdownRequested = false;

    CloseHandle(GakuLoader::g_startupBoundarySeen);
    CloseHandle(GakuLoader::g_startupWorkerFinished);
    GakuLoader::g_startupBoundarySeen = GakuLoader::g_startupWorkerFinished = nullptr;

    assert(!shutdownRequested);
    // RenderFrame fallback: when DoRenderLoop_Internal conflicts (-5), fall back to
    // Canvas::SendWillRenderCanvases, set HookState::Active, and deduplicate per Unity frame.
    {
        GakuLoader::il2cpp_domain_get = FakeDomainGet;
        GakuLoader::il2cpp_domain_assembly_open = FakeDomainAsmOpen;
        GakuLoader::il2cpp_assembly_get_image = FakeAsmGetImage;
        GakuLoader::il2cpp_class_from_name = FakeClassFromName;
        GakuLoader::il2cpp_class_get_method_from_name = FakeGetMethodFromName;

        static int beforeCalls = 0, afterCalls = 0;
        beforeCalls = afterCalls = 0;
        int rfOwner = 0;
        GakuRenderFrameBinding rfBinding{
            GAKU_SERVICE_VERSION, sizeof(GakuRenderFrameBinding), "framepump.rf",
            [](void*, int normal) { if (normal) ++beforeCalls; },
            [](void*, int normal) { if (normal) ++afterCalls; },
            nullptr
        };
        assert(GakuLoader::RenderFrame::Register(&rfOwner, &rfBinding) == GAKU_OK);
        GakuLoader::s_doRenderLoopAttempted.store(false);
        GakuLoader::RenderFrame::SetHookState(GakuLoader::RenderFrame::HookState::Pending);
        g_fakeConflictTarget = reinterpret_cast<void*>(&FakeDoRenderLoopTarget);
        g_failAllHostHooks = false;
        g_lastAcquiredTarget = nullptr;
        GakuLoader::EnsureRenderFrameHook();
        assert(g_lastAcquiredTarget == reinterpret_cast<void*>(&FakeCanvasTarget));
        assert(GakuLoader::RenderFrame::GetHookState() == GakuLoader::RenderFrame::HookState::Active);

        frameNumber = 77;
        GakuLoader::Detour_SendWillRenderCanvases(nullptr);
        GakuLoader::Detour_SendWillRenderCanvases(nullptr); // same Unity frame -> deduplicated
        assert(beforeCalls == 1 && afterCalls == 1 && g_canvasOrigCalls == 2);
        frameNumber = 78;
        GakuLoader::Detour_SendWillRenderCanvases(nullptr);
        assert(beforeCalls == 2 && afterCalls == 2 && g_canvasOrigCalls == 3);

        // If all targets fail hook acquisition, HookState must become Unavailable (never lie!).
        GakuLoader::s_doRenderLoopAttempted.store(false);
        GakuLoader::RenderFrame::SetHookState(GakuLoader::RenderFrame::HookState::Pending);
        g_failAllHostHooks = true;
        GakuLoader::EnsureRenderFrameHook();
        assert(GakuLoader::RenderFrame::GetHookState() == GakuLoader::RenderFrame::HookState::Unavailable);

        GakuLoader::RenderFrame::RetireOwner(&rfOwner);
        g_fakeConflictTarget = nullptr;
        g_failAllHostHooks = false;
    }

    GakuLoader::DisableAllHooksForExit();
    assert(shutdownRequested);

    DestroyWindow(gameWindow);
    UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
    puts("PASS: production Win32 wake path; worker rejection; actor-free progress; actual-frame dedup; reentry; lost-wake retry; bounded queue; frozen-frame shutdown; shared entry before/original/after then actor tick");
    puts("PASS: pre-Vuplex worker completion then Unity-thread queue before native load; no synthetic tick; unrelated/foreign/loader-callout rejection; one-time gate; timeout fallback");
    puts("PASS: shutdown cancellation is published before desktop/runtime worker teardown");
}
