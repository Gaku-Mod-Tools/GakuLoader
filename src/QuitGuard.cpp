// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

// =============================================================================
// QuitGuard — 进程退出兜底
// =============================================================================
// 背景：Gakumas 关闭时可能停在"音乐停止、窗口未响应"的状态：
//         - 实测 212/213 线程 CPU 全部零增长，进程完全静止
//         - 连 comsvcs / ProcDump 都取不到信息（目标已不执行代码）
//       宿主自己拥有 UnityWndClass 的窗口过程（Loader 是唯一的窗口 hook
//       owner，不再让位给其它 host），因此关闭路径也由这里统一负责。
//
// 职责：收到关闭请求后先请求 Application.Quit()；若经过宽限期进程仍存活，
//       则强制结束。这是本地 fork 的代码，不改上游 Localify / VRify。
//
// 设计约束（重要，与实现一致）：
//   1. 不把 WM_CLOSE 转发给 Unity 原生窗口过程。实测转发后该过程同步卡死
//      且永不返回（"CallWindowProc 返回后"的日志从未出现），因此改为消费
//      WM_CLOSE 并请求 Application.Quit()，走 Unity 自己的退出路径。
//   2. 不在 WndProc 内阻塞等待 —— 否则窗口在宽限期内会真的"未响应"。
//      改为独立线程轮询。
//   3. 只在确认收到过关闭请求后才可能终止进程，绝不主动杀进程。
// =============================================================================

#include "QuitGuard.hpp"
#include "GameHook.hpp"
#include "Host.hpp"
#include "UnityQuit.hpp"

#include <windows.h>
#include <atomic>
#include <tlhelp32.h>
#include <cwchar>

namespace GakuLoader {
namespace {

WNDPROC  g_prevWndProc = nullptr;
HWND     g_gameWindow = nullptr;
std::atomic_bool g_closeSeen{false};
bool     g_warnedMissing = false;
HANDLE   g_watchdogDone = nullptr;

// Unity 的窗口类与标题。用类名而非标题匹配：标题随语言/版本变化，类名稳定。
constexpr const wchar_t* kUnityWindowClass = L"UnityWndClass";

// 定位本进程的 Unity 窗口。
//
// 不能只靠 FindWindowW(类名, 标题)：标题 "gakumas" 随语言包/版本变化，
// 且可能存在同名但属于其他进程的窗口。VRify 早期也踩过这个坑，
// 现在的做法是：先按类名+标题试，再退化为按类名枚举并校验 PID。
// 见 gakumas-VRify src/host/VrWindowsPlatform.cpp 的 InstallWindowProcedure。
HWND FindUnityWindow() {
    const DWORD selfPid = GetCurrentProcessId();

    HWND hWnd = FindWindowW(kUnityWindowClass, L"gakumas");
    if (hWnd != nullptr) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hWnd, &pid);
        if (pid == selfPid) return hWnd;
        hWnd = nullptr;
    }

    HWND candidate = nullptr;
    while ((candidate = FindWindowExW(nullptr, candidate, kUnityWindowClass, nullptr)) != nullptr) {
        DWORD pid = 0;
        GetWindowThreadProcessId(candidate, &pid);
        if (pid == selfPid) return candidate;
    }
    return nullptr;
}

// 前置声明：诊断函数需要取GuardedWndProc 的地址做自比对，
// 而其定义在下方。
LRESULT CALLBACK GuardedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

// 前置声明：看门狗在终止前会清理崩溃处理器孤儿，定义在其下方。
void CleanUpCrashHandlerOrphans();

// 诊断：只保留「自调用环」这一项不变式。
//
// 原来的 5 行链形态转储（self / prev / current 三个模块路径）在发布版里没有
// 增量信息：三个地址每次启动都相同，而它带来的 3 行空消息反而污染会话头。
// 真正会让 WM_CLOSE 在本进程内打转的只有 prev == GuardedWndProc 这一种形态，
// 其余情况一律不打印。
void DiagnoseWndProcChain(HWND hWnd, const char* stage) {
    (void)hWnd;
    static std::atomic_int printed{0};
    if (printed.fetch_add(1, std::memory_order_acq_rel) > 1) return;
    if (g_prevWndProc != reinterpret_cast<WNDPROC>(&GuardedWndProc)) return;
    LogError("[QuitGuard][DIAG] %s: self-call loop detected (prev == GuardedWndProc); "
             "WM_CLOSE would spin inside this process", stage);
}

// 清理 UnityCrashHandler64 孤儿。
//
// 强杀本进程后，Unity 的崩溃处理器会残留。它加载了游戏目录下的 version.dll
// 并锁住文件，导致下一次部署无法覆盖（实测多次被此问题挡住）。
// 它由本进程派生，父进程已死，所以在本进程存活期间它必然是自己的子进程，
// 可以用快照枚举后按父 PID 精确终止，不波及其他同名进程。
void CleanUpCrashHandlerOrphans() {
    const DWORD selfPid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    int killed = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ParentProcessID == selfPid &&
                wcsstr(pe.szExeFile, L"UnityCrashHandler") != nullptr) {
                HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (h != nullptr) {
                    if (TerminateProcess(h, 0)) {
                        LogInfo("[QuitGuard] terminated orphan %ls (pid=%lu)",
                                pe.szExeFile, pe.th32ProcessID);
                        ++killed;
                    }
                    CloseHandle(h);
                }
            }
            pe.dwSize = sizeof(pe);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    if (killed == 0) {
        LogDebug("[QuitGuard] no UnityCrashHandler orphan found (it may exit on its own)");
    }
}

// 拦截 WM_CLOSE：不再转发给 Unity 原生窗口过程。
//
// 实测依据：转发后Unity 原生窗口过程**同步卡死且永不返回**。判定证据是
// QuitGuard 中"CallWindowProc 返回后"的那两行日志从未出现，而它们的上一行
// （转发目标解析）正常打印 —— 说明线程停在 CallWindowProc 内部出不来。
// 当时进程 212/213 线程 CPU 全部零增长，也无法抓 dump。
//
// 备选方案已排除：
//   - SC_CLOSE：Windows 会把它翻译回 WM_CLOSE，走同一条死路。
//   - 直接 DestroyWindow：DX11 渲染线程会因无效 HWND 非法访问而崩。
//
// 因此这里彻底掐断转发，改为请求 Application.Quit()（见 UnityQuit.cpp），
// 走 Unity 自己的退出路径。
LRESULT CALLBACK GuardedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    const DWORD windowThread = GetWindowThreadProcessId(hWnd, nullptr);
    if (windowThread == GetCurrentThreadId()) GakuLoader::BindMainThread(windowThread);
    if (GakuLoader::HandleFramePumpMessage(hWnd, uMsg, wParam, lParam)) return 0;
    // 本层投递的退出请求：在此消费，确保运行在 Unity 主线程。
    if (GakuLoader::HandleUnityQuitMessage(hWnd, uMsg)) {
        return 0;
    }
    if (uMsg == WM_CLOSE) {
        g_closeSeen = true;
        GakuLoader::LogInfo("[QuitGuard] WM_CLOSE swallowed (not forwarded to Unity); "
                               "requesting Application.Quit instead");
        GakuLoader::RequestUnityQuit(hWnd);
        return 0;
    }
    return CallWindowProc(g_prevWndProc, hWnd, uMsg, wParam, lParam);
}

// 退出看门狗：Application.Quit() 发出后，若进程没有自行结束则强制终止。
//
// Two budgets, because they measure different things:
//   * teardown budget - Desktop drain, plugin teardown, worker joins. It used to
//     share a five second ceiling with Unity's own exit, which expired 1.2 s
//     after Application.Quit returned and reported a hang that may not exist.
//   * post-quit budget - measured from the moment Application.Quit returns. Only
//     this one says whether Unity exits on its own.
// The absolute ceiling keeps a wedged runtime from blocking the process forever.
DWORD WINAPI ExitWatchdog(LPVOID) {
    constexpr DWORD kTeardownBudgetMs = 15000;
    constexpr DWORD kPostQuitBudgetMs = 8000;
    constexpr DWORD kCeilingMs = 40000;

    bool quitReturned = false;
    DWORD quitReturnedAt = 0;
    for (DWORD waited = 0; waited < kCeilingMs; waited += 50) {
        Sleep(50);
        if (!g_closeSeen) {
            LogInfo("[QuitGuard] watchdog cancelled; no exit was requested");
            CloseHandle(g_watchdogDone);
            g_watchdogDone = nullptr;
            return 0;
        }
        // 窗口已消失 => Unity 自己走完了退出流程，无需干预。
        if (!IsWindow(g_gameWindow)) {
            LogInfo("[QuitGuard] window destroyed after %lu ms; Unity exited on its own", waited);
            CloseHandle(g_watchdogDone);
            g_watchdogDone = nullptr;
            return 0;
        }
        const auto phase = GetUnityQuitPhase();
        if (!quitReturned && phase == UnityQuitPhase::Returned) {
            quitReturned = true;
            quitReturnedAt = waited;
            LogInfo("[QuitGuard] Application.Quit returned after %lu ms; allowing %lu ms for runtime exit",
                    waited, kPostQuitBudgetMs);
        }
        if (quitReturned && waited - quitReturnedAt >= kPostQuitBudgetMs) break;
        if (!quitReturned && waited >= kTeardownBudgetMs) break;
    }

    LogError("[QuitGuard] process alive after close request; forcing exit");
    {
        // Distinguish "still finishing frames" from "blocked": a window thread
        // that does not answer WM_NULL within 250 ms has stopped pumping
        // messages, so the remaining wait cannot complete on its own.
        DWORD_PTR answer = 0;
        const DWORD thread = GetWindowThreadProcessId(g_gameWindow, nullptr);
        const auto responded = SendMessageTimeoutW(g_gameWindow, WM_NULL, 0, 0,
            SMTO_ABORTIFHUNG | SMTO_BLOCK, 250, &answer) != 0;
        unsigned long long cpuMs = 0;
        const HANDLE threadHandle = thread ? OpenThread(THREAD_QUERY_INFORMATION, FALSE, thread) : nullptr;
        if (threadHandle) {
            FILETIME created{}, exited{}, kernel{}, user{};
            if (GetThreadTimes(threadHandle, &created, &exited, &kernel, &user)) {
                cpuMs = (static_cast<unsigned long long>(kernel.dwHighDateTime) << 32 | kernel.dwLowDateTime) +
                        (static_cast<unsigned long long>(user.dwHighDateTime) << 32 | user.dwLowDateTime);
                cpuMs /= 10000; // 100 ns ticks -> ms
            }
            CloseHandle(threadHandle);
        }
        LogError("[QuitGuard] window thread tid=%lu responded=%d hung=%d cpu_ms=%llu",
            thread, responded ? 1 : 0, IsHungAppWindow(g_gameWindow) ? 1 : 0, cpuMs);
    }
    const auto phase = GetUnityQuitPhase();
    if (phase == UnityQuitPhase::HostShutdown) {
        LogError("[QuitGuard] Application.Quit resolved; host teardown is still active; game Quit has not been invoked");
        LogShutdownProgress();
    } else if (phase == UnityQuitPhase::Calling || phase == UnityQuitPhase::Returned) {
        LogError("[QuitGuard] Application.Quit call was issued; runtime exit remains pending (phase=%s)",
            phase == UnityQuitPhase::Returned ? "call-returned" : "in-call");
    } else if (phase == UnityQuitPhase::ResolveFailed) {
        LogError("[QuitGuard] Application.Quit method resolution failed");
    } else if (phase == UnityQuitPhase::PostFailed) {
        LogError("[QuitGuard] quit message could not be scheduled");
    } else {
        LogError("[QuitGuard] quit request pending before host teardown (phase=%d)", static_cast<int>(phase));
    }

    // 先清孤儿再杀本进程：否则崩溃处理器残留会锁住 version.dll。
    CleanUpCrashHandlerOrphans();

    // 给插件一次收尾机会，再强制结束。逆序调用以匹配加载顺序。
    GakuLoader::Shutdown();

    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}

} // namespace

void StopQuitGuard() {
    if (g_gameWindow != nullptr && g_prevWndProc != nullptr) {
        // Desktop presentation may have joined the chain after QuitGuard.
        // Restoring through that newer owner would sever its WndProc layer.
        if (reinterpret_cast<WNDPROC>(GetWindowLongPtrW(g_gameWindow, GWLP_WNDPROC)) == GuardedWndProc)
            SetWindowLongPtrW(g_gameWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_prevWndProc));
        g_gameWindow = nullptr;
        g_prevWndProc = nullptr;
    }
    g_closeSeen = false;
}

// 轮询线程：等待 Unity 窗口出现后装上 WndProc 层。
//
// 为什么必须是独立线程而不是挂在 IL2CPP hook 上：
//   退出兜底必须不依赖插件 host 的加载模式、actor hook 状态或 OpenXR 初始化顺序。
//
// 本线程只调用 user32：FindWindowExW / GetWindowThreadProcessId /
// SetWindowLongPtrW。不触碰 IL2CPP，不与任何 MinHook 链竞争，因此不可被覆盖。
// 线程本身也是安全的：宿主与 VRify 本就各自 CreateThread 启动常驻线程，
// 游戏不阻止额外线程（实测 305 线程、35 个活跃调度）。
DWORD WINAPI QuitGuardThread(LPVOID) {
    constexpr DWORD kPollMs = 100;

    for (;;) {
        if (g_gameWindow == nullptr) {
            g_gameWindow = FindUnityWindow();
            if (g_gameWindow != nullptr) {
                SetLastError(0);
                g_prevWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
                    g_gameWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(GuardedWndProc)));
                if (g_prevWndProc == nullptr && GetLastError() != 0) {
                    LogError("[QuitGuard] SetWindowLongPtr(GWLP_WNDPROC) failed error=%lu; "
                             "exit guard inactive", GetLastError());
                    g_gameWindow = nullptr;
                } else {
                    LogInfo("[QuitGuard] exit guard armed on hwnd=0x%p prev=0x%p",
                            static_cast<void*>(g_gameWindow),
                            reinterpret_cast<void*>(g_prevWndProc));
                    // 记录装钩瞬间的链形态。VRify 的 InstallWindowProcedure挂在
                    // il2cpp_init 上，时机晚于本线程，因此这里读到的 prev 很可能
                    // 已经是 Unity 原生；随后 VRify 会再覆盖一层，把自己插到
                    // 本钩子之上。WM_CLOSE 时的诊断会揭示最终形态。
                    DiagnoseWndProcChain(g_gameWindow, "exit guard installed");
                }
            } else if (!g_warnedMissing) {
                // 只提示一次，避免轮询刷屏掩盖其它日志。
                g_warnedMissing = true;
                LogWarn("[QuitGuard] Unity window not found yet; polling every %lu ms", kPollMs);
            }
        }

        if (g_closeSeen && g_watchdogDone == nullptr) {
            g_watchdogDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (g_watchdogDone != nullptr) {
                LogInfo("[QuitGuard] close requested; starting exit watchdog");
                CreateThread(nullptr, 0, ExitWatchdog, nullptr, 0, nullptr);
            }
        }

        Sleep(kPollMs);
    }
    return 0;
}

void StartQuitGuard() {
    static std::atomic_bool started{false};
    if (started.exchange(true, std::memory_order_acq_rel)) return;

    const HANDLE t = CreateThread(nullptr, 0, QuitGuardThread, nullptr, 0, nullptr);
    if (t != nullptr) {
        CloseHandle(t);
        LogInfo("[QuitGuard] guard thread started");
    } else {
        LogError("[QuitGuard] CreateThread failed error=%lu; exit guard inactive", GetLastError());
    }
}

} // namespace GakuLoader
