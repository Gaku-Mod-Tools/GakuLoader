// Deliberately uses only the original ABI header: paths is an optional service.
#include "GakuPlugin.h"

#ifndef PATHS_FIXTURE_VARIANT
#define PATHS_FIXTURE_VARIANT 1
#endif

namespace {
#if PATHS_FIXTURE_VARIANT == 1
constexpr const char* kId = "paths.alpha";
#elif PATHS_FIXTURE_VARIANT == 2
constexpr const char* kId = "paths.beta";
#elif PATHS_FIXTURE_VARIANT == 3
constexpr const char* kId = "Paths.Alpha"; // valid old ABI, invalid paths namespace
#else
#error Unknown paths fixture variant
#endif
GakuStatus Activate(const GakuHostApi* host, void** state) {
    if (!host || !state || host->abiVersion != GAKU_ABI_VERSION ||
        host->structSize < sizeof(GakuHostApi)) return GAKU_E_INVALID_ARGUMENT;
    *state = nullptr;
    return GAKU_OK;
}
}

extern "C" GAKU_PLUGIN_EXPORT GakuStatus GakuPlugin_Query(
    uint32_t version, uint32_t outputSize, GakuPluginDescriptor* output) {
    if (version != GAKU_ABI_VERSION) return GAKU_E_VERSION;
    if (!output) return GAKU_E_INVALID_ARGUMENT;
    if (outputSize < sizeof(*output)) return GAKU_E_SIZE;
    GakuPluginDescriptor value{};
    value.abiVersion = GAKU_ABI_VERSION;
    value.structSize = sizeof(value);
    value.id = kId;
    value.name = kId;
    value.version = "1.0.0";
    value.activate = Activate;
    *output = value;
    return GAKU_OK;
}
