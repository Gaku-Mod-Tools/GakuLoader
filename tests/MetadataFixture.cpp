#include "GakuPluginMetadata.h"
#include <cstring>
#include <string>
#include <windows.h>

#ifndef METADATA_FIXTURE_VARIANT
#define METADATA_FIXTURE_VARIANT 1
#endif

#if METADATA_FIXTURE_VARIANT == 6

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        wchar_t path[MAX_PATH]{};
        const DWORD len = GetModuleFileNameW(module, path, MAX_PATH);
        if (len > 0 && len < MAX_PATH) {
            std::wstring marker(path, len);
            const size_t slash = marker.find_last_of(L"\\/");
            marker = (slash == std::wstring::npos ? std::wstring() : marker.substr(0, slash + 1)) + L"aux-marker.loaded";
            const HANDLE file = CreateFileW(marker.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                const char payload[] = "loaded\n";
                DWORD written = 0;
                WriteFile(file, payload, sizeof(payload) - 1, &written, nullptr);
                CloseHandle(file);
            }
        }
    }
    return TRUE;
}

extern "C" GAKU_PLUGIN_EXPORT uint32_t GakuAuxMarker_Dummy(void) {
    return 1u;
}

#else

namespace {

struct MetaBaseServiceV1 {
    GakuServiceHeader header;
    uint32_t magic;
};

constexpr const char* kMetaBaseServiceName = "meta.base/v1";
constexpr uint32_t kMetaBaseMagic = 0x42415345u;

#if METADATA_FIXTURE_VARIANT == 1
constexpr const char* kId = "meta.base";
constexpr const char* kName = "Metadata Base";
constexpr const char* kVersion = "0.2.1";

const char* const kMaintainers[] = {"Maintainer One", "Maintainer Two"};
const GakuPluginLocalizedTextV1 kLocalizedSummaries[] = {
    {"en", "English base summary"},
    {"zh-Hans", "\xE7\xAE\x80\xE4\xBD\x93\xE4\xB8\xAD\xE6\x96\x87\xE5\x9F\xBA\xE7\xA1\x80\xE6\x91\x98\xE8\xA6\x81"},
    {"ja", "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE3\x83\x99\xE3\x83\xBC\xE3\x82\xB9\xE6\xA6\x82\xE8\xA6\x81"},
};
const char* const kAuxiliaryDlls[] = {"ini-aux.dll"};

GakuStatus Activate(const GakuHostApi* host, void** state) {
    if (!host || !state || host->abiVersion != GAKU_ABI_VERSION ||
        host->structSize < sizeof(GakuHostApi)) return GAKU_E_INVALID_ARGUMENT;
    *state = nullptr;

    const MetaBaseServiceV1 service{{1u, sizeof(MetaBaseServiceV1)}, kMetaBaseMagic};
    if (!host->registerService) return GAKU_E_UNAVAILABLE;
    const GakuStatus regStatus = host->registerService(host->hostContext, kMetaBaseServiceName,
        sizeof(service), &service);
    if (regStatus != GAKU_OK) return regStatus;

    GakuLogService log{};
    if (!host->queryService ||
        host->queryService(host->hostContext, GAKU_SERVICE_LOG, GAKU_SERVICE_VERSION,
            sizeof(log), &log) != GAKU_OK ||
        !log.write) {
        return GAKU_E_UNAVAILABLE;
    }
    log.write(log.context, GAKU_LOG_INFO, "base ready");
    return GAKU_OK;
}

GakuStatus FillMetadata(GakuPluginMetadataV1* output) {
    output->originalAuthor = "Original Base Author";
    output->maintainerCount = 2u;
    output->maintainers = kMaintainers;
    output->subVersion = "build.42";
    output->releaseChannel = "beta";
    output->summary = "Default base summary";
    output->localizedSummaryCount = 3u;
    output->localizedSummaries = kLocalizedSummaries;
    output->repositoryUrl = "https://example.com/meta-base";
    output->releaseUrl = "https://example.com/meta-base/releases";
    output->issuesUrl = "https://example.com/meta-base/issues";
    output->license = "MIT";
    output->auxiliaryDllCount = 1u;
    output->auxiliaryDlls = kAuxiliaryDlls;
    return GAKU_OK;
}

#elif METADATA_FIXTURE_VARIANT == 2
constexpr const char* kId = "meta.consumer";
constexpr const char* kName = "Metadata Consumer";
constexpr const char* kVersion = "0.1.0";

const GakuPluginDependencyV1 kDependencies[] = {
    {"meta.base", "Metadata Base", ">0.2.0-beta.1", GAKU_DEPENDENCY_REQUIRED, 0u},
    {"meta.missing", "Missing Optional", ">=1.0.0", GAKU_DEPENDENCY_OPTIONAL, 0u},
    {"meta.opt_cycle_a", "Opt Cycle A", ">=0.1.0", GAKU_DEPENDENCY_OPTIONAL, 0u},
};

GakuStatus Activate(const GakuHostApi* host, void** state) {
    if (!host || !state || host->abiVersion != GAKU_ABI_VERSION ||
        host->structSize < sizeof(GakuHostApi) || !host->queryService) {
        return GAKU_E_INVALID_ARGUMENT;
    }
    *state = nullptr;

    MetaBaseServiceV1 baseService{};
    if (host->queryService(host->hostContext, kMetaBaseServiceName, 1u,
            sizeof(baseService), &baseService) != GAKU_OK) {
        return GAKU_E_UNAVAILABLE;
    }
    if (baseService.header.abiVersion != 1u ||
        baseService.header.structSize != sizeof(MetaBaseServiceV1) ||
        baseService.magic != kMetaBaseMagic) {
        return GAKU_E_INTERNAL;
    }

    GakuPluginsServiceV1 plugins{};
    if (host->queryService(host->hostContext, GAKU_SERVICE_PLUGINS, GAKU_PLUGINS_SERVICE_VERSION,
            sizeof(plugins), &plugins) != GAKU_OK ||
        !plugins.getHostInfo || !plugins.findPlugin || !plugins.getPluginAt) {
        return GAKU_E_UNAVAILABLE;
    }

    GakuHostInfoV1 hostInfo{};
    if (plugins.getHostInfo(sizeof(hostInfo), &hostInfo) != GAKU_OK ||
        hostInfo.abiVersion != GAKU_PLUGINS_SERVICE_VERSION ||
        hostInfo.structSize != sizeof(GakuHostInfoV1) ||
        hostInfo.pluginAbiVersion != GAKU_ABI_VERSION ||
        hostInfo.pluginCount < 5u ||
        std::strcmp(hostInfo.loaderVersion, GAKU_LOADER_VERSION) != 0) {
        return GAKU_E_INTERNAL;
    }

    GakuPluginSummaryV1 baseSummary{};
    if (plugins.findPlugin("meta.base", sizeof(baseSummary), &baseSummary) != GAKU_OK ||
        baseSummary.state != GAKU_PLUGIN_STATE_ACTIVATED ||
        baseSummary.hasMetadata != 1u ||
        std::strcmp(baseSummary.id, "meta.base") != 0 ||
        std::strcmp(baseSummary.version, "0.2.1") != 0 ||
        std::strcmp(baseSummary.subVersion, "build.42") != 0) {
        return GAKU_E_INTERNAL;
    }

    GakuPluginSummaryV1 missingSummary{};
    if (plugins.findPlugin("meta.missing", sizeof(missingSummary), &missingSummary) != GAKU_OK ||
        missingSummary.state != GAKU_PLUGIN_STATE_NOT_FOUND) {
        return GAKU_E_INTERNAL;
    }

    GakuPluginSummaryV1 brokenSummary{};
    if (plugins.findPlugin("meta.broken", sizeof(brokenSummary), &brokenSummary) != GAKU_OK ||
        brokenSummary.state != GAKU_PLUGIN_STATE_DEPENDENCY_ERROR) {
        return GAKU_E_INTERNAL;
    }

    bool sawBaseAtIndex = false;
    for (uint32_t i = 0; i < hostInfo.pluginCount; ++i) {
        GakuPluginSummaryV1 item{};
        if (plugins.getPluginAt(i, sizeof(item), &item) != GAKU_OK) {
            return GAKU_E_INTERNAL;
        }
        if (std::strcmp(item.id, "meta.base") == 0) {
            sawBaseAtIndex = true;
        }
    }
    GakuPluginSummaryV1 outOfRange{};
    if (!sawBaseAtIndex ||
        plugins.getPluginAt(hostInfo.pluginCount, sizeof(outOfRange), &outOfRange) != GAKU_E_INVALID_ARGUMENT) {
        return GAKU_E_INTERNAL;
    }

    return GAKU_OK;
}

GakuStatus FillMetadata(GakuPluginMetadataV1* output) {
    output->minLoaderVersion = "1.1.0";
    output->dependencyCount = 3u;
    output->dependencies = kDependencies;
    return GAKU_OK;
}

#elif METADATA_FIXTURE_VARIANT == 3
constexpr const char* kId = "meta.broken";
constexpr const char* kName = "Broken Dep";
constexpr const char* kVersion = "0.1.0";

const GakuPluginDependencyV1 kDependencies[] = {
    {"meta.base", "Metadata Base", ">=9.9.9", GAKU_DEPENDENCY_REQUIRED, 0u},
};

GakuStatus Activate(const GakuHostApi*, void**) {
    return GAKU_E_INTERNAL;
}

GakuStatus FillMetadata(GakuPluginMetadataV1* output) {
    output->dependencyCount = 1u;
    output->dependencies = kDependencies;
    return GAKU_OK;
}

#elif METADATA_FIXTURE_VARIANT == 4
constexpr const char* kId = "meta.opt_cycle_a";
constexpr const char* kName = "Opt Cycle A";
constexpr const char* kVersion = "0.2.0";

const GakuPluginDependencyV1 kDependencies[] = {
    {"meta.opt_cycle_b", "Opt Cycle B", ">=0.1.0", GAKU_DEPENDENCY_OPTIONAL, 0u},
};

GakuStatus Activate(const GakuHostApi* host, void** state) {
    if (!host || !state || host->abiVersion != GAKU_ABI_VERSION ||
        host->structSize < sizeof(GakuHostApi)) return GAKU_E_INVALID_ARGUMENT;
    *state = nullptr;
    return GAKU_OK;
}

GakuStatus FillMetadata(GakuPluginMetadataV1* output) {
    output->dependencyCount = 1u;
    output->dependencies = kDependencies;
    return GAKU_OK;
}

#elif METADATA_FIXTURE_VARIANT == 5
constexpr const char* kId = "meta.opt_cycle_b";
constexpr const char* kName = "Opt Cycle B";
constexpr const char* kVersion = "0.2.0";

const GakuPluginDependencyV1 kDependencies[] = {
    {"meta.opt_cycle_a", "Opt Cycle A", ">=0.1.0", GAKU_DEPENDENCY_OPTIONAL, 0u},
};

GakuStatus Activate(const GakuHostApi* host, void** state) {
    if (!host || !state || host->abiVersion != GAKU_ABI_VERSION ||
        host->structSize < sizeof(GakuHostApi)) return GAKU_E_INVALID_ARGUMENT;
    *state = nullptr;
    return GAKU_OK;
}

GakuStatus FillMetadata(GakuPluginMetadataV1* output) {
    output->dependencyCount = 1u;
    output->dependencies = kDependencies;
    return GAKU_OK;
}

#else
#error Unknown metadata fixture variant
#endif

} // namespace

extern "C" GAKU_PLUGIN_EXPORT GakuStatus GakuPlugin_Query(
    uint32_t version, uint32_t outputSize, GakuPluginDescriptor* output) {
    if (version != GAKU_ABI_VERSION) return GAKU_E_VERSION;
    if (!output) return GAKU_E_INVALID_ARGUMENT;
    if (outputSize < sizeof(*output)) return GAKU_E_SIZE;
    GakuPluginDescriptor value{};
    value.abiVersion = GAKU_ABI_VERSION;
    value.structSize = sizeof(value);
    value.id = kId;
    value.name = kName;
    value.version = kVersion;
    value.activate = Activate;
    *output = value;
    return GAKU_OK;
}

extern "C" GAKU_PLUGIN_EXPORT GakuStatus GakuPlugin_QueryMetadata(
    uint32_t requestedAbi, uint32_t outputSize, GakuPluginMetadataV1* output) {
    if (requestedAbi != GAKU_METADATA_ABI_VERSION) return GAKU_E_VERSION;
    if (!output) return GAKU_E_INVALID_ARGUMENT;
    if (outputSize < sizeof(*output)) return GAKU_E_SIZE;
    GakuPluginMetadataV1 value{};
    value.abiVersion = GAKU_METADATA_ABI_VERSION;
    value.structSize = sizeof(value);
    const GakuStatus status = FillMetadata(&value);
    if (status != GAKU_OK) return status;
    *output = value;
    return GAKU_OK;
}

#endif
