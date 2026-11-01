# Building

## 工具链

安装 Visual Studio 的“使用 C++ 的桌面开发”工作负载及 Windows SDK，支持：

- VS 2022 / MSVC v143。
- VS 2026 / MSVC v145。

根目录的 [GakuLoader.slnx](../GakuLoader.slnx) 用于支持该格式的 IDE；脚本直接构建 `.vcxproj`，
不要求旧版 VS 2022 识别 `.slnx`。公共参数定义在 [Directory.Build.props](../Directory.Build.props)。

宿主使用 C++20、UTF-8 源码、Release `/MT`（Debug `/MTd`）静态 CRT。
禁止为宿主启用 Control Flow Guard（`/guard:cf`），避免拦截 MinHook 动态 trampoline。
宿主不支持 MinGW；MinGW 仅用于跨编译器测试夹具。

## 本地构建

本快照已 vendor 构建所需依赖，普通文件随仓库检出；不需要初始化子模块或下载兄弟仓库。

默认只做源码验证，在仓库根目录执行 `tools/validate-source.ps1`。它不执行窗口或游戏检查。

单独构建命令：

```powershell
# 自动选择已安装的 v143/v145；Release 输出 build/version.dll
.\build.ps1

# 指定工具集
.\build.ps1 -PlatformToolset v143

# Debug 输出 build/debug/version.dll
.\build.ps1 -Configuration Debug

# 只将候选复制到仓库内指定位置
.\build.ps1 -OutputDll build/candidate/version.dll
```

脚本通过 `vswhere` 定位 MSBuild，并在构建前检查工具集和 vendor 文件；Windows SDK 是否完整由 MSBuild 校验。
`build.ps1` 始终只构建本地产物；`-OutputDll` 仅复制候选文件。本仓不接受 `-SkipDeploy` 或 `-DeployPath`，
也不会根据默认游戏路径安装。本仓不提供二进制发行包；手动安装边界见 [Installation](Installation.md)。

## 第一个插件

最小插件使用同一套 MSVC 发现逻辑，不依赖宿主、MinGW 或游戏：

```powershell
.\examples\minimal-plugin\build.ps1
.\examples\minimal-plugin\verify.ps1
```

第二条命令使用合成宿主验证查询、激活和生命周期日志。
源码及线程规则见 [Writing a Plugin](Writing-a-Plugin.md)。

## 测试

MSVC 路径可以独立检查文档、最小插件和本地宿主包：

```powershell
.\tools\test-docs.ps1
.\tools\test-package.ps1
.\tests\build-tests.ps1 -SkipFixtures
.\tests\run-all.ps1 -SkipBuild -SkipFixtures -Only docs,frame-pump
```

`test-package.ps1` 检查本地 `version.dll` 的 x64、代理导出和 CRT 依赖边界，不复制到游戏。
旧名 `tools/test-deployment.ps1` 仅作为兼容转发，已不表示部署或回滚实测。

完整矩阵另外需要 MinGW 的 `gcc`、`g++`，用于生成异构 DLL；其缺失必须报告为未运行的可选兼容检查，
不能把不存在的夹具当作宿主契约失败。`run-all.ps1 -SkipFixtures` 明确将依赖夹具的套件标为
`SKIP_DEPENDENCY`，报告总状态为 `PASS_WITH_SKIPS`，不冒充完整矩阵通过。安装 MinGW 后确保工具在 PATH，或向 `tests/build-fixtures.ps1`
传入 `-MinGwBin` 指定其 bin 目录。

```powershell
# 构建 MSVC 测试及 MinGW 夹具，再执行完整矩阵
.\tests\run-all.ps1

# 已完成对应构建时，可选跑具体套件
.\tests\run-all.ps1 -SkipBuild -Only host,frame-pump
```

测试报告写入 `build/tests/report.json`。完整矩阵覆盖文档、真实 fixture 加载、无 actor 帧泵、
诊断边界、原生对话框和 D3D11 WARP 等；窗口相关测试需要可用的 Windows 桌面会话。
具体未运行项、缺失依赖与失败要分别报告。离线检查不替代实机验收。

## 自动验证

[Windows MSVC 工作流](../.github/workflows/windows-msvc.yml) 准备了 VS2022/v143 的宿主、
MSVC 测试构建、独立插件、文档链接与命令、本地包契约，以及无窗口帧泵、C ABI、MinHook 检查。
配置文件落盘不代表远端已运行。MinGW 异构矩阵和真实桌面窗口检查需在具备相应依赖的环境另行执行。
受限进程若遇 `LNK1101`（MSPDB140.DLL），应先在允许 MSVC 调试符号服务的环境确认构建；
不要把无法链接的结果当成插件逻辑或游戏验收失败。
