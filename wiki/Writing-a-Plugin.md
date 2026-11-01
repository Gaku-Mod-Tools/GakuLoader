# Writing a Plugin

本页说明如何使用 `include/GakuPlugin.h` 编写一个 GakuLoader 插件。

---

## 插件基本规则

1. **只导出一个函数**：插件 DLL 只需要导出 `GakuPlugin_Query`。不要导出非标准的业务符号。
2. **纯 C 接口**：接口上只传基础类型和结构体指针，不要把 `std::string`、`std::vector` 之类的 C++ 容器传过边界。
3. **内存谁分配谁释放**：不要跨 DLL 释放另一方分配的内存。
4. **异常不要抛给宿主**：插件回调内部有任何可能抛异常的代码，必须在自己的回调里 `try ... catch` 捕获。异常漏出来会导致插件被宿主标记为失效（Retire）或引起崩溃。
5. **在对的时机做对的事**：`activate` 时游戏引擎和 IL2CPP 都还没初始化，绝对不要在这时候调 IL2CPP 函数或建托管对象。

---

## 最小可用插件

以下代码取自仓库 `examples/minimal-plugin/MinimalPlugin.cpp`：

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

---

## 独立构建与离线验证

从仓库根目录执行，只需要 VS 2022 / v143 或 VS 2026 / v145 的 MSVC 和 Windows SDK：

```powershell
.\examples\minimal-plugin\build.ps1
.\examples\minimal-plugin\verify.ps1
```

此入口不要求先构建 Loader 或 MinGW 夹具。验证器在合成宿主中加载编译后的 DLL，
检查 `GakuPlugin_Query`、激活与生命周期日志，不启动游戏。
构建工具集可以用 `-PlatformToolset v143` 或 `v145` 指定；`-Configuration Debug` 选择调试版本。

真正接入游戏时，先退出游戏，再按产品安装说明放置 `MinimalPlugin.dll`，
确认 `gakumas-local/gakuloader.log` 中出现 `example.minimal activated`。
离线验证不代表 actor、UI 或游戏画面已经验收。

## 编译建议

- **平台**：Windows x64 DLL。
- **运行时库**：建议使用 `/MT`（静态链接 CRT），避免因为使用者机器上缺少特定 VC++ Redistributable 导致插件加载失败。
- **输出位置**：编译完成后把 DLL 放进 `gakumas-local/plugins/` 目录即可。
- **头文件更新**：不要保留历史遗留的旧版接口（`GmlPlugin_Query`、`GakumasModPlugin.h`）。旧接口已经被彻底删除，加载器只认导出 `GakuPlugin_Query` 的插件。

---

[Wiki 首页](Home.md) · [全部章节](_Sidebar.md) · [项目首页](../README.md)
