// =============================================================================
// UnityQuit — 在 Unity 主线程安全上下文里调用 Application.Quit()
// =============================================================================
// 背景：实测（见 DevAgents 记录）Unity 的原生窗口过程处理 WM_CLOSE 时**同步
//       卡死、不返回**。表现为主窗口线程停在 CallWindowProc 内部，进程 212/213
//       线程 CPU 全部零增长，连 dump 都取不到（目标已不执行用户态代码）。
//
// 因此不能走"把 WM_CLOSE 转发给 Unity"这条路。改用 Unity 自己的退出 API：
// 在窗口线程上挂一个定时器，定时器回调必然运行在 Unity 主线程处理窗口消息的
// 上下文里，此时调用 Application.Quit(0) 是安全的。
//
// 为什么不用 Detour_LateUpdate 注入：
//   退出请求独立于插件 host 的选择以及 actor detour 是否安装。窗口消息路径
//   可以在 Localify 接管 actor hook 时继续工作。
//
// 注意：Application.Quit 只是请求退出，Unity 仍可能在其清理路径上卡住。
//       因此 QuitGuard 的看门狗仍然保留，作为最终兜底。
// =============================================================================

#include "UnityQuit.hpp"
#include "GameHook.hpp"
#include "Host.hpp"

#include <windows.h>
#include <atomic>

namespace GakuLoader {

using QuitFn = void (*)(int exitCode);

namespace UnityQuitImpl {

QuitFn   g_quitFn = nullptr;
HWND     g_timerWindow = nullptr;
std::atomic_bool g_quitRequested{false};
std::atomic_bool g_quitIssued{false};
std::atomic<UnityQuitPhase> g_phase{UnityQuitPhase::Idle};

// 自定义消息：用于把退出请求投递回 Unity 主线程。
//
// 为什么不用 SetTimer：实测 timer 装上后回调从未触发。钩子链顶是
// GameAssembly.dll 里 IL2CPP 生成的窗口过程，WM_TIMER 在到达本层之前
// 就被吞掉了。PostMessage 走的是同一条消息路由，但自定义消息
// (WM_APP..) 不会被 Unity 或 VRify 的switch 命中。
constexpr UINT kQuitRequestMsg = WM_APP + 0x5171;

void IssueQuitOnMainThread() {
    g_phase.store(UnityQuitPhase::Resolving, std::memory_order_release);
    // 惰性解析：放在首次触发时而不是启动时，因为只有到这一刻才确认 Unity 已就绪。
    if (g_quitFn == nullptr) {
        void* appClass = ResolveIl2cppClass("UnityEngine.CoreModule.dll", "UnityEngine", "Application");
        if (appClass == nullptr) {
            // 旧版 Unity 可能在不同程序集里。
            appClass = ResolveIl2cppClass("UnityEngine.dll", "UnityEngine", "Application");
        }
        if (appClass != nullptr) {
            g_quitFn = reinterpret_cast<QuitFn>(ResolveIl2cppMethod(appClass, "Quit", 1));
        }
        GakuLoader::LogInfo("[UnityQuit] Application.Quit resolved: %p (class=%p)",
                               reinterpret_cast<void*>(g_quitFn), appClass);
    }

    if (g_quitFn == nullptr) {
        g_phase.store(UnityQuitPhase::ResolveFailed, std::memory_order_release);
        GakuLoader::LogError("[UnityQuit] cannot resolve UnityEngine.Application.Quit; "
                                "Unity will not be asked to quit (watchdog still armed)");
        return;
    }

    // Complete owned Desktop/plugin/runtime teardown before the game destroys
    // its objects. Diagnostic protection stays armed through process exit.
    g_phase.store(UnityQuitPhase::HostShutdown, std::memory_order_release);
    const auto hostStarted = GetTickCount64();
    GakuLoader::LogInfo("[UnityQuit] host teardown begin before Application.Quit");
    GakuLoader::DisableAllHooksForExit();
    GakuLoader::LogInfo("[UnityQuit] host teardown complete elapsed_ms=%llu",
        static_cast<unsigned long long>(GetTickCount64() - hostStarted));

    GakuLoader::LogInfo("[UnityQuit] calling Application.Quit(0) on the Unity main thread");
    g_phase.store(UnityQuitPhase::Calling, std::memory_order_release);
    g_quitIssued.store(true, std::memory_order_release);
    g_quitFn(0);
    g_phase.store(UnityQuitPhase::Returned, std::memory_order_release);
    GakuLoader::LogInfo("[UnityQuit] Application.Quit call returned; waiting for runtime exit");
}

} // namespace UnityQuitImpl

void RequestUnityQuit(HWND window) {
    using namespace UnityQuitImpl;
    if (g_quitRequested.exchange(true, std::memory_order_acq_rel)) return;
    g_phase.store(UnityQuitPhase::Requested, std::memory_order_release);
    g_timerWindow = window;

    if (window == nullptr || !IsWindow(window)) {
        g_phase.store(UnityQuitPhase::PostFailed, std::memory_order_release);
        GakuLoader::LogError("[UnityQuit] no valid Unity window; cannot schedule Application.Quit");
        return;
    }

    // PostMessage 而非 SetTimer：定时器消息在到达本层钩子之前被吞掉了
    // （链顶是 IL2CPP 生成的窗口过程）。自定义消息走同一条路由但不被任何
    // switch 命中，因此能可靠回到这里—— 且仍运行在 Unity 主线程。
    if (PostMessageW(window, kQuitRequestMsg, 0, 0) == FALSE) {
        g_phase.store(UnityQuitPhase::PostFailed, std::memory_order_release);
        GakuLoader::LogError("[UnityQuit] PostMessage(0x%X) failed error=%lu; "
                                "Application.Quit not scheduled",
                                kQuitRequestMsg, GetLastError());
        return;
    }
    GakuLoader::LogInfo("[UnityQuit] exit requested via PostMessage(0x%X) on hwnd=0x%p",
                           kQuitRequestMsg, static_cast<void*>(window));
}


// 供 QuitGuard 的窗口过程调用：处理本层投递的退出请求。
// 必须在 GakuLoader 命名空间内可见 —— 头文件声明的是无命名空间前缀的版本。
bool HandleUnityQuitMessage(HWND hWnd, UINT uMsg) {
    if (uMsg != UnityQuitImpl::kQuitRequestMsg) return false;
    UnityQuitImpl::IssueQuitOnMainThread();
    return true;
}

bool UnityQuitIssued() {
    return UnityQuitImpl::g_quitIssued.load(std::memory_order_acquire);
}
UnityQuitPhase GetUnityQuitPhase() { return UnityQuitImpl::g_phase.load(std::memory_order_acquire); }

} // namespace GakuLoader
