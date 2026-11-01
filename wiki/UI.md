# UI

本页说明 GakuLoader 的独立控制窗口与 ImGui 绘制方法。

---

## 独立伴随窗口

与很多在游戏画面上直接叠加绘制（Hook `IDXGISwapChain::Present`）的加载器不同，GakuLoader 采用**完全独立的 Win32 + Direct3D 11 伴随窗口**：

- 它运行在独立的线程上，有自己的 `HWND` 和 D3D11 交换链，硬件受限时自动回退到微软 WARP 软件光栅化驱动。
- 它完全不侵入游戏自己的 D3D12 渲染链，游戏切分辨率或全屏切换不会导致控制窗口崩溃。
- 启动时自动从系统目录加载微软雅黑字体（`msyh.ttc`，18px），插件不需要自己打包字体文件。

---

## 窗口操作与 F1 键

- 按 **F1** 可以在“打开控制窗口”与“隐藏/最小化”之间切换。F1 走的是 UI 线程内部消息，不占用游戏主线程帧。
- 点击控制窗口右上角的关闭按钮（`X`）或者在菜单栏选 `File -> Hide controls`，窗口会自动收缩到系统托盘。
- 托盘图标左键点击可以重新唤出窗口，右键可以弹出菜单。

---

## 怎么在 `onDesktopUi` 里画自己的面板

当用户在控制窗口左侧导航栏点击你的插件时，宿主会在每次 UI 渲染时调用插件的 `onDesktopUi` 回调。

```cpp
void OnDesktopUi(void* pluginState) {
    // 1. 声明并填写你工程编译使用的 ImGui 指纹结构
    GakuImGuiAbi abi{
        IMGUI_VERSION_NUM,
        sizeof(ImGuiIO),
        sizeof(ImGuiStyle),
        sizeof(ImDrawVert),
        sizeof(ImDrawIdx),
        sizeof(ImWchar)
    };

    // 2. 绑定到宿主
    GakuDesktopUiBinding binding{};
    if (desktop.bind(&abi, sizeof(binding), &binding) != GAKU_OK) return;

    // 3. 将你的 ImGui 模块指向宿主的上下文与分配器
    ImGui::SetAllocatorFunctions(binding.alloc, binding.free, binding.allocatorUserData);
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(binding.imguiContext));

    // 4. 直接调用原生 ImGui 控件绘制你的内容
    ImGui::TextUnformatted("我的插件配置");
    ImGui::Separator();
    static bool enabled = true;
    ImGui::Checkbox("启用特定功能", &enabled);
}
```

### 几个切忌事项
- **不要自己建上下文或管生命周期**：在 `onDesktopUi` 内部，宿主已经替你调过 `NewFrame()`，并且已经把内容子区域切好了。**绝对不要**在回调里调用 `CreateContext`、`DestroyContext`、`NewFrame`、`Render` 或 `Present`。
- **不要在 UI 线程里卡着等游戏主线程**：UI 回调是同步在 UI 线程上画的。如果有配置变更要通知游戏，用 `gakuloader.main-thread` 的 `enqueue` 投递过去，不要在这里同步等待返回。
- **慢面板检测**：如果你的单次回调耗时超过 16 毫秒（相当于 60 帧掉帧），宿主会在日志里打警告：`plugin panel callback <id> blocked the shell for xx ms`。

---

## 文本框防误触

当你在控制窗口里的 ImGui 文本框里点鼠标打字时，加载器的热键轮询线程会自动暂停普通字母键和符号键的分发。防止你在输入框里敲 `W`、`A`、`S`、`D` 或快捷键字母时，误触游戏里的 Mod 动作。打完字点一下其他空白处失去焦点即可恢复。
