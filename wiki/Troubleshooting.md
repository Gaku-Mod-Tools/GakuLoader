# 常见问题与排查

本页说明 GakuLoader 常见的运行时问题、排查方式与 QuitGuard 退出看门狗机制。

---

## 1. 怎么查看运行日志

- **常规日志文件**：默认输出在 `<游戏根目录>/GakuLoader/Logs/gakuloader.log`。该文件包含了宿主加载过程以及所有插件的合并日志输出。
- **实时控制台黑窗口**：在 `gakuloader.ini` 中添加以下配置，启动游戏时会弹出一个名为 `GakuLoader` 的独立黑窗口，实时输出每条日志：
  ```ini
  [host]
  console=true
  ```
- **崩溃转储与追踪**：在 `gakuloader.ini` 中配置 `[diagnostics] trace=true`。若程序遭遇未处理异常或内存访问违规，会在 `GakuLoader/Logs/crash.log` 记录崩溃时刻的线程调用堆栈与寄存器上下文。

---

## 2. 插件未能正常加载

1. **检查导出函数**：使用 Dumpbin 或 Dependencies 工具检查插件 DLL 是否导出了 `GakuPlugin_Query` 符号。
2. **检查配置文件**：检查 `gakuloader.ini` 的 `[plugins]` 段，确认没有将该插件设为 `false`、`0` 或 `no`。
3. **接口版本是否过旧**：如果日志中出现 `Rejected ... no GakuPlugin_Query export`，说明该插件基于已废弃的旧版接口编写。旧接口已被彻底移除，需基于当前仓库的 `include/GakuPlugin.h` 重新编译。
4. **插件 ID 重复**：宿主根据插件描述符中的 `id` 甄别唯一性。若两个 DLL 声明了相同的 `id`，后加载的插件会被直接跳过。

---

## 3. 按快捷键没有响应

1. **检查按键冲突**：按 **F1** 打开控制窗口，在 **Hotkeys** 页签查看该按键状态是否标为 `conflict`。若发生冲突，双方均会停止派发，在面板中直接为其中一方修改快捷键即可恢复。
2. **确认窗口焦点**：GakuLoader 不监听系统全局按键。只有当游戏主窗口或控制面板处于前台活动状态时，热键才会生效。
3. **文本框打字保护**：若曾在控制窗口的 ImGui 文本输入框中点击输入，系统会暂停普通字母按键分发以防误触。在空白处点击使其失去焦点即可恢复。详细配置语法参见 [配置文件](Configuration.md#hotkeys-段)。

---

## 4. 启动时控制窗口自动弹出

控制窗口初始化完成后调用了 `SW_SHOW`，因此启动游戏时会自动弹出并获得前台焦点。按一次 **F1** 键或点击右上角的关闭按钮即可将其隐藏并最小化至系统托盘。

---

## 5. 游戏退出机制与看门狗设计 (QuitGuard)

### 背景与退出死锁原因
在 Unity 6 PC 客户端运行环境中，若游戏主窗口直接处理由操作系统分发的 `WM_CLOSE` 窗口关闭消息，Unity 的原生窗口过程会在内部同步死锁（CPU 占用归零，主线程静止不退出）。

### QuitGuard 处理流程
为了保证游戏正常平稳退出，GakuLoader 实现了 QuitGuard 机制：
1. 拦截游戏主窗口的 `WM_CLOSE` 消息，不让其传递给 Unity 的原生窗口过程。
2. 向 Unity 主线程投递消息，安全调用 `UnityEngine.Application.Quit(0)`，让引擎正常触发各模块的退出收尾流程。

### ExitWatchdog 多阶段时间预算
看门狗采用细分的多阶段时间预算，而不是简单的固定延时：
- **Teardown 阶段预算（15 秒，`kTeardownBudgetMs = 15000`）**：为桌面 UI 渲染注销、各插件的清理（Teardown）以及后台工作线程回收提供充裕的安全缓冲。
- **运行时退出预算（8 秒，`kPostQuitBudgetMs = 8000`）**：从 `Application.Quit()` 执行完毕返回的时刻开始计时，为 Unity 引擎运行时自我退出留出 8 秒等待时间。若在此期间游戏窗口已自然销毁，看门狗判定为正常退出并自行撤销。
- **绝对安全硬上限（40 秒，`kCeilingMs = 40000`）**：若超过 40 秒进程仍未能自行退出，看门狗执行终极强制终止，确保后台不残留僵死进程。退出前还会自动清理孤儿进程 `UnityCrashHandler64.exe`。

### 退出卡死二分排查法
若怀疑退出缓慢或卡死是由某个插件的 Hook 引起，可在 `GakuLoader/` 目录下放置一个名为 `gml_disable_hooks.txt` 的空文本文件（或设置系统环境变量 `GML_DISABLE_HOOKS=1`）。宿主在启动时会跳过所有业务 MinHook 的挂接，仅保留 QuitGuard 退出保护。若此时退出依然卡顿，可断定卡顿源于 Unity 引擎或系统底层；若恢复迅速退出，则可进一步排查具体插件的 Detour 逻辑。

---

[Wiki 首页](Home.md) · [全部章节](_Sidebar.md) · [项目首页](../README.md)
