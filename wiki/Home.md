# GakuLoader Wiki

欢迎来到 GakuLoader 文档。这里记录 GakuLoader 的安装、配置、插件开发与底层工作细节。

GakuLoader 是给《学园偶像大师》PC 版（Unity 6000.0.x IL2CPP x64）写的原生 DLL 加载器。它通过代理系统的 `version.dll` 注入游戏进程，负责把插件加载进内存、管理独占 Hook、分发物理热键、在 Unity 主线程调度任务，以及提供一个独立的 ImGui 控制窗口。

宿主尽量保持简单，只负责协调跨插件的共享资源，具体的 Mod 功能完全由插件自己实现。

---

## 文档目录

- [安装](Installation.md)：文件放到哪里、如何备份、升级与卸载。
- [配置](Configuration.md)：`gakuloader.ini` 的所有配置段、开关规则与热键支持的语法。
- [编写插件](Writing-a-Plugin.md)：从零写一个最小插件，导出契约与编译注意事项。
- [插件 API](Plugin-API.md)：生命周期各个回调的时机、线程归属与宿主提供的服务接口。
- [Hooks](Hooks.md)：MinHook 独占机制、批量申请，以及共享 `CampusActorController::LateUpdate` 的参与者链。
- [界面与 UI](UI.md)：独立 Win32/D3D11 控制窗口的工作方式、F1 切换与 ImGui 绘制方法。
- [快捷键](Hotkeys.md)：物理按键检测、输入焦点保护、双向失效冲突仲裁与 ini 重映射。
- [编译与测试](Building.md)：MSVC 编译要求、静态运行时 `/MT`、编译选项与测试套件。
- [故障排查](Troubleshooting.md)：看日志、插件未加载、启动时窗口抢焦点排查，以及游戏退出死锁的看门狗说明。

---

[Wiki 首页](Home.md) · [全部章节](_Sidebar.md) · [项目首页](../README.md)
