# 桌面界面与 UI 规范

本页说明 GakuLoader 独立桌面控制窗口的工作机制、F1 交互、ImGui 面板绘制规范以及文件拖放集成方法。

---

## 独立伴随窗口架构

与常见的直接在游戏画面上 Hook `Present` 叠加绘制界面的方式不同，GakuLoader 采用完全独立的 **Win32 + Direct3D 11 伴随窗口**：

- **独立渲染线程**：控制窗口运行在专属的 UI 线程上，拥有独立的 Win32 消息循环和 Direct3D 11 交换链。在显卡硬件受限环境下，会自动回退至微软 WARP 软件光栅化驱动。
- **不侵入游戏管线**：窗口与游戏的 Direct3D 12 渲染链完全解耦。游戏分辨率变化、全屏与窗口化切换或游戏画面暂停均不会影响控制窗口的稳定运行。
- **高质量字体栅格化**：启动时自动从系统目录加载中日韩字体（优先选择微软雅黑 `msyh.ttc`），按照 `18px × UI Scale × DPI Scale` 的物理像素尺寸栅格化字库。字库单贴图上限为 54px、256 MiB RGBA，超限时自动回落；未找到 CJK 字体时回退到内建 Latin 字体。

---

## 窗口交互与 F1 键

- 按 **F1** 可以在“显示控制窗口”与“隐藏窗口”之间自由切换。F1 键响应走内部消息派发，不占用游戏主线程帧时间。
- 点击控制窗口右上角的关闭按钮（`X`）或在菜单栏选择 `File -> Hide controls` 时，窗口会自动隐藏并最小化至 Windows 系统任务栏托盘区域。
- 单击托盘图标可重新唤出控制窗口，右键托盘图标可弹出退出与操作菜单。

---

## Manager 四大页签与职能边界

控制窗口内建四个全局管理页签：

1. **Plugin List**：展示当前已扫描到的所有插件 DLL、名称、版本、作者及状态指示灯（就绪、加载中旋转动画、错误提示）。
2. **Appearance**：提供 5 套内建色彩主题切换、UI 缩放比例滑块（50% 到 300%）与 4 种界面语言选择。
   > **职能边界说明**：Manager 的语言切换会通过 `gakuloader.language` 服务向支持该特性的插件同步语言偏好。此设置仅改变加载器与适配插件的控制面板文字；游戏内部的卡面与剧情汉化需由专门的汉化插件实现。
3. **Hotkeys**：列出当前所有动作快捷键绑定与冲突状态，支持免重启的可视化按键捕获、修改、解绑与恢复默认。
4. **Settings**：提供实时控制台日志黑窗口开关、崩溃追踪开关，以及各个插件 DLL 的独立启闭选项。

---

## 在 `onDesktopUi` 中绘制插件面板

当用户在控制窗口左侧导航栏中选中你的插件时，宿主会在每次 UI 渲染帧中调用该插件的 `onDesktopUi` 回调：

```cpp
void OnDesktopUi(void* pluginState) {
    // 1. 声明并填写插件编译时使用的 ImGui ABI 指纹
    GakuImGuiAbi abi{
        IMGUI_VERSION_NUM,
        sizeof(ImGuiIO),
        sizeof(ImGuiStyle),
        sizeof(ImDrawVert),
        sizeof(ImDrawIdx),
        sizeof(ImWchar)
    };

    // 2. 向宿主发起桌面 UI 绑定
    GakuDesktopUiBinding binding{};
    if (desktop.bind(&abi, sizeof(binding), &binding) != GAKU_OK) return;

    // 3. 将当前模块的 ImGui 上下文与内存分配器指向宿主
    ImGui::SetAllocatorFunctions(binding.alloc, binding.free, binding.allocatorUserData);
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(binding.imguiContext));

    // 4. 调用原生 ImGui 控件绘制面板内容
    ImGui::TextUnformatted("我的插件面板");
    ImGui::Separator();
    static bool featureEnabled = true;
    ImGui::Checkbox("启用特定功能", &featureEnabled);
}
```

### 绘制规范与注意事项
- **不要自行管理上下文**：宿主在调用 `onDesktopUi` 之前已为你调用了 `NewFrame()` 并规划好绘制子窗口。严禁在回调内部调用 `CreateContext`、`DestroyContext`、`NewFrame`、`Render` 或 `Present`。
- **避免阻塞主线程**：UI 回调在独立的 UI 线程中同步执行。若面板上的操作需要通知游戏执行，请通过 `gakuloader.main-thread` 的 `enqueue` 异步投递，不要在 UI 线程同步等待。
- **慢面板检测**：若单次 UI 绘制回调耗时超过 16 毫秒，宿主会在日志中记录警告：`plugin panel callback <id> blocked the shell for xx ms`。

---

## 文件拖放集成 (`gakuloader.file-drop`)

插件面板支持直接接收外部拖入的文件（如将动作文件或模型文件直接拖入面板加载）：

1. 在 `onDesktopUi` 回调中，调用 `fileDrop.setTarget` 传入当前 ImGui 视口中的接收矩形坐标（`left`, `top`, `right`, `bottom`）。
2. 调用 `fileDrop.poll` 检查并提取用户拖入的文件路径列表。单次最大支持接收 256 个文件、1MB 路径总长度。

---

## 输入框焦点与按键防误触

当用户在控制窗口的 ImGui 文本输入框中点击打字时，宿主的热键轮询系统会自动置位打字保护标志，暂停分发字母键与标点符号键。这样可以避免在输入文字时意外触发游戏内的 Mod 功能快捷键。光标离开输入框或失焦后自动恢复正常分发。

---

[Wiki 首页](Home.md) · [全部章节](_Sidebar.md) · [项目首页](../README.md)
