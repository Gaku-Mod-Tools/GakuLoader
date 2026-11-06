# 快捷键系统

本页说明 GakuLoader 的统一物理热键服务、前台焦点保护机制、按键冲突仲裁与配置重映射规则。

---

## 热键工作原理

插件通过 `gakuloader.hotkeys` 服务注册与注销快捷键：

```cpp
GakuHotkeyService hotkeys{};
host->queryService(host->hostContext, GAKU_SERVICE_HOTKEYS, GAKU_SERVICE_VERSION, sizeof(hotkeys), &hotkeys);

GakuHotkeyBinding binding{};
binding.abiVersion = GAKU_ABI_VERSION;
binding.structSize = sizeof(binding);
binding.actionId = "show-gui";                     // 插件内稳定的动作标识符
binding.displayName = "打开界面";                  // 在 F1 界面与诊断中显示的友好名称
binding.virtualKey = 'M';                          // Win32 虚拟键码
binding.modifiers = GAKU_HOTKEY_MOD_CTRL | GAKU_HOTKEY_MOD_SHIFT;
binding.callback = [](void* userData) {
    // 回调安全运行在 Unity 主线程
};
binding.userData = state;

hotkeys.registerHotkey(hotkeys.context, &binding);
```

### 核心特性
- **专用后台线程检测**：采用后台专用轮询线程，以 12 毫秒为周期检测物理按键状态。
- **单次边沿触发**：仅在按键按下的瞬间触发一次派发，长按不会造成连续高频重发。
- **安全派发至 Unity 主线程**：回调函数由 Unity 主线程在每帧 `onTick` 推进前统一调度执行，可安全调用游戏逻辑与托管对象。
- **非系统全局抢占**：加载器不使用 Win32 全局热键（`RegisterHotKey`），切换到浏览器或其他外部应用时不会截获按键，不影响外部软件正常使用。

---

## 前台焦点与打字保护

为了避免在不恰当的时机误触发快捷键，热键仅在同时满足以下条件时才会派发：

1. 当前前台活动窗口为游戏主窗口，或者 GakuLoader 的独立控制窗口。
2. 进程内没有正在弹出的系统原生模态文件选择对话框。
3. 用户当前没有在 GakuLoader 控制窗口的 ImGui 文本输入框中键入文字。

---

## 冲突仲裁与双向失效

若两个插件的动作注册了**完全相同**的快捷键组合：

- **双向失效保护**：冲突的两个动作均**不会被触发**。加载器坚决不执行重复派发，也不会随机选择一方执行。
- **控制台与日志告警**：宿主会在日志中记录冲突详情：
  ```text
  [Hotkeys] conflict: pluginA.action1 (Ctrl+M), pluginB.action2 (Ctrl+M); conflicting bindings do not dispatch.
  ```
- **可视化状态呈现**：按 **F1** 打开控制窗口，在 Hotkeys 页签表格中，冲突项的状态会明确显示为 `conflict`。
- **即时解除冲突**：在 F1 界面中为其中一个动作重新指定其他快捷键，冲突解除后即可立即恢复正常触发，无需重启游戏。

---

## 快捷键重映射与配置建议

### 推荐：在 F1 界面中可视化修改
按 **F1** 打开控制窗口，进入 **Hotkeys** 页签：
- 点击对应动作的组合键按钮，直接在弹窗中按下新的按键组合即可完成捕获。
- 修改立即生效并自动保存到 `gakuloader.ini` 中，无需重启游戏。
- 支持点击“解除绑定”（保存为空值）或“恢复默认”（跟随插件声明的默认按键）。

### 手工编辑 `gakuloader.ini`
`GakuLoader/Configs/gakuloader.ini` 包含 `[hotkeys]` 段。手工修改配置后需重启游戏生效：

```ini
[hotkeys]
; 兼容旧格式：<pluginId>.<actionId>=<按键组合>（仅在全进程唯一匹配时生效）
gakumotion.show-gui=Ctrl+Shift+M

; 等号右侧留空表示完全解绑该动作快捷键
example.action=
```

- **保留按键**：**F1 键被加载器保留用于呼出/隐藏控制面板，插件注册 F1 会被直接拒绝**。
- **按键名称规范**：支持 `Ctrl`、`Shift`、`Alt`、`Win` 修饰符（用 `+` 连接）；普通字母 `A-Z`、数字 `0-9`、功能键 `F1-F24`、控制键 `Space`, `Tab`, `Enter`, `Esc`, `Delete`, 方向键等及常见标点符号。完整按键清单参见 [配置文件](Configuration.md#hotkeys-段)。

---

[Wiki 首页](Home.md) · [全部章节](_Sidebar.md) · [项目首页](../README.md)
