# Configuration

本页说明 `gakuloader.ini` 的配置项、格式规范与支持的按键语法。

配置文件位于游戏根目录下。所有段落和键值都是可选的，文件不存在时加载器完全按默认行为运行。

```ini
; gakuloader.ini

[plugins]
; 禁用指定插件。文件名不区分大小写。
; 填 false / no / 0 都会在 LoadLibrary 之前跳过，文件依然留在磁盘上。
SomePlugin.dll=false

[hotkeys]
; 兼容旧格式：<pluginId>.<actionId>=<快捷键>（唯一匹配时生效）
gakumotion.show-gui=Ctrl+Shift+M
; 等号右边留空表示完全解绑该快捷键
example.action=

[host]
; 设为 true 会在启动时通过 AllocConsole 打开一个黑窗口，实时显示合并日志
console=false

[diagnostics]
; 设为 true 会记录崩溃转储日志 crash.log
trace=false
```

修改配置文件后需要重启游戏生效。

---

## `[plugins]` 段

- 这里的检查发生在 `LoadLibraryExW` 之前。被禁用的 DLL 根本不会被加载到内存里，它的 `DllMain` 也不会执行。
- 支持 `true/false`、`yes/no`、`1/0`。空值保持默认允许加载。

---

## `[hotkeys]` 段

整个 `[hotkeys]` 段上限为 32768 个字符（含结尾双 NUL）。超限时会记录诊断并拒绝整段覆盖，保留当前运行状态，不会静默加载截断的部分。

- 建议通过 **Manager → Hotkeys** 保存绑定。动作身份使用插件注册的原始 `(pluginId, actionId)`，区分点的位置和大小写；修改一项不会改动其他动作。
- Manager 保存的键名为 `v2:<pluginId 原始字节的十六进制>:<actionId 原始字节的十六进制>`。无需手工编码；保留生成的键名，修改等号后的组合键即可。新键优先于旧格式。
- 旧 `<pluginId>.<actionId>` 项仍兼容读取，不区分大小写，但仅在当前已注册动作中唯一匹配时生效。例如 `a.b` / `c` 与 `a` / `b.c` 都对应旧键 `a.b.c`，此时旧覆盖不应用于任一动作，日志会说明歧义；可在 Manager 分别重新设置。旧项不会自动删除或改写。
- 空值表示解除绑定；新键的 `<default>` 值表示跟随插件当前声明的默认值，包括插件升级后改变的默认值。Manager 的“恢复默认”写入此标记，不把当下按键固化成覆盖。
- 快捷键组合支持 `Ctrl`、`Shift`、`Alt`、`Win` 修饰符，不区分大小写，用 `+` 连接。
- **F1 键被加载器保留用于切换 F1 控制窗口，不能绑定给插件**。

支持的按键名字包括：
- 普通字母与数字：`A` - `Z`，`0` - `9`
- 功能键：`F1` - `F24`
- 特殊键：`Space`, `Tab`, `Enter` / `Return`, `Esc` / `Escape`, `Backspace`, `Insert`, `Delete` / `Del`, `Home`, `End`, `PageUp`, `PageDown`, `Up`, `Down`, `Left`, `Right`
- 标点符号：`Comma`, `Period`, `Minus`, `Plus`, `Equal`, `Slash`, `Backslash`, `Semicolon`, `Quote`, `LeftBracket`, `RightBracket`, `Grave`

如果两个插件注册了相同的快捷键，加载器会判定为冲突，两个插件都不会被触发，并在 F1 窗口里标出 `conflict`。在 Manager 中给其中一个改键即可立即恢复；手工修改 ini 后需要重启。

---

## 旧配置迁移

如果根目录下有旧版的 `gml-host.ini` 且当前没有 `gakuloader.ini`，加载器在首次启动时会自动把旧文件复制一份命名为 `gakuloader.ini`，避免用户之前关掉的插件又被重新开启。这只是配置文件的单次拷贝导入，并不代表兼容旧版插件。

---

[Wiki 首页](Home.md) · [全部章节](_Sidebar.md) · [项目首页](../README.md)
