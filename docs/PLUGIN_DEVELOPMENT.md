# 开发 GakuLoader 插件

新插件使用 [include/GakuPlugin.h](../include/GakuPlugin.h)，只导出 `GakuPlugin_Query`。Loader 拥有进程生命周期、主线程投递、hook 租约、热键、Desktop UI 和配置；插件拥有自己的业务。旧 GML ABI 已整体删除，没有兼容层，`HOST_MIGRATION.md` 只是第一阶段的历史记录。

## 最小插件

下面的代码就是 `examples/minimal-plugin/MinimalPlugin.cpp`；`tools/test-docs.ps1` 校验文档与文件逐字一致，并通过 MSVC 编译和检查导出符号。

```cpp
// Minimal GakuLoader plugin. It negotiates ABI v1, writes to the loader log and
// keeps a frame counter. It is compiled by tools/test-docs.ps1 so the example in
// docs/PLUGIN_DEVELOPMENT.md cannot drift away from the header.
#include "GakuPlugin.h"

namespace {
GakuLogService g_log{};

void Log(GakuLogLevel level, const char* message) {
    if (g_log.write) g_log.write(g_log.context, level, message);
}

GakuStatus Activate(const GakuHostApi* host, void** pluginState) {
    if (!host || !pluginState) return GAKU_E_INVALID_ARGUMENT;
    if (host->abiVersion != GAKU_ABI_VERSION ||
        host->structSize < sizeof(GakuHostApi) || !host->queryService) {
        return GAKU_E_VERSION;
    }
    const auto status = host->queryService(host->hostContext, GAKU_SERVICE_LOG,
        GAKU_SERVICE_VERSION, sizeof(g_log), &g_log);
    if (status != GAKU_OK) return status;
    // Activation runs before Unity exists. Only native work belongs here.
    *pluginState = nullptr;
    Log(GAKU_LOG_INFO, "example.minimal activated");
    return GAKU_OK;
}

void RuntimeReady(void*) {
    Log(GAKU_LOG_INFO, "example.minimal runtime ready");
}

unsigned long long g_ticks = 0;
void Tick(void*, float) {
    ++g_ticks;
}

void Shutdown(void*) {
    Log(GAKU_LOG_INFO, "example.minimal shutdown");
}
} // namespace

extern "C" GAKU_PLUGIN_EXPORT GakuStatus GakuPlugin_Query(
    uint32_t requestedAbi, uint32_t outputSize, GakuPluginDescriptor* output) {
    if (requestedAbi != GAKU_ABI_VERSION) return GAKU_E_VERSION;
    if (!output) return GAKU_E_INVALID_ARGUMENT;
    if (outputSize < sizeof(GakuPluginDescriptor)) return GAKU_E_SIZE;

    GakuPluginDescriptor descriptor{};
    descriptor.abiVersion = GAKU_ABI_VERSION;
    descriptor.structSize = sizeof(GakuPluginDescriptor);
    descriptor.id = "example.minimal";
    descriptor.name = "Minimal Example";
    descriptor.version = "1.0.0";
    descriptor.activate = Activate;
    descriptor.onRuntimeReady = RuntimeReady;
    descriptor.onTick = Tick;
    descriptor.shutdown = Shutdown;
    // Nothing is written before every check above passed.
    *output = descriptor;
    return GAKU_OK;
}
```

Windows x64 DLL 放在 `gakumas-local/plugins/`。几条硬性规则：

- `GakuPlugin_Query` 只填描述符。不要在这里做 IL2CPP、UI、hook 或线程初始化，也不要返回指向临时缓冲的字符串。
- 先验证 `requestedAbi` 和 `outputSize`，再写 `*output`；尺寸不足时必须不写。描述符的 `abiVersion` / `structSize` 固定为 `GAKU_ABI_VERSION` / `sizeof(GakuPluginDescriptor)`。
- ABI 上不传 STL 对象、不跨 CRT 释放内存、不让异常越过回调边界。Loader 的 C++ `catch` 不等于能隔离原生访问违规。
- `id` 在进程内唯一；`GakuPlugin_Query` 返回失败只禁用这一个插件。

## 独立构建与首次验证

安装 VS 2022 / MSVC v143 或 VS 2026 / MSVC v145 的 C++ 桌面开发工作负载及 Windows SDK，
在仓库根目录执行：

```powershell
.\examples\minimal-plugin\build.ps1
.\examples\minimal-plugin\verify.ps1
```

不依赖 Loader 的构建产物、MinGW 或游戏。验证器在合成宿主中加载 DLL，检查 ABI 查询、
激活与生命周期日志。真正放入游戏时，按 [安装指南](../wiki/Installation.md) 操作，
从 `gakumas-local/gakuloader.log` 确认 `example.minimal activated`。

## 生命周期与线程

| 回调 | 线程与时机 | 允许做什么 |
|---|---|---|
| `GakuPlugin_Query` | Loader 启动线程，loader lock 外 | 只填描述符；宿主先 `LoadLibraryExW`，再在此协商 ABI |
| `activate(host, &state)` | Loader 启动线程，早于 runtime-ready | 连接服务、读配置、装进程级 hook；**不调用 IL2CPP、不建托管对象** |
| `onRuntimeReady(state)` | 可能是 loader worker 线程 | 解析导出/元数据、安装 detour；仍**不创建托管对象** |
| `onTick(state, dt)` | Unity 主线程，每帧至多一次 | 帧级工作；无 actor 的标题/登录界面同样执行 |
| `onActorTick(state, actor, dt)` | Unity 主线程，每个存活 actor 的原生 `LateUpdate` 之后 | actor 级工作；`actor` 是借用指针，跨场景不可缓存 |
| `onDesktopUi(state)` | Desktop UI 线程，已开始 ImGui frame | 只在选中面板的内容区绘制 |
| `shutdown(state)` | Unity 主线程，逆序 | 停止自有线程、释放自有资源 |

`onTick` / `onActorTick` 之前，主线程队列里的任务已经跑完。一次渲染帧可能有多个 actor 回调；`dt` 是宿主测得并钳制的回调间隔，不是 `Time.deltaTime`。回调必须短：不要在回调里等待另一个需要这些回调推进的线程。

## 服务

服务按**名字 + 精确版本**查询，table 以 `{abiVersion, structSize}` 开头。查询失败不修改输出；未知服务返回 `GAKU_E_UNAVAILABLE`，版本不符返回 `GAKU_E_VERSION`，缓冲区过小返回 `GAKU_E_SIZE`。`context` 是 owner token，不能转交或伪造。

```cpp
GakuLogService log{};
const auto status = host->queryService(host->hostContext, GAKU_SERVICE_LOG,
    GAKU_SERVICE_VERSION, sizeof(log), &log);
```

| 服务 | 职责 |
|---|---|
| `gakuloader.log` | UTF-8 纯文本写入 Loader 私有日志；不是 printf 格式串，也不走 Unity 日志 |
| `gakuloader.runtime` | 游戏根/本地/插件目录（绝对宽字符路径，进程生命周期有效）与 `getGameAssembly()`；后者在 GameAssembly 加载前为 NULL |
| `gakuloader.main-thread` | 有界 FIFO（1024）；`enqueue` 永不同步执行，shutdown 丢弃未执行任务；`isMainThread` 供线程判断 |
| `gakuloader.hooks` | 独占 detour 租约；冲突返回 `GAKU_E_CONFLICT`。`acquireBatch` 一次发布 1..256 个 original。不要调用 `MH_ALL_HOOKS` / `MH_Uninitialize` |
| `gakuloader.process-lifetime-hooks` | 可选 v1 扩展，声明在 `include/GakuProcessLifetimeHooks.h`；原子保留本 owner 已启用的指定 Hook，退役及退出收尾后继续生效 |
| `gakuloader.desktop-ui` | 在 `onDesktopUi` 内校验 ImGui 指纹并绑定 context/allocator |
| `gakuloader.desktop-window` | 宿主窗口 HWND、异步 `requestShow()`、`presentationKind`；不可销毁或 subclass |
| `gakuloader.file-dialog` | `beginOpen` 立即复制参数返回 ticket；UI 帧里 `poll`；`release` 释放。每 owner 一个未释放 ticket |
| `gakuloader.hotkeys` | 物理键边沿分发，只在主线程回调；同一组合被两个 owner 认领时两者一起失效并报冲突；F1 归 Loader |
| `gakuloader.actor-entry` | 共享 actor 更新入口的 before/after 参与者，见下节 |

自定义服务用 `registerService(hostContext, name, tableSize, table)` 发布：宿主复制字节，不接管指针指向的资源；`gakuloader.` 前缀保留；名字最长 127 字节、进程最多 64 个自定义服务、table 最大 64 KiB。owner 退役（shutdown，或某个回调抛异常导致该 owner 被退役）时宿主一并撤下它的服务、任务、热键、participant 和 hook 租约，provider 不需要自己反注册。没有依赖排序，不要依赖扫描顺序。

需要在 owner 退役后持续工作的保护 Hook，可查询 `GakuProcessLifetimeHooksV1`，先固定插件模块和 detour 状态的进程生命周期，再调用 `retainOwnedHooks(context, count, targets)`。目标必须全部属于本 owner、已启用、非空且互不重复（1..256 项）；任一目标校验失败，整批均不提升。成功后这些 Hook 不随 owner 退役或退出批次停用，插件 `release()` 返回 `GAKU_E_CONFLICT`；此提升不可撤销，detour 和 trampoline 状态必须一直保留。普通 `GakuHookService` v1 布局未变，旧宿主缺少此可选服务时返回 `GAKU_E_UNAVAILABLE`。

## 共享 actor 入口

`CampusActorController::LateUpdate` 全进程只有一个 detour，属于 Loader。需要在这个入口工作的插件不要 `acquire` 该地址（会冲突），而是注册参与者：

```cpp
GakuActorEntryBinding binding{};
binding.abiVersion = GAKU_ABI_VERSION;
binding.structSize = sizeof(binding);
binding.phase = GAKU_ACTOR_ENTRY_BEFORE;   // 或 GAKU_ACTOR_ENTRY_AFTER
binding.name = "example.subtitles";
binding.callback = [](void* userData, void* actor) { /* Unity main thread */ };
binding.userData = state;
const auto status = actorEntry.registerEntry(actorEntry.context, &binding);
```

原生函数每次调用恰好执行一次，顺序是：全部 before 参与者（按注册序）→ 原生 `LateUpdate` → 全部 after 参与者 → Loader 的 `onTick`/`onActorTick`。每个 owner 每个 phase 只有一个绑定，重注册会原位替换；回调内重注册从下一次调用生效。参与者回调不要调用原函数，也不要假设它已经跑过。

参与者与 `onTick` / `onDesktopUi` 共用同一把宿主回调门：同一 phase 的全部参与者在该门下连续执行，桌面 UI 帧不会插进来，所以参与者可以读插件里同时被 GUI 复选框写的字段（例如自由相机开关），不需要自己加锁。原生 `LateUpdate` 不在这把门里——它在两个 phase 之间、门之外执行。门保证的只是「描述符回调与 actor 参与者互斥」；它不覆盖插件自己安装的业务 detour、worker 线程，也不覆盖原生函数内部再次进入的插件代码。这些路径若也写同一份状态，锁仍然要你自己加。

## Desktop UI

`onDesktopUi` 在宿主窗口的内容区绘制，宿主已经 `NewFrame`。不要创建/销毁 ImGui context，不要调用 `NewFrame` / `Render` / `Present`，也不要混用私有字体或 allocator。若插件静态链接自己的 ImGui，必须与 `deps/imgui/` 同一 revision 和 `imconfig.h`，并在回调内绑定：

```cpp
GakuImGuiAbi abi{IMGUI_VERSION_NUM, sizeof(ImGuiIO), sizeof(ImGuiStyle),
    sizeof(ImDrawVert), sizeof(ImDrawIdx), sizeof(ImWchar)};
GakuDesktopUiBinding binding{};
if (desktop.bind(&abi, sizeof(binding), &binding) != GAKU_OK) return;
ImGui::SetAllocatorFunctions(binding.alloc, binding.free, binding.allocatorUserData);
ImGui::SetCurrentContext(static_cast<ImGuiContext*>(binding.imguiContext));
```

`bind` 只在当前选中插件的桌面回调期间成功，ABI 不匹配时不返回 context。控件触发的 Unity 修改应投递到 `gakuloader.main-thread`；不要在回调里等待原生模态窗口或主线程结果。

## 退出与模块

正常退出在主线程收尾：先停 Desktop presentation，再逆序 `shutdown`，然后 `RetireOwner` 回收该插件普通 hook / 主线程任务 / 热键 / 对话框。已提升的进程生命周期 Hook 继续工作。模块和 trampoline 保留到进程退出，不支持 hot unload/restart；不要在 `DllMain` 里调用插件、等待线程或释放 D3D。强制结束或崩溃时不能保证 `shutdown`，不要把唯一的一份数据只放在退出回调里保存。

诊断边界（`DebugLogHandler.Internal_Log` / `Internal_LogException`）是 Loader 自己的兼容策略：它不转发官方 sink、只保留有界私有细节。安装失败只记 WARN，不阻止插件激活。

## 验证

```powershell
# 通过 MSVC 编译本文档附带的最小插件并检查导出
.\tools\test-docs.ps1

# 完整宿主契约需先构建 MinGW 夹具，详见 Building
# 激活/生命周期/服务/hook/热键/退出
.\tests\test-host.ps1
.\tools\test-package.ps1
```

本地包检查不部署；可选 MinGW 兼容矩阵的依赖见 [Building](../wiki/Building.md)。

这些测试不替代实机验收：插件是否真的拿到 actor、画面/字幕/输入是否正确，仍要在游戏里看。
