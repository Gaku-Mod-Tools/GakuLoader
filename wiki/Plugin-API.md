# 插件 API 与服务

本页说明 GakuLoader 插件的生命周期回调函数、执行线程模型、可选元数据与依赖契约（`GakuPluginMetadata.h`），以及宿主提供的全部 15 项官方内置与扩展服务。

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

## 宿主服务全集（15 项官方与扩展服务）

插件在 `activate` 或后续回调中，通过 `host->queryService` 查询宿主提供的功能接口：

```cpp
GakuLogService log{};
host->queryService(host->hostContext, GAKU_SERVICE_LOG, GAKU_SERVICE_VERSION, sizeof(log), &log);
```

宿主当前提供 15 项标准服务，涵盖核心运行时、界面交互与扩展能力：

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
| `gakuloader.plugins` | `GakuPluginMetadata.h` | `GakuPluginsServiceV1` | 只读查询宿主版本/ABI 以及所有已发现插件的加载状态与摘要 |

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

### 8. `gakuloader.plugins`（只读插件注册表）
通过 `include/GakuPluginMetadata.h` 中的 `GAKU_SERVICE_PLUGINS`（`"gakuloader.plugins"`）与版本 `GAKU_PLUGINS_SERVICE_VERSION`（`1u`）查询 `GakuPluginsServiceV1`：

- `getHostInfo(uint32_t outputSize, GakuHostInfoV1* output)`：返回 `GakuHostInfoV1`，包含 `abiVersion`、`structSize`、`pluginAbiVersion`（`GAKU_ABI_VERSION`）、`pluginCount`（已发现的插件记录总数，含禁用与失败项）与 `loaderVersion[64]`（如 `"1.1.0"`）。
- `findPlugin(const char* pluginId, uint32_t outputSize, GakuPluginSummaryV1* output)`：按规范 `pluginId` 查找插件摘要。
- `getPluginAt(uint32_t index, uint32_t outputSize, GakuPluginSummaryV1* output)`：按索引枚举已发现插件摘要（`0 .. pluginCount - 1`）。

`GakuPluginSummaryV1` 包含 `abiVersion`、`structSize`、`state`（`GakuPluginLoadState`）、`hasMetadata`（`GakuPlugin_QueryMetadata` 成功时为 `1`）、`id[65]`、`name[128]`、`version[64]`、`subVersion[64]`。`GakuPluginLoadState` 状态值如下：

| 常量 | 值 | 含义 |
|---|---|---|
| `GAKU_PLUGIN_STATE_NOT_FOUND` | `0u` | 未找到对应插件 |
| `GAKU_PLUGIN_STATE_DISABLED` | `1u` | 被 `[plugins]` 配置禁用 |
| `GAKU_PLUGIN_STATE_DEPENDENCY_ERROR` | `2u` | 缺失必需依赖、版本范围不满足、存在冲突或存在必需依赖环 |
| `GAKU_PLUGIN_STATE_INCOMPATIBLE` | `3u` | 不满足 `minLoaderVersion` 或 ABI/描述符不兼容 |
| `GAKU_PLUGIN_STATE_QUERIED` | `4u` | 已完成 `GakuPlugin_Query`（与可选元数据查询），尚未激活 |
| `GAKU_PLUGIN_STATE_ACTIVATED` | `5u` | `activate()` 返回 `GAKU_OK` |
| `GAKU_PLUGIN_STATE_RUNTIME_READY` | `6u` | `onRuntimeReady()` 已完成 |
| `GAKU_PLUGIN_STATE_FAILED` | `7u` | `activate()` 失败或在回调中抛出异常退役 |

---

## 插件元数据与依赖声明（`GakuPluginMetadata.h`）

插件可选择额外导出 `GakuPlugin_QueryMetadata`（签名 `FnGakuPlugin_QueryMetadata`），在不改动原 `GakuPlugin.h` v1 布局的前提下向宿主声明作者、多语言简介、最低宿主版本、依赖图与伴生 DLL。未导出该函数的旧插件仍按原流程正常加载。

```cpp
GakuStatus GakuPlugin_QueryMetadata(
    uint32_t requestedAbi, uint32_t outputSize, GakuPluginMetadataV1* output);
```

- **调用时机与校验规则**：紧接 `GakuPlugin_Query` 在 loader lock 外调用一次；禁止在其中初始化 IL2CPP、UI、Hook 或工作线程。写入 `*output` 前必须先检查 `requestedAbi == GAKU_METADATA_ABI_VERSION`（`1u`）、`output != NULL` 且 `outputSize >= sizeof(GakuPluginMetadataV1)`。
- **内存生命周期**：所有字符串指针与数组指针必须指向静态或模块生命周期存储；宿主会在 `activate()` 或 `FreeLibrary()` 之前拷贝全部字符串与结构内容。

### `GakuPluginMetadataV1` 字段说明

| 字段 | 类型 | 说明 |
|---|---|---|
| `abiVersion` | `uint32_t` | 固定填 `GAKU_METADATA_ABI_VERSION`（`1u`） |
| `structSize` | `uint32_t` | 固定填 `sizeof(GakuPluginMetadataV1)` |
| `originalAuthor` | `const char*` | 原作者名（UTF-8，可选） |
| `maintainerCount` / `maintainers` | `uint32_t` / `const char* const*` | 现任维护者数组（UTF-8，可选；`reserved0` 填 `0`） |
| `subVersion` | `const char*` | 次级/构建展示标签（如 `"build.42"`），**不参与 SemVer 比较** |
| `releaseChannel` | `const char*` | 发布通道展示标签（如 `"stable"`、`"beta"`、`"dev"`） |
| `summary` | `const char*` | 默认单行简介（UTF-8，可选） |
| `localizedSummaryCount` / `localizedSummaries` | `uint32_t` / `const GakuPluginLocalizedTextV1*` | 多语言简介数组，每项含 `languageTag`（如 `"en"`、`"zh-Hans"`、`"zh-Hant"`、`"ja"`）与 `text`（`reserved1` 填 `0`） |
| `repositoryUrl` / `releaseUrl` / `issuesUrl` | `const char*` | 项目仓库、发布页与问题反馈链接（`https://` 或 `http://`） |
| `license` | `const char*` | SPDX 或许可证标识（如 `"MIT"`） |
| `minLoaderVersion` | `const char*` | 最低宿主版本或 SemVer 范围（裸版本号如 `"1.1.0"` 表示 `>=1.1.0`；当前宿主版本宏为 `GAKU_LOADER_VERSION`） |
| `dependencyCount` / `dependencies` | `uint32_t` / `const GakuPluginDependencyV1*` | 依赖与冲突声明数组（`reserved2` 填 `0`） |
| `auxiliaryDllCount` / `auxiliaryDlls` | `uint32_t` / `const char* const*` | 属于本插件的原生伴生 DLL 文件名数组（`reserved3` 填 `0`） |

### `GakuPluginDependencyV1` 与拓扑激活规则

每条依赖声明由 `GakuPluginDependencyV1` 描述：

- `pluginId`：目标插件规范 ID（即 `GakuPluginDescriptor::id`）。
- `displayName`：用于日志与 F1 界面的可读名称（可选）。
- `versionRange`：SemVer 2.0.0 版本约束表达式（如 `">0.2.0-beta.1"`、`">=1.0.0 <2.0.0"`、`^0.2.0`、`~0.2.1`；`"*"`、`NULL` 或 `""` 表示匹配任意版本）。
- `kind`（`GakuPluginDependencyKind`）：
  - `GAKU_DEPENDENCY_REQUIRED`（`1u`）：强依赖，目标缺失、被禁用、不兼容或版本不满足时本插件进入 `DEPENDENCY_ERROR`。
  - `GAKU_DEPENDENCY_OPTIONAL`（`2u`）：可选依赖，目标缺失或被禁用时本插件仍可激活；若目标存在且启用，则检查 `versionRange` 并优先在其之后激活。
  - `GAKU_DEPENDENCY_CONFLICT`（`3u`）：互斥冲突，若目标存在、启用且命中 `versionRange`，则阻止激活。
- `reserved`：保留字段，必须为 `0`。

宿主采用两阶段加载：第一阶段发现并查询所有候选插件的描述符与元数据；第二阶段完成兼容性与依赖边校验，并按拓扑顺序调用 `activate()`（`shutdown()` 仍按严格逆序执行）。若出现仅由 `Optional` 边参与且可通过忽略该可选边打破的环路，宿主会对该 `Optional` 边执行环路降级并记录诊断，避免无谓阻塞启动。

---

## 插件间自定义服务发布与共享

插件不仅能消费宿主服务，还可以作为服务提供方，通过 `host->registerService` 发布自定义函数表供其他 Mod 插件调用：

- **命名规范**：服务名最长 127 字节，**严禁以 `gakuloader.` 前缀命名**（该前缀为宿主官方保留）。
- **容量约束**：函数表大小上限为 64 KiB，全进程最多支持注册 64 个自定义服务。
- **生命周期**：宿主负责服务注册与查表，不接管内部业务数据生命周期。发布服务的插件卸载或退出时，宿主会自动注销其发布的服务表。
- **可选 Web 面板契约 (`include/GakuWebPanel.h`)**：插件可通过 `registerService` 发布 `<pluginId>.web-panel/v1`（`GakuWebPanelServiceV1`），由插件自身提供离线前端资产目录（`GakuWebPanelAssetsV1`）与消息处理回调，宿主仅负责在插件内容区托管 WebView2 与消息转发，详见 [WebView2 构建与打包](../docs/webview2-build.md)。

---

[Wiki 首页](Home.md) · [全部章节](_Sidebar.md) · [项目首页](../README.md)
