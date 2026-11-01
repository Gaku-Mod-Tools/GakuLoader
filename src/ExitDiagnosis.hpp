#pragma once

namespace GakuLoader {

// 是否已通过环境变量 GML_DISABLE_HOOKS=1 禁用全部 MinHook。
// 用于二分定位"Application.Quit 送达但 Unity 不退出"的阻塞源。
bool HooksDisabledForDiagnosis();

// 打印当前模式，便于从日志确认实验条件是否生效。
void LogExitDiagnosisMode();

} // namespace GakuLoader
