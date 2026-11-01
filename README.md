**简体中文** | [English](README.en.md) | [日本語](README.ja.md)

# GakuLoader

完整文档：[Wiki 首页](wiki/Home.md) · [快速上手](docs/QUICKSTART.md) · [全部章节导航](wiki/_Sidebar.md)。

《学园偶像大师》（PC DMM 版）的原生 DLL 加载器。它通过伪装成游戏根目录的 `version.dll` 注入游戏进程，负责加载插件、分发热键、调度主线程任务、管理独占 Hook，并提供一个独立的 F1 调试窗口。

宿主尽量保持精简，只做跨插件的共享资源仲裁，具体的 Mod 业务逻辑完全由插件自己实现。

## 怎么安装

1. 退出游戏。
2. 把编译出来的 `build/version.dll` 放到游戏根目录（和 `gakumas.exe` 放在一起）。
3. 把插件 DLL 放到 `gakumas-local/plugins/` 目录。

```text
<游戏根目录>/
├── gakumas.exe
├── version.dll                    GakuLoader 本体
├── gakuloader.ini                 可选配置文件
└── gakumas-local/
    ├── gakuloader.log             运行日志
    └── plugins/                   插件目录
        ├── GakuMotion.dll
        └── GakuLocalify.dll
```

启动游戏后按 **F1** 可以打开或收起 GakuLoader 控制窗口。点击窗口右上角关闭会最小化到系统托盘，右键托盘图标可以重新唤出。

## 怎么配置

游戏根目录下的 `gakuloader.ini` 可以用来开关插件、改热键或开控制台。文件不存在时全部走默认配置：

```ini
[plugins]
; 禁用指定插件，不填默认加载（文件名不区分大小写）
SomePlugin.dll=false

[hotkeys]
; 重新绑定插件热键，格式是 <pluginId>.<actionId>=<按键>
gakumotion.show-gui=Ctrl+Shift+M
; 留空表示解绑
someplugin.action=

[host]
; 设为 true 会在启动时弹出一个黑窗口显示实时合并日志
console=false

[diagnostics]
; 设为 true 会在崩溃时记录 crash.log
trace=false
```

日志默认写在 `gakumas-local/gakuloader.log`。不支持热重载，修改配置或增删插件需要重启游戏生效。

## 它现在能加载什么插件？

- **GakuMotion**：已适配，用于动作驱动与运镜。
- **GakuLocalify**：已适配，用于汉化与文本替换。
- **自制插件**：任何按照当前 `include/GakuPlugin.h` 导出 `GakuPlugin_Query` 的 x64 DLL 都可以加载。

旧版基于已废弃接口（`GmlPlugin_Query` / `GakumasModPlugin.h`）编译的旧 DLL 无法加载，日志会提示错误，需要拿当前头文件重新编译。

## 我想开发插件，从哪里开始？

完整源码见 [最小插件](examples/minimal-plugin/MinimalPlugin.cpp)。只需要 MSVC 和当前头文件，不依赖 MinGW 测试夹具：

```powershell
.\examples\minimal-plugin\build.ps1
.\examples\minimal-plugin\verify.ps1
```

`verify.ps1` 在合成宿主中检查查询、激活与生命周期日志，不启动游戏；它不等于实机加载验收。

插件开发步骤、回调生命周期与完整示例见 Wiki 的 [Writing a Plugin](wiki/Writing-a-Plugin.md)。

## 怎么构建

安装 Visual Studio 的“使用 C++ 的桌面开发”工作负载及 Windows SDK；支持 VS 2022 / MSVC v143 与 VS 2026 / MSVC v145。脚本自动选择已安装工具集，也可显式指定 `-PlatformToolset v143`：

```powershell
# 编译 Release（产物输出在 build/version.dll）
.\build.ps1

# 检查文档、ABI 与独立最小插件
.\tools\test-docs.ps1
```

> 宿主强制使用 `/MT` 静态运行时，编译时不能开启 Control Flow Guard（`/guard:cf`），否则 MinHook 的动态跳板会被拦截崩溃。宿主不支持 MinGW 编译。

`build.ps1` 只生成本地产物；`-OutputDll` 仅复制到指定候选路径，不会自动定位游戏目录或部署。完整测试、可选 MinGW 夹具与依赖说明见 [Building](wiki/Building.md)。

源码已包含构建所需的 ImGui 和 MinHook，无需初始化子模块。Loader 与业务插件按产品发布包配套安装。

## 更详细的资料在哪里？

实现细节、API 规约与排查说明都在 Wiki：

- [Installation](wiki/Installation.md) - 安装、备份、卸载与部署职责
- [Configuration](wiki/Configuration.md) - ini 配置项与支持的按键语法
- [Writing a Plugin](wiki/Writing-a-Plugin.md) - 编写最小插件与编译注意事项
- [Plugin API](wiki/Plugin-API.md) - 生命周期回调与宿主提供的服务
- [Hooks](wiki/Hooks.md) - MinHook 独占租约与共享 LateUpdate 参与者
- [UI](wiki/UI.md) - 独立 D3D11 窗口与 ImGui 绘制
- [Hotkeys](wiki/Hotkeys.md) - 物理按键检测、焦点判断与冲突处理
- [Building](wiki/Building.md) - 编译器选项、产物隔离与测试套件
- [Troubleshooting](wiki/Troubleshooting.md) - 常见问题排查与退出看门狗
