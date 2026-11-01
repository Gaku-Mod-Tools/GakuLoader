# Hotkeys

本页说明 GakuLoader 的统一热键服务、冲突处理与 ini 重映射。

---

## 热键怎么工作

插件通过 `gakuloader.hotkeys` 注册热键：

```cpp
GakuHotkeyService hotkeys{};
host->queryService(host->hostContext, GAKU_SERVICE_HOTKEYS, GAKU_SERVICE_VERSION, sizeof(hotkeys), &hotkeys);

GakuHotkeyBinding binding{};
binding.abiVersion = GAKU_ABI_VERSION;
binding.structSize = sizeof(binding);
binding.actionId = "show-gui";
binding.displayName = "打开界面";
binding.virtualKey = 'M';
binding.modifiers = GAKU_HOTKEY_MOD_CTRL | GAKU_HOTKEY_MOD_SHIFT;
binding.callback = [](void* userData) {
    // 运行在 Unity 主线程
};
binding.userData = state;

hotkeys.registerHotkey(hotkeys.context, &binding);
```

- **专用线程检测**：后台专用线程以 12 毫秒间隔轮询物理按键。
- **单次边沿触发**：只有物理按键按下的那一瞬间触发一次，长按不会连续高频触发。
- **分发到 Unity 主线程**：回调函数是在 Unity 引擎的主线程里跑的（在每帧 `onTick` 前统一派发），你可以安全操作游戏对象。
- **不会截获全局按键**：加载器不使用 Win32 全局热键（`RegisterHotKey`），切换到浏览器或其他软件时，游戏热键不会被触发。

---

## 焦点与打字保护

热键只有在以下条件满足时才会分发：
1. 当前前台激活的窗口是游戏主窗口，或者 GakuLoader 的控制窗口。
2. 没有弹出发起在同进程的原生文件选择窗口。
3. 当前没有在 GakuLoader 控制窗口的文本输入框里打字。

---

## 冲突处理与双向失效

如果插件 A 和插件 B 注册了**完全相同**的快捷键组合：
- **两个插件都不会被触发**。加载器坚决不执行双发，也不会随机挑一个执行。
- 宿主会在控制台打出警告，告诉你哪个键冲突了：
  ```text
  [Hotkeys] conflict: pluginA.action1 (Ctrl+M), pluginB.action2 (Ctrl+M); conflicting bindings do not dispatch.
  ```
- 按 F1 打开控制窗口，在 Hotkeys 表格里这两项的状态会显示为 `conflict`。
- 解决办法很简单：在 `gakuloader.ini` 里面给其中一个换个按键，冲突解除后即可正常使用。

---

## 在 `gakuloader.ini` 里改热键

玩家不需要修改插件源码，在游戏根目录的 `gakuloader.ini` 就可以给任何插件换按键：

```ini
[hotkeys]
; 格式：<pluginId>.<actionId>=<新按键>
gakumotion.show-gui=Ctrl+Shift+F11

; 留空表示完全解绑该功能
example.action=
```

> **注意**：F1 键被加载器保留用于切换控制窗口，任何插件注册 F1 都会被直接拒绝。
