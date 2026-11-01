# Plugin API

本页说明插件生命周期的各个回调、执行线程，以及宿主提供的服务接口。

---

## 生命周期回调

插件填写的 `GakuPluginDescriptor` 包含以下回调函数。不需要的回调填 `nullptr` 即可：

### 1. `activate(const GakuHostApi* host, void** pluginState)`
- **运行线程**：Loader 启动线程（在 Windows Loader Lock 外）。
- **说明**：此时 Unity 引擎和 IL2CPP 都还没加载。在这里只能读本地配置、拿宿主服务、注册物理热键。绝对不要在这里解析 IL2CPP 函数或建托管对象。如果在这里返回失败，宿主只会跳过本插件，其他插件继续跑。

### 2. `onRuntimeReady(void* pluginState)`
- **运行线程**：Loader 的 Hook 工作线程。
- **说明**：此时 `GameAssembly.dll` 已经加载，IL2CPP 核心导出函数已就绪。适合用来解析类名、方法指针并安装 Detour Hook。但此时 GC 环境还不完整，依然不要在这里创建托管对象。

### 3. `onTick(void* pluginState, float deltaTime)`
- **运行线程**：Unity 主线程。
- **说明**：每帧调用至多一次。标题界面或无角色场景同样会推进。在它运行之前，主线程任务队列里的任务已经全部跑完。可以在这里安全调用托管对象。

### 4. `onActorTick(void* pluginState, void* activeActor, float deltaTime)`
- **运行线程**：Unity 主线程。
- **说明**：在每个存活角色的原生 `LateUpdate` 执行完后调用。`activeActor` 是借用指针，代表当前角色的 C# 实例地址。**切勿跨帧缓存这个指针**，场景切换后它会失效。

### 5. `onDesktopUi(void* pluginState)`
- **运行线程**：独立的 Desktop UI 线程。
- **说明**：只有在用户按下 F1 打开窗口并在左侧选中了你的插件面板时才会执行。在这里画 ImGui 控件。详见 [界面与 UI](UI.md)。

### 6. `shutdown(void* pluginState)`
- **运行线程**：Unity 主线程。
- **说明**：游戏退出时按插件加载的严格逆序调用。用于停止插件内部的私有线程并释放自建句柄。退出后宿主会自动回收该插件占用的 Hook 租约和主线程任务。

---

## 常用内置服务

在 `activate` 或后续回调中，可以通过 `host->queryService` 获取宿主提供的服务：

```cpp
GakuLogService log{};
host->queryService(host->hostContext, GAKU_SERVICE_LOG, GAKU_SERVICE_VERSION, sizeof(log), &log);
```

### `gakuloader.log`
写日志到 `gakumas-local/gakuloader.log`：
```cpp
log.write(log.context, GAKU_LOG_INFO, "hello from plugin");
```
消息是普通 UTF-8 文本，不是 printf 格式串。

### `gakuloader.runtime`
获取游戏目录与 GameAssembly 句柄：
- `gameRoot`：游戏根目录宽字符绝对路径。
- `localRoot`：`gakumas-local` 宽字符绝对路径。
- `pluginsRoot`：`plugins` 宽字符绝对路径。
- `getGameAssembly()`：获取 `GameAssembly.dll` 模块句柄（就绪前返回 NULL）。

### `gakuloader.main-thread`
投递需要在 Unity 主线程执行的任务：
```cpp
mainThread.enqueue(mainThread.context, [](void* data) {
    // 运行在 Unity 主线程
}, userData);
```
队列上限 1024 个任务。`enqueue` 永远异步执行，不会在调用处同步卡住。

### `gakuloader.file-dialog`
异步调用系统原生文件打开对话框，不会卡死游戏或 UI 帧：
1. 调 `beginOpen()` 发起请求，拿到一个 `ticket`（每个插件同时只能有一个进行中的 ticket）。
2. 在每帧循环或 UI 回调里调 `poll()` 检查结果，状态包括等待中（`PENDING`）、已选中（`ACCEPTED`）、用户取消（`CANCELLED`）、失败（`FAILED`）。
3. 拿到结果后调 `release()` 释放 ticket。

---

## 插件间自定义服务

插件还可以作为服务提供方，通过 `host->registerService` 发布自己的函数表供其他插件调用：
- 服务名不能以 `gakuloader.` 开头（该前缀为宿主保留）。
- 服务名最长 127 字节，表最大 64 KiB，全进程最多注册 64 个自定义服务。
- 宿主只负责复制和查表，不接管指针指向的数据。发布服务的插件退出时，宿主会自动撤销它发布的服务。

---

[Wiki 首页](Home.md) · [全部章节](_Sidebar.md) · [项目首页](../README.md)
