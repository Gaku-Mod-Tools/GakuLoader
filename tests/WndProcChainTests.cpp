// Regression coverage for the game-window WndProc chain.
//
// QuitGuard installs a single GWLP_WNDPROC subclass (GuardedWndProc) on the
// Unity window. It consumes exactly three classes of message (FramePump wake,
// UnityQuit custom message, WM_CLOSE) and forwards everything else with one
// CallWindowProc. This test builds a real UnityWndClass window, adds a
// third-party WndProc either before or after QuitGuard, and drives keyboard /
// IME / mouse / app messages through the chain. Every forwarded message must
// reach the chain bottom exactly once while the three consumed classes never
// do. No production source is modified; the production QuitGuard.cpp is
// included and only its external boundaries are replaced with counting stubs.
//
// Messages are delivered with SendMessageW (a real Win32 window-procedure
// dispatch) so the count is exact: it avoids coalescing of posted input and
// stray WM_PAINT-style messages that a fresh hidden window leaves queued. A
// real PeekMessage/DispatchMessage pump drains those strays and QueueEmpty()
// asserts the chain never re-posts or recurses.
#include "../src/QuitGuard.cpp"
#include <cassert>
#include <cstdio>
#include <vector>

#pragma comment(lib, "user32.lib")

// Message ids claimed by the two classification boundaries, plus one ordinary
// application message that must be forwarded. All stay inside the WM_APP range.
constexpr UINT kFramePumpMsg = WM_APP + 0x51;
constexpr UINT kQuitMsg      = WM_APP + 0x52;
constexpr UINT kAppMsg       = WM_APP + 0x3A7;

struct ChainCounts {
    int bottom = 0;        // messages that reached the chain bottom
    int third = 0;         // messages seen by the third-party WndProc
    int framePump = 0;     // HandleFramePumpMessage consumption
    int quitMsg = 0;       // HandleUnityQuitMessage consumption
    int quitRequests = 0;  // RequestUnityQuit (from swallowed WM_CLOSE)
};
ChainCounts g_counts;
std::vector<UINT> g_bottomMessages;
WNDPROC g_thirdPrev = nullptr;

LRESULT CALLBACK ChainBottomProc(HWND, UINT message, WPARAM, LPARAM) {
    // WM_NCCREATE must return TRUE; returning 0 aborts CreateWindowEx. The
    // production window class goes through DefWindowProc for the same reason.
    if (message == WM_NCCREATE) return TRUE;
    ++g_counts.bottom;
    g_bottomMessages.push_back(message);
    return 0; // do not call DefWindowProc: keep the queue free of derived messages
}
LRESULT CALLBACK ThirdPartyProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    ++g_counts.third;
    return CallWindowProc(g_thirdPrev, window, message, wparam, lparam);
}
void DispatchAll() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        DispatchMessageW(&message);
}
bool QueueEmpty() {
    MSG message{};
    return !PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
}

// Production boundaries referenced by QuitGuard.cpp, replaced with counters.
namespace GakuLoader {
bool BindMainThread(DWORD) { return true; }
bool HandleFramePumpMessage(HWND, UINT message, WPARAM, LPARAM) {
    if (message == ::kFramePumpMsg) { ++::g_counts.framePump; return true; }
    return false;
}
bool HandleUnityQuitMessage(HWND, UINT message) {
    if (message == ::kQuitMsg) { ++::g_counts.quitMsg; return true; }
    return false;
}
void RequestUnityQuit(HWND) { ++::g_counts.quitRequests; }
void LogInfo(const char*, ...) {}
void LogWarn(const char*, ...) {}
void LogError(const char*, ...) {}
void LogDebug(const char*, ...) {}
void Shutdown() {}
void LogShutdownProgress() {}
UnityQuitPhase GetUnityQuitPhase() { return UnityQuitPhase::Idle; }

// Expose QuitGuard.cpp's anonymous-namespace chain state to the test. QuitGuard
// itself relies on the same visibility in StopQuitGuard(), so these are safe.
WNDPROC TestGuardedProc() { return GuardedWndProc; }
WNDPROC TestPrevProc() { return g_prevWndProc; }
void TestSetPrevProc(WNDPROC proc) { g_prevWndProc = proc; }
} // namespace GakuLoader

int main() {
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = ChainBottomProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"UnityWndClass";
    assert(RegisterClassW(&windowClass));
    HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"WndProc chain fixture", 0,
        0, 0, 100, 100, nullptr, nullptr, windowClass.hInstance, nullptr);
    assert(window);

    const UINT forwarded[] = {WM_KEYDOWN, WM_KEYUP, WM_CHAR, WM_SYSKEYDOWN, WM_SYSKEYUP,
                              WM_MOUSEMOVE, WM_IME_STARTCOMPOSITION, kAppMsg};
    const bool orders[] = {true, false};
    for (bool thirdPartyFirst : orders) {
        // Reset to the bare class WndProc so both scenarios start identically,
        // then drain any stray posted messages (WM_PAINT etc.) before counting.
        SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ChainBottomProc));
        DispatchAll();
        g_counts = ChainCounts{};
        g_bottomMessages.clear();
        g_thirdPrev = nullptr;

        WNDPROC guarded = GakuLoader::TestGuardedProc();
        WNDPROC prev = nullptr;
        if (thirdPartyFirst) {
            // Third party installs first, below QuitGuard.
            g_thirdPrev = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
                window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ThirdPartyProc)));
            assert(g_thirdPrev == ChainBottomProc);
            prev = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
                window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(guarded)));
            assert(prev == ThirdPartyProc);
        } else {
            // QuitGuard installs first; third party sits above it.
            prev = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
                window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(guarded)));
            assert(prev == ChainBottomProc);
            g_thirdPrev = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
                window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ThirdPartyProc)));
            assert(g_thirdPrev == guarded);
        }
        GakuLoader::TestSetPrevProc(prev);

        // The saved chain must stay intact and never point back at GuardedWndProc
        // itself (a self-call loop would replay messages forever).
        assert(GakuLoader::TestPrevProc() == prev);
        assert(GakuLoader::TestPrevProc() != nullptr);
        assert(GakuLoader::TestPrevProc() != guarded);

        for (UINT message : forwarded) {
            const int before = g_counts.bottom;
            SendMessageW(window, message, 0, 0);                // synchronous: one chain traversal
            assert(g_counts.bottom == before + 1);             // exactly once at the bottom
            assert(!g_bottomMessages.empty() && g_bottomMessages.back() == message);
            assert(QueueEmpty());                               // no recursion / re-posting
        }
        assert(g_counts.bottom == 8);

        const int bottomAfterForward = g_counts.bottom;

        // WM_CLOSE is swallowed and triggers RequestUnityQuit; it never reaches
        // the bottom of the chain.
        const int quitBefore = g_counts.quitRequests;
        SendMessageW(window, WM_CLOSE, 0, 0);
        assert(g_counts.quitRequests == quitBefore + 1);
        assert(g_counts.bottom == bottomAfterForward);
        assert(QueueEmpty());

        // FramePump registered message and the Quit custom message are consumed
        // by the two classification boundaries and never forwarded.
        const int framePumpBefore = g_counts.framePump;
        SendMessageW(window, kFramePumpMsg, 0, 0);
        assert(g_counts.framePump == framePumpBefore + 1);
        assert(g_counts.bottom == bottomAfterForward);
        assert(QueueEmpty());

        const int quitMsgBefore = g_counts.quitMsg;
        SendMessageW(window, kQuitMsg, 0, 0);
        assert(g_counts.quitMsg == quitMsgBefore + 1);
        assert(g_counts.bottom == bottomAfterForward);
        assert(QueueEmpty());

        // g_prevWndProc survived the whole scenario unchanged.
        assert(GakuLoader::TestPrevProc() == prev);
        assert(GakuLoader::TestPrevProc() != guarded);
    }

    DestroyWindow(window);
    UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
    puts("PASS: GuardedWndProc forwards WM_KEYDOWN/KEYUP/CHAR/SYSKEYDOWN/SYSKEYUP/MOUSEMOVE/IME_STARTCOMPOSITION/WM_APP exactly once");
    puts("PASS: WM_CLOSE swallowed -> RequestUnityQuit; FramePump + Quit messages consumed; no recursion, no duplicate posts");
    puts("PASS: g_prevWndProc chain preserved with third-party WndProc installed before and after QuitGuard");
    return 0;
}
