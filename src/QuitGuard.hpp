#pragma once
#include <windows.h>

namespace GakuLoader {

// 启动退出兜底的常驻轮询线程。
//
// 线程只调用 user32（查找 Unity 窗口 + 替换其 WndProc），完全不触碰 IL2CPP。
// 退出路径独立于插件 detour，因此在 GML 独立运行或 Localify 接管插件时都可用。
void StartQuitGuard();

// 还原 WndProc 并停止兜底。在 DLL 卸载时调用。
void StopQuitGuard();

} // namespace GakuLoader
