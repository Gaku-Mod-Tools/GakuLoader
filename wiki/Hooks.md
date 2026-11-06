# Hook 机制与 Actor Entry

本页说明 GakuLoader 的 MinHook 独占租约机制、批量安装方式、进程生命周期 Hook 保留，以及共享角色更新入口（Actor Entry）的工作原理。

---

## MinHook 独占租约 (`gakuloader.hooks`)

GakuLoader 内部使用 MinHook 提供原生代码 Detour 能力。为了避免不同插件争抢同一目标函数导致死锁或逻辑覆盖，所有 Hook 均采用**独占租约**机制：

```cpp
GakuHookService hooks{};
host->queryService(host->hostContext, GAKU_SERVICE_HOOKS, GAKU_SERVICE_VERSION, sizeof(hooks), &hooks);

void* originalFunc = nullptr;
const auto status = hooks.acquire(hooks.context, targetFunc, myDetour, &originalFunc);
```

### 独占规则
- **单一持有者**：目标函数内存地址全进程仅允许被一个插件持有。若已被其他插件占用，`acquire` 会返回 `GAKU_E_CONFLICT`。
- **生命周期安全**：禁止插件自行调用 `MH_ALL_HOOKS` 或 `MH_Uninitialize` 等全局操作。插件正常退役时，宿主会自动回收并停用该插件持有的 Hook 租约；分配的跳板（Trampoline）内存会维持保留直至进程结束，防止其他线程处于跳板中时发生崩溃。

### 批量安装 (`acquireBatch`)
当插件需要一次性安装大量 Hook 时（例如汉化插件挂接数十个文本解析函数），逐个调用 `acquire` 会因多次挂起全进程线程而产生明显的卡顿。使用 `acquireBatch` 可以在单次线程挂起窗口中完成 1 至 256 个 Hook 的集中使能：

```cpp
GakuHookRequest requests[2] = {
    { target1, detour1, &original1 },
    { target2, detour2, &original2 }
};
hooks.acquireBatch(hooks.context, 2, requests);
```

---

## 进程生命周期 Hook (`gakuloader.process-lifetime-hooks`)

对于需要跨插件生命周期常驻的关键底层拦截（例如崩溃防御、跨插件基础服务），插件可通过 `include/GakuProcessLifetimeHooks.h` 查询 `gakuloader.process-lifetime-hooks` 扩展服务。

调用 `retainOwnedHooks` 可以将本插件已申请并处于使能状态的 Hook 提升为**进程生命周期级别**。提升后的 Hook 在所属插件退役或宿主退出阶段不会被自动卸载，Detour 将持续生效直至进程完全终止。

---

## 共享 Actor 更新入口 (`gakuloader.actor-entry`)

在《学园偶像大师》中，角色的动作姿态、骨骼变换与视线跟随由 `CampusActorController::LateUpdate`（及 `CampusVirtualActorController`）驱动。

该函数由 GakuLoader 宿主独占挂接。插件若直接对其调用 `acquire`，会直接返回 `GAKU_E_CONFLICT`。

需要介入角色每帧更新的插件（例如姿态捕捉、骨骼重定向或字幕对齐），必须通过 `gakuloader.actor-entry` 服务注册为参与者：

```cpp
GakuActorEntryService actorEntry{};
host->queryService(host->hostContext, GAKU_SERVICE_ACTOR_ENTRY, GAKU_SERVICE_VERSION, sizeof(actorEntry), &actorEntry);

GakuActorEntryBinding binding{};
binding.abiVersion = GAKU_ABI_VERSION;
binding.structSize = sizeof(binding);
binding.phase = GAKU_ACTOR_ENTRY_AFTER;   // 角色原生更新后，或 GAKU_ACTOR_ENTRY_BEFORE（原生更新前）
binding.name = "my-plugin.pose";
binding.callback = [](void* userData, void* actor) {
    // 运行在 Unity 主线程，actor 为当前角色的原生实例指针
};
binding.userData = state;

actorEntry.registerEntry(actorEntry.context, &binding);
```

### 执行时序链条
每次游戏执行 `LateUpdate` 时，宿主按以下严格顺序驱动：
1. **BEFORE 参与者链**：按注册顺序依次调用所有 `GAKU_ACTOR_ENTRY_BEFORE` 回调。
2. **原生 `LateUpdate`**：执行原版游戏的角色更新（执行且仅执行一次）。
3. **AFTER 参与者链**：按注册顺序依次调用所有 `GAKU_ACTOR_ENTRY_AFTER` 回调。
4. **宿主生命周期**：统一推进 `onTick` 与 `onActorTick` 回调。

### 异常隔离与互斥保护
- **异常隔离**：若某个参与者的回调函数抛出异常，宿主会自动捕获并记录错误日志，不会导致游戏崩溃或影响后续其他参与者执行。
- **互斥保护**：参与者回调与桌面 UI 回调（`onDesktopUi`）受宿主内部互斥锁保护。UI 正在绘制修改配置时，参与者不会读到中间态；参与者更新时 UI 也不会并发改写，插件无需自行加锁同步简单共享变量。

---

## 退出故障二分排查标记

如果在排查游戏退出卡死时需要确认故障是否由插件的 Hook 引起，可以在 `GakuLoader/` 目录下放置一个名为 `gml_disable_hooks.txt` 的空文本文件（或设置系统环境变量 `GML_DISABLE_HOOKS=1`）。宿主在启动时会跳过所有业务 MinHook 的挂接，仅保留退出防护机制，用于快速定位问题源头。

---

[Wiki 首页](Home.md) · [全部章节](_Sidebar.md) · [项目首页](../README.md)
