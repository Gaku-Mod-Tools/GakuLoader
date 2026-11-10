// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

#pragma once
#include <windows.h>

namespace GakuLoader {

// 请求 Unity 优雅退出：向窗口投递一条自定义消息，由窗口过程在 Unity 主线程
// 上下文里调用 Application.Quit(0)。
//
// 必须在吞掉 WM_CLOSE 之后调用——Unity 的原生窗口过程处理 WM_CLOSE 时会同步
// 卡死并永不返回，所以那条路径已被 QuitGuard 切断。
// 不用 SetTimer：定时器消息在到达本层钩子前会被 IL2CPP 的窗口过程吞掉。
void RequestUnityQuit(HWND window);

// 供本层窗口过程识别并处理退出请求消息。返回 true 表示已消费。
// 单独暴露是因为投递用的是 PostMessage，必须由接收侧主动分派。
bool HandleUnityQuitMessage(HWND hWnd, UINT uMsg);

// 是否已经成功调用过 Application.Quit。用于日志与排查。
bool UnityQuitIssued();
enum class UnityQuitPhase {
    Idle, Requested, Resolving, ResolveFailed, HostShutdown, Calling, Returned, PostFailed
};
// Atomic, non-blocking state for the exit watchdog; distinguishes a successful
// resolve from pending host teardown and from the actual game Quit invocation.
UnityQuitPhase GetUnityQuitPhase();

} // namespace GakuLoader
