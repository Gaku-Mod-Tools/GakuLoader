# 插件 API 与服务

本页说明 GakuLoader 插件的生命周期回调函数、执行线程模型，以及宿主提供的全部 14 项官方内置与扩展服务。

---

## 生命周期回调与线程模型

插件在 `GakuPluginDescriptor` 中注册以下回调函数。未使用的回调设为 `nullptr` 即可：

| 回调函数 | 执行线程 | 触发时机与调用规则 |
|---|---|---|
| `activate` | Loader 启动线程 | 宿主加载插件时最先调用（在 Windows Loader Lock 外）。Unity 引擎与 IL2CPP 尚未加载，仅允许读取本地配置、获取宿主服务、注册物理热键；禁止解析 IL2CPP 函数或创建托管对象。 |
| `onRuntimeReady` | Hook 工作线程 | `GameAssembly.dll` 导出就绪后调用。适合在此阶段解析元数据与安装 Detour Hook；此时 GC 尚未完全就绪，禁止创建托管对象。 |
| `onTick` | Unity 主线程 | 游戏每帧调用至多一次。主线程任务队列在它之前已被排空。标题界面或无角色场景同样会推进，可安全调用托管对象。 |
| `onActorTick` | Unity 主线程 | 在每个存活角色的原生 `LateUpdate` 执行完毕后调用。`activeActor` 是瞬时借用指针，代表当前角色的 C# 实例地址，严禁跨帧持久化缓存。 |
| `onDesktopUi` | 独立桌面 UI 线程 | 用户在 F1 控制窗口左侧选中本插件面板时调用，用于绘制 ImGui 控件。 |
| `shutdown` | Unity 主线程 | 游戏退出时按插件加载顺序的严格逆序调用，用于停止私有线程并释放自建句柄。退出后宿主会自动回收该插件占用的 Hook 租约和主线程任务。 |

---

## 宿主服务全集（14 项官方与扩展服务）

插件在 `activate` 或后续回调中，通过 `host->queryService` 查询宿主提供的功能接口：

```cpp
GakuLogService log{};
host->queryService(host->hostContext, GAKU_SERVICE_LOG, GAKU_SERVICE_VERSION, sizeof(log), &log);
```

宿主当前提供 14 项标准服务，涵盖核心运行时、界面交互与扩展能力：

| 服务标识符 | 所在头文件 | 接口结构体 | 核心功能与用途 |
|---|---|---|---|
| `gakuloader.log` | `GakuPlugin.h` | `GakuLogService` | 统一日志写入，输出至 `GakuLoader/Logs/gakuloader.log` |
| `gakuloader.runtime` | `GakuPlugin.h` | `GakuRuntimeService` | 获取游戏根目录、本地数据目录路径及 `GameAssembly.dll` 模块句柄 |
| `gakuloader.main-thread` | `GakuPlugin.h` | `GakuMainThreadService` | 向 Unity 主线程投递异步排队任务，检测当前是否为主线程 |
| `gakuloader.hooks` | `GakuPlugin.h` | `GakuHookService` | MinHook 独占租约申请、释放与单次挂起批量安装（1-256 个） |
| `gakuloader.desktop-ui` | `GakuPlugin.h` | `GakuDesktopUiService` | 在 `onDesktopUi` 中通过 ABI 指纹绑定宿主 ImGui 上下文与内存分配器 |
| `gakuloader.appearance` | `GakuPlugin.h` | `GakuAppearanceService` | 查询宿主当前的桌面 UI 主题与缩放快照 |
| `gakuloader.desktop-window` | `GakuPlugin.h` | `GakuDesktopWindowService` | 获取宿主桌面 Win32 窗口 HWND 句柄，异步请求切换到本插件面板 |
| `gakuloader.file-dialog` | `GakuPlugin.h` | `GakuFileDialogService` | 非模态异步文件打开对话框（发起、轮询与释放） |
| `gakuloader.hotkeys` | `GakuPlugin.h` | `GakuHotkeyService` | 注册与解绑物理热键，边沿触发与防误触 |
| `gakuloader.actor-entry` | `GakuPlugin.h` | `GakuActorEntryService` | 共享介入角色 `LateUpdate` 的 BEFORE 与 AFTER 执行阶段 |
| `gakuloader.paths` | `GakuPaths.h` | `GakuPathsV1` | 规范化插件分类数据存储路径（UserData、Configs、Cache 等） |
| `gakuloader.language` | `GakuLanguage.h` | `GakuLanguageServiceV1` | 查询当前加载器桌面 UI 语言偏好（英文/简中/繁中/日文） |
| `gakuloader.file-drop` | `GakuFileDrop.h` | `GakuFileDropService` | 在 ImGui 面板中声明文件拖放区域并轮询接收拖入文件路径 |
| `gakuloader.process-lifetime-hooks` | `GakuProcessLifetimeHooks.h` | `GakuProcessLifetimeHooksV1` | 将 Hook 独占租约升级为进程生命周期常驻（插件退役时不卸载） |

---

## 重点服务使用说明

### 1. `gakuloader.log`（日志输出）
写日志到 `GakuLoader/Logs/gakuloader.log`：
```cpp
log.write(log.context, GAKU_LOG_INFO, "hello from plugin");
```
消息为普通 UTF-8 文本，不需要传 printf 格式化串。

### 2. `gakuloader.runtime`（运行环境）
提供游戏各级目录与模块句柄：
- `gameRoot`：游戏根目录绝对路径（宽字符）。
- `localRoot`：`GakuLoader` 根目录绝对路径（宽字符）。
- `pluginsRoot`：`GakuLoader/plugins` 目录绝对路径（宽字符）。
- `getGameAssembly()`：获取 `GameAssembly.dll` 模块句柄（加载完成前返回 `NULL`）。

### 3. `gakuloader.main-thread`（主线程任务调度）
向 Unity 主线程任务队列投递任务：
```cpp
mainThread.enqueue(mainThread.context, [](void* data) {
    // 该回调安全运行在 Unity 主线程
}, userData);
```
任务队列容量上限为 1024。`enqueue` 为纯异步投递，不会阻塞当前调用线程。

### 4. `gakuloader.paths`（规范化数据路径）
通过 `include/GakuPaths.h` 获取分类明确的磁盘存储路径，避免插件随意在游戏目录生成文件：
- `GAKU_PATH_USER_DATA` (1)：用户生成数据（如导出的动作、录制的镜头文件）。
- `GAKU_PATH_CONFIGS` (2)：插件私有配置文件。
- `GAKU_PATH_PLUGIN_DATA` (3)：插件自带静态资源与模型包。
- `GAKU_PATH_CACHE` (4)：运行时缓存文件。
- `GAKU_PATH_LOGS` (5)：插件专用日志输出目录。
- `GAKU_PATH_TEMP` (6)：临时解压与中间文件目录。

通过 `getRoot` 获取对应目录的绝对路径，通过 `ensureRoot` 显式创建该目录。

### 5. `gakuloader.language`（界面语言）
通过 `include/GakuLanguage.h` 获取当前用户在 F1 面板设置的界面语言，方便插件面板多语言适配：
- `GAKU_LANGUAGE_ENGLISH` (0)：英文
- `GAKU_LANGUAGE_SIMPLIFIED_CHINESE` (1)：简体中文
- `GAKU_LANGUAGE_TRADITIONAL_CHINESE` (2)：繁体中文
- `GAKU_LANGUAGE_JAPANESE` (3)：日文

### 6. `gakuloader.file-dialog`（异步文件选择）
异步请求系统原生文件打开对话框，由专用线程处理，不需要在游戏帧内等待选择结果：
1. 调用 `beginOpen()` 发起请求，获得一个 `ticket`（每个插件同一时刻仅允许一个活跃请求）。
2. 在每帧循环或 UI 回调中调用 `poll()` 检查状态（`PENDING`、`ACCEPTED`、`CANCELLED`、`FAILED`）。
3. 状态变为 `ACCEPTED` 时读取选中文件路径，随后调用 `release()` 释放 ticket。

### 7. `gakuloader.file-drop`（文件拖放集成）
通过 `include/GakuFileDrop.h`，在所属插件的 `onDesktopUi` 回调中调用 `setTarget` 设置接收区域，坐标为宿主单视口的**客户区像素坐标**。区域每个完成的 UI 帧都要重新登记；宿主只向当前选中的插件面板投递。`poll` 先读取数量与 UTF-8 字节数，再用足够的缓冲区取回路径并消费一次；缓冲区不足时保留批次。每批最多 256 个文件、路径总长 1 MiB。窗口外或区域外的拖放不会投递。

---

## 插件间自定义服务发布与共享

插件不仅能消费宿主服务，还可以作为服务提供方，通过 `host->registerService` 发布自定义函数表供其他 Mod 插件调用：

- **命名规范**：服务名最长 127 字节，**严禁以 `gakuloader.` 前缀命名**（该前缀为宿主官方保留）。
- **容量约束**：函数表大小上限为 64 KiB，全进程最多支持注册 64 个自定义服务。
- **生命周期**：宿主负责服务注册与查表，不接管内部业务数据生命周期。发布服务的插件卸载或退出时，宿主会自动注销其发布的服务表。

---

[Wiki 首页](Home.md) · [全部章节](_Sidebar.md) · [项目首页](../README.md)
