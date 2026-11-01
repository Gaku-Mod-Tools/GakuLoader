#pragma once
#include <windows.h>

namespace GakuLoader {

// installHooks=false keeps IL2CPP method resolution available to UnityQuit but
// leaves runtime detours to an already active external plugin host (Localify).
void StartHookThread(bool installHooks = true);
void ShutdownHooks();

// QuitGuard forwards this private wake message before normal window dispatch.
// The worker only posts a wake; all Unity API reads run on the window's thread.
bool HandleFramePumpMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

// Shutdown plugins on main thread, then disable GML-owned leases only. The
// legacy name remains for QuitGuard source compatibility. No global MinHook
// shutdown and no foreign host hooks are touched.
void DisableAllHooksForExit();

// IL2CPP 解析入口，供 UnityQuit 解析 UnityEngine.Application.Quit。
// 纯查表，可在任意线程调用；实际调用需在 Unity 主线程上下文。
void* ResolveIl2cppClass(const char* assembly, const char* namespaze, const char* name);
void* ResolveIl2cppMethod(void* klass, const char* methodName, int argsCount);

} // namespace GakuLoader
