# Troubleshooting

本页说明 GakuLoader 常见的运行时问题、排查方式与退出看门狗机制。

---

## 1. 怎么看日志

- **日志文件**：默认输出在 `<游戏目录>/gakumas-local/gakuloader.log`。包含宿主和所有插件的合并输出。
- **开黑窗口看实时日志**：在 `gakuloader.ini` 中加上：
  ```ini
  [host]
  console=true
  ```
  启动游戏时会弹出一个名为 `GakuLoader` 的独立控制台黑窗口，实时刷出运行日志。
- **看崩溃日志**：在 `gakuloader.ini` 中加上 `[diagnostics] trace=true`。如果发生内存访问违规等崩溃，会在 `gakumas-local/crash.log` 记录崩溃时的线程与寄存器状态。

---

## 2. 插件没有加载

1. **检查导出函数**：用 Dumpbin 或 Dependencies 检查插件 DLL 是否导出了 `GakuPlugin_Query`。
2. **检查 ini 配置**：检查 `gakuloader.ini` 的 `[plugins]` 段是不是把这个 DLL 设成了 `false`。
3. **是不是旧版接口**：如果日志里出现 `Rejected ... no GakuPlugin_Query export. Rebuild it against GakuPlugin.h`，说明这个 DLL 使用了已经废弃的旧版接口。旧接口已经彻底删除，必须拿当前的 `include/GakuPlugin.h` 重新编译。
4. **ID 重复**：如果两个插件的描述符 `id` 相同，后加载的会被跳过。

---

## 3. 按快捷键没有反应

1. **检查是不是冲突了**：按 F1 打开 GakuLoader 控制窗口，看 Hotkeys 表格里该按键的状态是不是标了 `conflict`。如果是，说明有两个插件绑了同一个键，双方都被停用了。在 Manager → Hotkeys 给其中一个换个按键即可立即恢复。旧 `pluginId.actionId` 配置若匹配多个动作会被忽略并记入日志，此时也可在 Manager 分别设置，详见 [热键配置](Configuration.md#hotkeys-段)。
2. **焦点在不在游戏上**：GakuLoader 不监听系统全局按键。如果焦点切到了浏览器或其他软件，按键不会触发。
3. **是不是在打字**：如果在控制窗口的 ImGui 文本框里点过了鼠标，普通字母键会被拦截防误触。

---

## 4. 游戏启动时控制窗口自动弹出抢焦点

这是目前版本的默认行为：控制窗口建好之后调了 `SW_SHOW`，所以启动时会自动弹出并短暂拿到前台焦点。按一次 **F1** 或点右上角关闭按钮收进托盘即可。

---

## 5. 关游戏时总是等 5 秒才完全退出

这是 Unity 6 PC 客户端在这个环境下的毛病：引擎如果直接处理系统发来的 `WM_CLOSE` 窗口关闭消息，原生窗口过程会在内部卡死（CPU 彻底归零，整个进程静止不动）。

QuitGuard 的处理流程是：
1. 拦截 `WM_CLOSE` 消息，不让它传给 Unity 原生窗口过程。
2. 异步在 Unity 主线程调用 `UnityEngine.Application.Quit(0)` 让引擎自己正常收尾。
3. 如果 5 秒后游戏进程还没退出，看门狗会直接强制终止进程，防止后台留下僵尸进程。

### 怎么确认退出卡住是不是插件 Hook 引起的？
在 `gakumas-local/` 目录下放一个名为 `gml_disable_hooks.txt` 的空文本文件。加载器启动时会跳过所有 MinHook 的安装，只保留 QuitGuard。如果此时关游戏依然卡住等 5 秒强杀，就证明卡住完全是 Unity 引擎自己的问题，和插件的 Hook 没关系。

---

[Wiki 首页](Home.md) · [全部章节](_Sidebar.md) · [项目首页](../README.md)
