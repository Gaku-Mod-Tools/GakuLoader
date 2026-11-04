# GakuLoader Wiki

GakuLoader 是为《学园偶像大师》PC 版（Unity 6000.0.x IL2CPP x64）编写的原生 DLL 插件加载器。它通过代理系统 `version.dll` 注入游戏进程，负责加载与管理插件、提供 MinHook 独占租约与批量挂接、在 Unity 主线程调度异步任务、分发物理热键，并提供基于 Direct3D 11 与 ImGui 的独立桌面控制窗口。

加载器本身保持轻量，专注于跨插件的共享资源协调与底层调用仲裁，具体的游戏功能完全由业务插件独立实现。

---

## 快速导航

### 使用指南
- [安装与部署](Installation.md)：文件放置路径、配置备份、版本升级与干净卸载实操步骤。
- [配置文件](Configuration.md)：`gakuloader.ini` 的段落结构、外观设置、插件启闭与热键语法。
- [常见问题与排查](Troubleshooting.md)：查看运行日志、插件未加载诊断、按键失效排查与退出看门狗多阶段预算。

### 插件开发
- [编写插件](Writing-a-Plugin.md)：从零编写 C-ABI v1 插件、导出契约与离线验证。
- [插件 API 与服务](Plugin-API.md)：生命周期回调时机、线程模型与 14 项内置及扩展核心服务。
- [Hook 机制与 Actor Entry](Hooks.md)：MinHook 独占租约机制、批量申请与角色更新入口共享。
- [桌面界面与 UI 规范](UI.md)：独立 Win32/D3D11 伴随窗口、F1 交互、ImGui 面板绘制规范与文件拖放集成。
- [快捷键系统](Hotkeys.md)：12ms 物理按键轮询、边沿触发、前台焦点保护与冲突仲裁。

### 源码与构建
- [源码构建与测试](Building.md)：MSVC 工具链要求、静态 CRT `/MT` 配置、本地编译与测试矩阵。

---

[Wiki 首页](Home.md) · [全部章节](_Sidebar.md) · [项目首页](../README.md)
