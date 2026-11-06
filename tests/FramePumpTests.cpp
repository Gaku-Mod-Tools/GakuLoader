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
BOOLEAN inLoaderCallout = FALSE;
BOOLEAN NTAPI IsLoaderCallout() { return inLoaderCallout; }
HMODULE WINAPI NativeLoad(LPCWSTR) {
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
GakuStatus AcquireHostHook(void*, void*, void**) { return GAKU_OK; }
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
    shutdownRequested = false;
    GakuLoader::g_originalLoadLibraryW = NativeLoad;
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
    CloseHandle(GakuLoader::g_startupBoundarySeen);
    CloseHandle(GakuLoader::g_startupWorkerFinished);
    GakuLoader::g_startupBoundarySeen = GakuLoader::g_startupWorkerFinished = nullptr;

    assert(!shutdownRequested);
    GakuLoader::DisableAllHooksForExit();
    assert(shutdownRequested);

    DestroyWindow(gameWindow);
    UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
    puts("PASS: production Win32 wake path; worker rejection; actor-free progress; actual-frame dedup; reentry; lost-wake retry; bounded queue; frozen-frame shutdown; shared entry before/original/after then actor tick");
    puts("PASS: pre-Vuplex worker completion then Unity-thread queue before native load; no synthetic tick; unrelated/foreign/loader-callout rejection; one-time gate; timeout fallback");
    puts("PASS: shutdown cancellation is published before desktop/runtime worker teardown");
}
