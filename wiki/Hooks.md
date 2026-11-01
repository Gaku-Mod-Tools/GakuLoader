# Hooks

本页说明如何在插件中使用 GakuLoader 的 Hook 服务，以及如何介入角色更新入口。

---

## MinHook 独占租约 (`gakuloader.hooks`)

GakuLoader 内部使用 MinHook 提供 Hook 能力。为了防止多个插件抢同一个函数导致死循环或互相覆盖，所有 Hook 采用独占租约：

```cpp
GakuHookService hooks{};
host->queryService(host->hostContext, GAKU_SERVICE_HOOKS, GAKU_SERVICE_VERSION, sizeof(hooks), &hooks);

void* originalFunc = nullptr;
const auto status = hooks.acquire(hooks.context, targetFunc, myDetour, &originalFunc);
```

### 独占规则
- 一个目标函数地址全进程**只允许被一个插件持有**。
- 如果目标地址已经被其他插件 Hook 了，`acquire` 会直接返回 `GAKU_E_CONFLICT`。
- 不要私自调用 `MH_ALL_HOOKS` 或 `MH_Uninitialize`，这些属于进程共享设施，插件退出时宿主会自动停用该插件申请的 Hook。

### 批量 Hook (`acquireBatch`)
如果要安装大量 Hook（例如汉化插件同时 Hook 几十上百个函数），逐个调 `acquire` 会导致每次都挂起全进程线程，加起来会卡顿数秒。
使用 `acquireBatch` 可以在单次挂起窗口中一次性全部使能：

```cpp
GakuHookRequest requests[2] = {
    { target1, detour1, &original1 },
    { target2, detour2, &original2 }
};
hooks.acquireBatch(hooks.context, 2, requests);
```

---

## 共享 Actor 更新入口 (`gakuloader.actor-entry`)

在《学园偶像大师》中，角色的姿态、骨骼与镜头跟随主要由 `CampusActorController::LateUpdate` 驱动。

这个函数全进程**只允许由 GakuLoader 自身挂接一个独占 Detour**。如果插件直接对它调 `acquire`，必定会返回 `GAKU_E_CONFLICT`。

如果你需要在角色每帧更新时插入逻辑（例如读取或修改骨骼姿态、同步字幕跟随），必须通过 `gakuloader.actor-entry` 注册为参与者：

```cpp
GakuActorEntryService actorEntry{};
host->queryService(host->hostContext, GAKU_SERVICE_ACTOR_ENTRY, GAKU_SERVICE_VERSION, sizeof(actorEntry), &actorEntry);

GakuActorEntryBinding binding{};
binding.abiVersion = GAKU_ABI_VERSION;
binding.structSize = sizeof(binding);
binding.phase = GAKU_ACTOR_ENTRY_AFTER;   // 或者 GAKU_ACTOR_ENTRY_BEFORE
binding.name = "my-plugin.pose";
binding.callback = [](void* userData, void* actor) {
    // 运行在 Unity 主线程，actor 是当前角色的原始指针
};
binding.userData = state;

actorEntry.registerEntry(actorEntry.context, &binding);
```

### 执行时序
每次游戏调用 `LateUpdate` 时，宿主按以下固定顺序驱动：
1. 全部 `BEFORE` 参与者（按注册顺序执行）
2. 原生 `LateUpdate`（恰好执行一次）
3. 全部 `AFTER` 参与者（按注册顺序执行）
4. Loader 统一推进 `onTick` 与 `onActorTick`

### 自动互斥保护
参与者代码与桌面 UI 回调（`onDesktopUi`）都在宿主的互斥门控保护下运行。也就是说，当参与者在读变量时，桌面 UI 线程不可能插进来写；反过来 UI 正在绘制修改数据时，参与者也不会并发读到一半。因此你可以在参与者里直接读取被 UI 复选框修改的开关变量，无需自己额外加互斥锁。

---

[Wiki 首页](Home.md) · [全部章节](_Sidebar.md) · [项目首页](../README.md)
