**简体中文** | [English](README.en.md) | [日本語](README.ja.md)

# GakuLoader

[数据目录与迁移](docs/ROOT_LAYOUT.md)

完整文档：[Wiki 首页](wiki/Home.md) · [快速上手](docs/QUICKSTART.md) · [全部章节导航](wiki/_Sidebar.md)

## 这是什么

GakuLoader 是《学园偶像大师》（PC DMM 版）的原生 DLL 插件加载器。它伪装成游戏根目录下的 `version.dll`，在游戏启动时由操作系统自动加载注入游戏进程。GakuLoader 负责发现并加载插件、分发键盘快捷键、调度主线程任务、管理底层独占 Hook 租约，并提供一个按 **F1** 呼出的独立管理窗口。具体的 Mod 功能（如动作播放、文本翻译等）由各个插件独立实现。

## 准备工作与依赖

### 运行环境
- 操作系统：Windows 10 / 11 64 位（x64）
- 目标游戏：《学园偶像大师》PC DMM 版

### 安装步骤
1. 完全退出游戏。
2. 将编译产物或发布包中的 `version.dll` 放置在游戏根目录（即 `gakumas.exe` 所在文件夹）。
3. 将插件 DLL 放置在 `GakuLoader/plugins/` 目录中。

```text
<游戏根目录>/
├── gakumas.exe
├── version.dll                    # GakuLoader 核心
└── GakuLoader/
    ├── Configs/gakuloader.ini     # 宿主配置（可选）
    ├── Logs/gakuloader.log        # 宿主日志
    └── plugins/                   # 插件目录
        ├── GakuMotion.dll         # 动作播放插件
        └── GakuLocalify.dll       # 本地化汉化插件
```

### 配置文件说明

`GakuLoader/Configs/gakuloader.ini` 用于控制插件开关、调整快捷键、设置界面主题或开启调试控制台。如果该文件不存在，加载器会自动采用默认设置：

```ini
[plugins]
; 禁用指定插件，不填默认全部加载（文件名不区分大小写）
SomePlugin.dll=false

[hotkeys]
; 快捷键设置格式：<pluginId>.<actionId>=<按键>
gakumotion.show-gui=Ctrl+Shift+M
; 留空表示解绑该动作的快捷键
someplugin.action=

[appearance]
; 界面主题：Dark | Sapphire | PaperAndInk | AppleLight | AppleDark（默认 Dark）
theme=Dark
; 界面缩放比例：百分比整数，范围 50 到 300（默认 100）
scale=100
; 界面语言：en | zh-Hans | zh-Hant | ja（默认自动匹配系统语言）
language=zh-Hans

[host]
; 设为 true 会在游戏启动时打开独立的控制台窗口，实时输出合并日志
console=false

[diagnostics]
; 设为 true 会在遇到异常崩溃时生成 crash.log
trace=false
```

日志文件默认保存在 `GakuLoader/Logs/gakuloader.log`。手动修改配置文件或增删插件文件后需重启游戏生效。

## 使用方法与快捷键

启动游戏后，常用快捷键与操作如下：

| 操作 / 快捷键 | 功能说明 |
| --- | --- |
| **F1** | 打开或收起 GakuLoader 管理控制台窗口 |
| **关闭按钮** | 点击窗口右上角“×”会最小化到系统右下角托盘，右键托盘图标可重新呼出 |
| **Manager → Hotkeys** | 在面板的 Hotkeys 页签中可直接可视化查看和修改各插件的快捷键映射，即时生效 |
| **Manager → Plugins** | 查看当前所有已加载插件的名称、版本号、作者及激活状态 |

### 支持的插件类型
- **GakuMotion**：角色 MMD 动作驱动、表情、音频与运镜回放。
- **GakuLocalify**：游戏界面文本汉化翻译、贴图与字体替换。
- **GakuVRify**：OpenXR VR 沉浸式体验支持。
- **自定义插件**：任何遵循当前 `include/GakuPlugin.h` 规约、导出 `GakuPlugin_Query` 入口函数的 x64 DLL 均可由 GakuLoader 加载。

> 旧版基于早期废弃接口（`GmlPlugin_Query` / `GakumasModPlugin.h`）编译的旧插件无法直接加载，运行日志中会记录错误提示，需要使用新版头文件重新编译。

## 常见问题与排查

1. **插件没有被加载**：
   - 检查 `GakuLoader/Logs/gakuloader.log` 日志文件。
   - 确认插件 DLL 是否放置在 `GakuLoader/plugins/` 目录中，而不是游戏根目录。
   - 确认插件架构为 64 位（x64），且导出了正确的 `GakuPlugin_Query` 接口。
2. **退出游戏时卡住或安全看门狗（QuitGuard）机制**：
   - Unity 6 引擎在接收到常规窗口关闭指令（WM_CLOSE）时，底层有时会因内部线程死锁而无法正常结束进程。
   - GakuLoader 内置了退出看门狗（QuitGuard）：当用户关闭游戏时，宿主会在预留时间内依次通知插件安全释放资源；若主线程因引擎原因陷入死锁超时，看门狗会强制安全终止进程，防止游戏卡死在后台占用系统资源。详情见 [排障指南](wiki/Troubleshooting.md)。
3. **快捷键冲突**：
   - 若不同插件绑定了相同快捷键，可以在 F1 窗口的 **Manager → Hotkeys** 选项卡中重新分配，也可以直接在 `gakuloader.ini` 中手动指定。
4. **插件热重载限制**：
   - 目前插件不支持在游戏运行中途热插拔或动态替换，修改插件文件后需重启游戏。

## 源码构建

### 编译环境要求
- Windows 10 / 11 64 位
- Visual Studio 2022（MSVC v143）或 Visual Studio 2026（MSVC v145），勾选“使用 C++ 的桌面开发”工作负载与 Windows SDK。
- 源码内已包含所需的 Dear ImGui 与 MinHook 源码，无需另外拉取外部子模块。

### 编译命令
在 `GakuLoader` 目录下打开 PowerShell 执行构建：

```powershell
# 编译 Release 版本（输出产物为 build/version.dll）
.\build.ps1

# 校验文档与 ABI 兼容性
.\tools\test-docs.ps1
```

> **编译注意事项**：
> 宿主工程强制使用 `/MT` 静态运行时链接。编译时切勿开启 Control Flow Guard（`/guard:cf`），否则会导致 MinHook 的底层动态跳板被系统安全机制拦截而崩溃。

### 插件开发入门
新建插件只需包含当前仓库的 `include/GakuPlugin.h` 头文件，参考示例位于 [examples/minimal-plugin/MinimalPlugin.cpp](examples/minimal-plugin/MinimalPlugin.cpp)。

运行最小插件的构建与离线合成检查：
```powershell
.\examples\minimal-plugin\build.ps1
.\examples\minimal-plugin\verify.ps1
```

### Wiki 详细文档导航
- [安装与目录规范](wiki/Installation.md)：部署路径、文件备份与更新方法
- [配置说明](wiki/Configuration.md)：`gakuloader.ini` 各配置项详解
- [编写插件](wiki/Writing-a-Plugin.md)：从零编写一个 GakuLoader 插件
- [插件 API 规范](wiki/Plugin-API.md)：生命周期回调函数与宿主服务接口
- [底层 Hook 机制](wiki/Hooks.md)：MinHook 独占租约与 LateUpdate 调度
- [UI 渲染架构](wiki/UI.md)：基于 D3D11 与 ImGui 的独立管理器窗口
- [热键系统](wiki/Hotkeys.md)：全局按键检测与冲突仲裁
- [源码构建深入](wiki/Building.md)：编译参数、工具集兼容性与测试流程
- [常见问题与排障](wiki/Troubleshooting.md)：日志分析、退出看门狗与常见报错
