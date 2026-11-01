// =============================================================================
// ExitDiagnosis — 退出阻塞源二分实验
// =============================================================================
// 目的：判定Application.Quit 之后 Unity 不unwind 的阻塞源。
//
// 已知事实（实机）：
//   - 无 VRify、仅 ModLoader + 插件时，链路依旧完整：
//     WM_CLOSE swallowed -> PostMessage -> Application.Quit(0) 送达主线程
//   - 但窗口 1 秒内不销毁，运行时不解冻，最终强杀
//   - 此时钩子链只有一层：current GWLP_WNDPROC = VERSION.dll，
//     转发目标 = UnityPlayer.dll
//
// 两个候选：
//   H: MinHook detour在 Unity 销毁对象期间访问了已失效的对象
//   W:与 hook 无关，是 Unity 退出流程本身在本环境下不收敛
//
// 判据：关掉全部 MinHook（保留 QuitGuard 的 WndProc 与 Application.Quit 注入）。
//   若 Unity 优雅退出 => H，阻塞源是 detour。
//   若仍强杀      => W，与 hook 无关。
//
// 开关通过环境变量 GML_DISABLE_HOOKS=1 读取，改代码无需重新编译。
// =============================================================================

#include "ExitDiagnosis.hpp"
#include "Host.hpp"

#include <windows.h>
#include <cstdlib>
#include <cstring>
#include <cwchar>

namespace GakuLoader {
namespace ExitDiagnosis {

// 两种开关都支持，优先用文件：
//
//   环境变量不可靠 —— 游戏由 DMM 启动器拉起，那是另一个进程，环境变量不会
//   从本会话继承过去（实测 GML_DISABLE_HOOKS=1 无效，hook 照常安装）。
//   标记文件则由游戏进程自己读，不依赖继承。
//
//   1. 文件：游戏根目录下 gakumas-local/ 里的 gml_disable_hooks.txt
//   2. 环境变量：仅当直接从命令行启动游戏时有效
bool HooksDisabled() {
    static const bool disabled = []() {
        wchar_t path[MAX_PATH]{};
        const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return false;
        wchar_t* slash = wcsrchr(path, L'\\');
        if (slash == nullptr) return false;
        *slash = L'\0';

        // <game>\gakumas-local\gml_disable_hooks.txt
        wcscat_s(path, MAX_PATH, L"\\gakumas-local\\gml_disable_hooks.txt");
        if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
            return true;
        }

        char buf[8]{};
        const DWORD m = GetEnvironmentVariableA("GML_DISABLE_HOOKS", buf, sizeof(buf));
        return m > 0 && buf[0] == '1';
    }();
    return disabled;
}

} // namespace ExitDiagnosis

bool HooksDisabledForDiagnosis() {
    return ExitDiagnosis::HooksDisabled();
}

void LogExitDiagnosisMode() {
    if (ExitDiagnosis::HooksDisabled()) {
        GakuLoader::LogInfo("[ExitDiag] *** GML_DISABLE_HOOKS=1 : all MinHook hooks are OFF ***");
        GakuLoader::LogInfo("[ExitDiag] QuitGuard window hook and Application.Quit injection stay active.");
        GakuLoader::LogInfo("[ExitDiag] If Unity still fails to exit, the blocker is NOT the detours.");
    } else {
        GakuLoader::LogInfo("[ExitDiag] hooks active (normal mode). Set GML_DISABLE_HOOKS=1 to bisect.");
    }
}

} // namespace GakuLoader
