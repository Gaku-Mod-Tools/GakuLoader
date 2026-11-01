# 快速上手

GakuLoader 是《Gakumas》的原生插件宿主：游戏根目录的 `version.dll` 转发系统版本 API，并负责插件生命周期。插件作者见 [插件开发指南](PLUGIN_DEVELOPMENT.md)。

## 安装

先退出游戏。把构建产物 `build/version.dll` 复制到与 `gakumas.exe` 同层的游戏根目录，插件放进 `gakumas-local/plugins/`。

```text
<游戏根目录>/
├── gakumas.exe
├── version.dll                    GakuLoader
├── gakuloader.ini                 可选
└── gakumas-local\
    ├── gakuloader.log
    └── plugins\
        ├── GakuMotion.dll
        └── GakuLocalify.dll
```

游戏根目录的 `gakuloader.ini`（全部可选）：

```ini
[plugins]
; 文件名不区分大小写；默认允许，显式 false 才跳过
SomePlugin.dll=false

[hotkeys]
; 键名为 <pluginId>.<actionId>；空值表示解绑
gakumotion.show-gui=Ctrl+Shift+M

[diagnostics]
; 打开后记录更详细的诊断轨迹
trace=false

[host]
; 需要独立控制台时打开
console=false
```

缺省值就是上面的行为。修改后重启游戏生效，插件不支持 hot reload。`[plugins]` 在 `LoadLibrary` 前跳过禁用项，文件原样保留；支持 `true/false`、`yes/no`、`1/0`，空值保持默认允许。`[hotkeys]` 是物理键绑定：同一组合被两个 owner 认领时，两者一起失效并在 F1 管理器里显示冲突，不会静默双触发；F1 由 Loader 保留。若存在旧 `gml-host.ini` 且没有 `gakuloader.ini`，首次启动会**复制一次**作为配置迁移——这是配置导入，不是二进制兼容。

首次安装前备份已有的 `version.dll` 和配置。卸载时退出游戏并移走 Loader 的 `version.dll`；
若安装前已有其它代理，恢复其原始备份。插件与配置可以保留。

## 确认宿主启动

`gakumas-local/gakuloader.log` 应依次包含：

```text
Roots ready: gameRoot=... localRoot=...; native plugin activation starts before the game runtime
Scanning ...\gakumas-local\plugins (GakuLoader ABI v1)
Activated GakuMotion v... (native; runtime callbacks deferred)
Runtime ready on thread=...; dispatching onRuntimeReady (metadata/hooks only)
Frame readers ready; Unity window messages will drive actor-independent work
Successfully hooked CampusActorController::LateUpdate (...)
[DesktopUI] independent Win32/D3D11 control window ready ...
```

Desktop 控制窗口的标题是 `GakuLoader`；插件列表显示发现、激活和运行状态。帧泵与 actor 解耦：`onTick` 在标题、登录等没有角色的界面同样推进；`onActorTick` 只在有存活 actor 时执行。第一次进入有角色的场景会出现：

```text
First LateUpdate intercepted on actor ...; actor tick active.
```

DLL 被发现、插件被激活、插件真正开始处理帧/actor 是三个不同的里程碑；界面是否可读、字幕/动作是否正确仍需实机检查。

## 排查和退出

| 现象 | 检查 |
|---|---|
| 没有 `Scanning` | `version.dll` 是否在游戏根目录；是否被其它代理或杀软替换 |
| 插件没有 `Activated` | DLL 是否导出 `GakuPlugin_Query`、ABI/尺寸是否匹配；日志会给出拒绝原因 |
| `diagnostic boundary unavailable` | 只影响 Unity 日志隔离，插件照常运行；不需要处理，除非正在诊断日志转发 |
| 热键不触发 | `[hotkeys]` 是否被改空；同一组合是否与其它插件冲突（F1 管理器有提示） |
| 旧 DLL 只有一条 `rebuild against GakuPlugin.h` | 该 DLL 使用已删除的 GML ABI，需要按当前头文件重编译 |
| 控制窗关了但游戏还在 | 控制窗 X / Alt+F4 只是隐藏到托盘（托盘不可用时最小化）；关闭游戏走游戏自己的退出 |

正常关闭走游戏退出流程：先停 Desktop presentation，再逆序调用插件 `shutdown`，然后回收该插件的 hook / 任务 / 热键。看门狗保留强制结束兜底；强制退出时不能保证插件收尾，不要把唯一数据只放在退出回调里。

诊断边界（`DebugLogHandler.Internal_Log` / `Internal_LogException`）只做本地隔离：不转发官方 sink、不修改网络或其它上报。安装失败只记 WARN。

## 源码构建

安装 VS 2022 / MSVC v143 或 VS 2026 / MSVC v145 的 C++ 桌面开发工作负载及 Windows SDK。
在仓库根目录执行：

```powershell
.\build.ps1
.\examples\minimal-plugin\build.ps1
.\examples\minimal-plugin\verify.ps1
.\tools\test-docs.ps1
```

`build.ps1` 通过 `vswhere` 定位 MSBuild 并构建 `GakuLoader.vcxproj`，自动选择已安装的 v143/v145 工具集。
Release 输出 `build/version.dll`，Debug 输出 `build/debug/version.dll`。两者都只做本地构建；
可选 `-OutputDll` 复制候选文件，不会自动定位游戏目录或部署。完整解决方案入口为 `GakuLoader.slnx`。

最小插件的验证使用合成宿主，不依赖游戏、Loader 构建或 MinGW。完整宿主测试需要额外的跨编译器夹具，
见 [构建与测试](../wiki/Building.md)。夹具、WARP 与合成宿主检查均不能替代实际游戏行为验收。

配套安装、升级与回滚见 [安装与卸载](../wiki/Installation.md)；完整章节见 [Wiki 首页](../wiki/Home.md)。

[全部文档](README.md) · [项目首页](../README.md)
