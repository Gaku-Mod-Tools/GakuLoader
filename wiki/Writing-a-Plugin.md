# 编写插件

本页说明如何使用 `include/GakuPlugin.h` 从零开始编写并验证一个符合 C-ABI v1 规范的 GakuLoader 插件。

---

## 插件核心规则

1. **单一导出函数**：插件 DLL 仅需导出 `GakuPlugin_Query` 一个入口，不需要导出额外的内部符号。
2. **纯 C ABI 边界**：跨 DLL 调用只传递基础数值类型和普通 C 结构体指针，严禁将 `std::string` 或 `std::vector` 等 STL 容器跨边界传递。
3. **内存谁分配谁释放**：插件内分配的内存由插件自身释放，宿主分配的资源由宿主回收。
4. **内部捕获异常**：插件的所有回调函数必须在内部使用 `try ... catch` 捕获异常，绝不能让 C++ 异常泄漏到宿主或 Unity 引擎中。
5. **遵循时序约束**：在 `activate` 阶段，Unity 引擎与 IL2CPP 尚未完成初始化，此时只能执行本地配置读取与服务注册，禁止调用托管对象或解析 IL2CPP 函数。

---

## 插件开发五步实战

### 第一步：创建工程并引入头文件
创建一个 Windows x64 DLL 动态链接库项目，将 GakuLoader 仓库中的 `include/GakuPlugin.h` 复制到工程的头文件目录中引用。

### 第二步：定义插件状态与生命周期函数
实现插件所需的生命周期回调，未使用的回调在描述符中置为 `nullptr` 即可。

### 第三步：编写导出函数 `GakuPlugin_Query`
实现并导出标准查询函数，完成 ABI 版本协商与元数据填写。

### 第四步：编译为 x64 DLL
使用 MSVC 编译为 64 位 DLL。推荐在项目属性中使用 `/MT`（静态链接 CRT），避免因用户电脑缺少 VC++ 运行时分发包而导致加载失败。

### 第五步：部署到游戏插件目录并验证
将编译产出的 DLL 复制到 `<游戏根目录>/GakuLoader/plugins/` 目录下，启动游戏，在 `GakuLoader/Logs/gakuloader.log` 中确认加载与激活信息。

---

## 最小可用插件代码

以下代码源自仓库 `examples/minimal-plugin/MinimalPlugin.cpp`，展示了协商 ABI v1、写入日志和记录帧计数的完整流程：

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

在具备 Visual Studio 2022 (MSVC v143) 或 Visual Studio 2026 (MSVC v145) 的开发环境中，可直接在仓库根目录下运行 PowerShell 脚本进行独立编译与模拟宿主验证，无需启动游戏：

```powershell
# 编译示例插件
.\examples\minimal-plugin\build.ps1

# 运行离线验证（在合成宿主中加载并测试 ABI 握手、激活与日志输出）
.\examples\minimal-plugin\verify.ps1
```

可通过 `-PlatformToolset v143` 或 `v145` 指定工具集，或使用 `-Configuration Debug` 构建调试版本。

---

[Wiki 首页](Home.md) · [全部章节](_Sidebar.md) · [项目首页](../README.md)
