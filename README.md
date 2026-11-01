# GakuLoader

《学园偶像大师》PC DMM 版的 Windows x64 原生插件宿主源码。提供版本代理、插件生命周期、主线程任务、Hook 租约、热键和 F1 桌面控制台；业务由插件实现。

本仓是独立冻结的源码快照，没有预编译安装包。默认只在本地构建和执行合成检查，不安装或操作游戏。源码、来源与审核范围见 [审核记录](STAGING_REVIEW.md)。

## 构建与验证

需要 Visual Studio 的 C++ 桌面工作负载（MSVC v143 或 v145）及 Windows SDK。第三方源码已完整按构建范围 vendor，无需子模块或其他仓库。

```powershell
.\tools\validate-source.ps1
```

该入口构建 Release/Debug 宿主及现有 MSVC 测试工程，运行文档、无窗口帧泵、C ABI、MinHook 隔离和最小插件检查，检查本地 DLL 包契约。所有产物和日志写入忽略的 `build/`。
MinGW 异构兼容矩阵、真实桌面窗口测试及游戏验收不在默认执行范围。

只构建宿主或第一个插件：

```powershell
.\build.ps1
.\examples\minimal-plugin\verify.ps1
```

[构建说明](wiki/Building.md) · [插件开发](docs/PLUGIN_DEVELOPMENT.md) · [API](wiki/Plugin-API.md) · [配置](wiki/Configuration.md) · [手动安装边界](wiki/Installation.md)

## 来源与许可

冻结来源：`6db0f56440b183d37b57c467d68b452d13eb3d47`。实际选中与调整文件见 [SOURCE_SELECTION.json](SOURCE_SELECTION.json)，当前快照每文件哈希见 [SOURCE_MANIFEST.json](SOURCE_MANIFEST.json)。
第一方代码保留 [MIT](LICENSE) 声明；[第三方通知](THIRD_PARTY_NOTICES.md) 单列 ImGui、MinHook/HDE 及本地补丁。没有独立上游 pristine 或所有权认证。

GakuMotion.dll、GakuLocalify.dll 等业务插件不包含在本仓。宿主日志契约见用户指南，本次源码验证不写游戏日志。

[English](README.en.md) · [日本語](README.ja.md)
