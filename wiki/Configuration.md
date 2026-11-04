# 配置文件

`gakuloader.ini` 位于游戏根目录下，用于设置界面外观、插件启闭、热键绑定与调试选项。所有段落与键值均为可选，文件不存在时加载器完全按默认值运行。

---

## 完整配置示例

```ini
; gakuloader.ini - GakuLoader 配置文件

[appearance]
; 界面主题：Dark | Sapphire | PaperAndInk | AppleLight | AppleDark（默认 Dark）
theme=Dark
; 界面缩放比例：百分比整数，范围 50 到 300（默认 100，即 1.0x 原始缩放）
scale=100
; 界面语言：en | zh-Hans | zh-Hant | ja（默认自动匹配系统区域语言，回落为 en）
language=zh-Hans

[plugins]
; 禁用指定插件。文件名不区分大小写。
; 设为 false、0 或 no 会在 LoadLibrary 之前跳过加载，文件仍保留在磁盘上。
SomePlugin.dll=false

[hotkeys]
; 推荐在 F1 窗口的可视化 Hotkeys 面板中修改，系统会自动保存为高精度键名：
; v2:<pluginId十六进制>:<actionId十六进制>=<组合键> 或 <default>
; 同时也兼容旧版直接指定名称格式（仅在全进程唯一匹配时生效）：
gakumotion.show-gui=Ctrl+Shift+M
; 等号右边留空表示完全解绑该快捷键：
example.action=

[host]
; 设为 true 会在启动时调用 AllocConsole 打开控制台黑窗口，实时打印运行日志（默认 false）
console=false

[diagnostics]
; 设为 true 会开启崩溃追踪，并在发生崩溃时生成 gakumas-local/crash.log（默认 false）
trace=false
```

修改配置文件后，需要重启游戏才能生效。若在 F1 界面中调整外观或热键，相关设置会立即生效并自动保存到此文件中。

---

## 布尔值与数值解析规则

- **布尔值**：`true`、`1`、`yes` 解析为真；`false`、`0`、`no` 解析为假（不区分大小写）。
- **缺省与错误容忍**：未填或无法识别的值保持默认行为，不会因拼写疏漏误关插件。

---

## 各配置段说明

### `[appearance]` 段

本段控制 F1 桌面控制窗口的视觉外观与语言偏好：

- `theme`：可选以下 5 套预设主题（不区分大小写）：
  - `Dark`（默认）：经典深色主题。
  - `Sapphire`：蓝宝石暗调主题。
  - `PaperAndInk`：纸墨明亮浅色主题。
  - `AppleLight`：清爽浅色主题。
  - `AppleDark`：深空灰暗色主题。
- `scale`：整型百分比（有效范围 `50` 到 `300`，默认 `100` 即 1.0x）。用于根据屏幕 DPI 与个人习惯调整文字与控件大小。系统会根据该比例自动栅格化矢量字体。
- `language`：界面语言代码（可选 `en`、`zh-Hans`、`zh-Hant`、`ja`）。
  > **注意**：此项用于设置 GakuLoader 控制面板以及调用了 `gakuloader.language` 服务的插件 UI 语言。游戏内卡面、剧情与界面的文本汉化属于汉化插件的独立职能，不受此项控制。

### `[plugins]` 段

本段用于单独禁用不需要加载的插件：

- 检查在 `LoadLibraryExW` 之前执行。被禁用的 DLL 不会被载入进程内存，其 `DllMain` 也不会执行。
- 键名为插件文件名（如 `GakuMotion.dll`），不区分大小写。

### `[hotkeys]` 段

本段用于管理插件快捷键绑定。整段最大长度为 32768 个字符：

- **推荐修改方式**：按 **F1** 打开控制窗口，在 **Hotkeys** 页签中直接点击组合键或“修改”按钮进行可视化录入。界面修改立即生效，并自动按高精度格式保存回 INI 文件。
- **高精度格式**：`v2:<pluginId十六进制>:<actionId十六进制>=<按键组合>`。由 F1 界面自动维护，区分原始标识符大小写与点号。等号后为 `<default>` 时表示跟随插件升级后的默认按键。
- **旧版格式兼容**：`<pluginId>.<actionId>=<按键组合>`。不区分大小写，但仅在当前注册的动作中唯一匹配时生效；若存在歧义则忽略该项并在日志中提示，避免误绑定。
- **按键解绑**：等号右侧留空即表示彻底解除绑定。
- **支持的按键语法**：
  - 修饰符（用 `+` 连接，不区分大小写）：`Ctrl`, `Shift`, `Alt`, `Win`
  - 字母与数字：`A` - `Z`, `0` - `9`
  - 功能键：`F1` - `F24`
  - 常用控制键：`Space`, `Tab`, `Enter` / `Return`, `Esc` / `Escape`, `Backspace`, `Insert`, `Delete` / `Del`, `Home`, `End`, `PageUp`, `PageDown`, `Up`, `Down`, `Left`, `Right`
  - 符号键：`Comma`, `Period`, `Minus`, `Plus`, `Equal`, `Slash`, `Backslash`, `Semicolon`, `Quote`, `LeftBracket`, `RightBracket`, `Grave`
- **保留按键**：**F1 键被加载器保留用于呼出/隐藏控制面板，插件不能注册 F1**。
- **冲突处理**：两个插件注册相同按键时，双方均不会触发，并在界面中标记为 `conflict`。在 F1 界面重新改键即可恢复。

### `[host]` 与 `[diagnostics]` 段

- `[host] console`：设为 `true` 时，会在游戏启动时通过 `AllocConsole()` 弹出一个名为 `GakuLoader` 的黑窗口，实时输出加载器与各插件的合并日志，便于即时调试。
- `[diagnostics] trace`：设为 `true` 时开启追踪记录。若运行中发生崩溃，会在 `gakumas-local/crash.log` 记录异常发生时的线程堆栈与寄存器上下文。

---

## 旧版配置迁移

若游戏根目录下存在旧版的 `gml-host.ini` 且当前没有 `gakuloader.ini`，加载器在首次启动时会自动将其复制并重命名为 `gakuloader.ini`，保留用户先前已停用的插件设置。

---

[Wiki 首页](Home.md) · [全部章节](_Sidebar.md) · [项目首页](../README.md)
