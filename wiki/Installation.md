# 源码快照的安装边界

本仓默认只做源码构建验证，没有预编译发行包。下面是维护者未来单独授权安装时的文件契约，
不授权本任务或 Agent 访问、部署或启动游戏。

# Installation

## 安装与备份

普通用户使用与插件配套的产品发布包；源码开发者可使用本仓构建的 `build/version.dll`。
不要混搭来自不同阶段的 Loader、插件和导入器文件。

1. 确认游戏已完全退出。
2. 如果游戏根目录原有 `version.dll`，先备份并确认它属于哪个工具；保留可恢复副本。
3. 将配套的 `version.dll` 放到与 `gakumas.exe` 同层的位置。
4. 将配套插件 DLL 放进 `gakumas-local/plugins/`，插件附属文件按对应产品指南放置。

```text
<游戏根目录>/
├── gakumas.exe
├── version.dll                    GakuLoader
├── gakuloader.ini                 可选，升级时保留个人配置
└── gakumas-local/
    ├── gakuloader.log
    └── plugins/
        ├── GakuMotion.dll
        └── GakuLocalify.dll
```

启动游戏后按 **F1** 打开控制窗口，并查看 `gakumas-local/gakuloader.log` 中的扫描、
拒绝原因与 `Activated` 记录。看到宿主窗口不代表每个插件都已激活；插件的使用验收见其产品指南。

## 升级与卸载

升级前保留旧 `version.dll`、插件配套文件和自己的 `gakuloader.ini`。关闭游戏后整体替换配套文件，
不要用默认配置覆盖个人开关或空热键绑定。发生问题时退出游戏，再恢复同一组备份。

卸载时退出游戏，将根目录的 Loader `version.dll` 移走；若安装前有其它代理，恢复它的原始副本。
插件、日志和配置可以先保留。移走 Loader 后这些插件不会再由它加载。

## 仓库与部署职责

[GakuLoader](../README.md) 是当前宿主源码入口。本仓 `build.ps1` 只做本地构建，
不提供自动安装到游戏的 `tools/deploy-host.ps1`。不依赖任何历史工作区，
其中的构建、部署脚本不作为当前入口；本仓不会自动调用或复制那套脚本。

包含 Loader、GakuMotion 与导入器的产品发布包负责配套版本、备份、校验和回滚流程。
仓库测试 `tools/test-package.ps1` 只验证本地构建产物；旧名 `tools/test-deployment.ps1`
仅为兼容转发，不能作为实际安装或回滚成功的证据。
