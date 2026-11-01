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
