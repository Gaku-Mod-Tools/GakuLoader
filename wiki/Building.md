# 源码构建与测试

本页说明 GakuLoader 宿主的开发环境准备、源码编译步骤、最小插件构建与测试套件运行方法。

---

## 开发环境准备

构建 GakuLoader 需要 Windows 64 位开发环境，支持以下工具链：

- **编译器**：Visual Studio 2022 (MSVC v143) 或 Visual Studio 2026 (MSVC v145)。
- **工作负载**：在 Visual Studio Installer 中勾选“使用 C++ 的桌面开发”及 Windows 10/11 SDK。
- **方案文件**：根目录包含 [GakuLoader.slnx](../GakuLoader.slnx)，公共构建属性配置在 [Directory.Build.props](../Directory.Build.props) 中。
- **编译参数规范**：
  - C++ 标准：C++20。
  - 运行时库：Release 采用 `/MT`，Debug 采用 `/MTd`（静态链接 CRT，避免依赖外部 VC++ 运行库）。
  - 安全选项：禁止开启 Control Flow Guard（`/guard:cf`），避免拦截 MinHook 动态生成的跳板代码。

---

## 获取源码与子模块

发布源码快照已包含 MinHook 和 ImGui；开发检出若将依赖配置为子模块，先检查状态和本地修改，再按需初始化：

```powershell
git submodule status
git submodule update --init --recursive
```

---

## 本地编译步骤

在仓库根目录下运行 PowerShell 构建脚本：

```powershell
# 自动检测已安装的 MSVC 工具集，编译 Release 版本（输出至 build/version.dll）
.\build.ps1

# 显式指定使用的工具集版本（v143 或 v145）
.\build.ps1 -PlatformToolset v143

# 编译 Debug 调试版本（输出至 build/debug/version.dll）
.\build.ps1 -Configuration Debug

# 指定输出 DLL 文件的路径
.\build.ps1 -OutputDll build/candidate/version.dll
```

构建脚本通过 `vswhere` 定位 MSBuild，并在编译前检查依赖项完整性。关于编译产物的安装放置与目录布局，参见 [安装与部署](Installation.md)。

---

## 编译最小插件并验证

示例插件 `examples/minimal-plugin/` 拥有独立的构建与验证逻辑，不依赖游戏客户端即可离线验证 ABI 协商与基础功能：

```powershell
# 编译示例插件
.\examples\minimal-plugin\build.ps1

# 运行离线验证（在模拟宿主中测试加载、激活与生命周期日志）
.\examples\minimal-plugin\verify.ps1
```

插件代码结构与生命周期说明参见 [编写插件](Writing-a-Plugin.md)。

---

## 运行自动化测试

在 MSVC 环境下可以独立运行文档静态检查、打包契约验证以及宿主测试：

```powershell
# 验证文档链接完整性、代码块一致性与命令规范
.\tools\test-docs.ps1 -StaticOnly

# 验证编译产物的导出符号与 CRT 依赖规范
.\tools\test-package.ps1

# 构建测试套件（跳过外部 MinGW 夹具）
.\tests\build-tests.ps1 -SkipFixtures

# 运行基础测试套件
.\tests\run-all.ps1 -SkipBuild -SkipFixtures -Only docs,frame-pump
```

若需运行包含异构编译器验证的完整测试矩阵，可安装 MinGW-w64 并将其 `bin` 目录加入 PATH，随后执行：

```powershell
# 构建 MSVC 与 MinGW 测试夹具并执行完整矩阵
.\tests\run-all.ps1
```

测试报告将生成在 `build/tests/report.json` 中。涉及桌面窗口与 WARP 光栅化的测试需要当前环境具备可用的 Windows 桌面会话。

---

## 常见构建问题排查

- **遇到 `LNK1101: 无法打开模块调试源文件 (MSPDB140.DLL)`**：通常是由于杀毒软件占用或符号服务权限受限引起，可尝试将构建目录加入防病毒排除项，或在管理员权限终端中重新构建。
- **子模块缺失报错**：先确认检出是否使用子模块及本地依赖状态；发布源码缺失时应重新取得完整快照，开发检出则按仓库配置初始化。

---

[Wiki 首页](Home.md) · [全部章节](_Sidebar.md) · [项目首页](../README.md)
