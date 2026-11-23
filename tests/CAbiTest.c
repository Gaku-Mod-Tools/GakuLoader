#include "GakuPlugin.h"
#include "GakuPluginMetadata.h"
#include "GakuFileDrop.h"
#include "GakuProcessLifetimeHooks.h"
#include "GakuPaths.h"
#include "GakuLanguage.h"
#include "GakuLifecycle.h"
#include <stddef.h>
_Static_assert(offsetof(GakuHostApi, abiVersion) == 0, "version must be first");
_Static_assert(offsetof(GakuHostApi, structSize) == sizeof(uint32_t), "size must be second");
_Static_assert(offsetof(GakuPluginDescriptor, structSize) == sizeof(uint32_t), "descriptor header");
_Static_assert(offsetof(GakuPluginDescriptor, activate) == 32, "activation follows identity");
_Static_assert(offsetof(GakuPluginDescriptor, onTick) == 48, "frame tick follows runtime-ready");
_Static_assert(offsetof(GakuPluginDescriptor, onActorTick) == 56, "actor tick follows frame tick");
_Static_assert(sizeof(GakuPluginDescriptor) == 80, "x64 v1 descriptor layout");
_Static_assert(sizeof(GakuServiceHeader) == 8, "table header layout");
_Static_assert(sizeof(GakuLogService) == 24, "log service layout");
_Static_assert(sizeof(GakuRuntimeService) == 40, "runtime service layout");
_Static_assert(sizeof(GakuPathsV1) == 32, "optional path service layout");
_Static_assert(offsetof(GakuPathsV1, context) == 8, "path service owner token");
_Static_assert(offsetof(GakuPathsV1, getRoot) == 16, "path service getter");
_Static_assert(offsetof(GakuPathsV1, ensureRoot) == 24, "path service creator");
_Static_assert(sizeof(GakuMainThreadService) == 32, "main-thread service layout");
_Static_assert(sizeof(GakuHookRequest) == 24, "hook request layout");
_Static_assert(sizeof(GakuHookService) == 40, "hook service layout");
_Static_assert(sizeof(GakuProcessLifetimeHooksV1) == 24, "process lifetime extension layout");
_Static_assert(sizeof(GakuHotkeyBinding) == 48, "hotkey binding layout");
_Static_assert(sizeof(GakuHotkeyService) == 32, "hotkey service layout");
_Static_assert(sizeof(GakuDesktopUiBinding) == 40, "ImGui binding layout");
_Static_assert(sizeof(GakuDesktopUiService) == 16, "ImGui service layout");
_Static_assert(sizeof(GakuFileDropTarget) == 24, "file drop target extension layout");
_Static_assert(sizeof(GakuFileDropResult) == 16, "file drop result extension layout");
_Static_assert(sizeof(GakuFileDropService) == 32, "file drop service extension layout");
_Static_assert(offsetof(GakuFileDropService, context) == 8, "file drop owner token");
_Static_assert(sizeof(GakuUiAppearanceV1) == 16, "appearance v1 layout");
_Static_assert(sizeof(GakuAppearanceService) == 16, "appearance service layout");
_Static_assert(GAKU_LANGUAGE_VERSION == 1u, "optional language service version");
_Static_assert(sizeof(GakuUiLanguageV1) == 16, "language value layout");
_Static_assert(offsetof(GakuUiLanguageV1, abiVersion) == 0, "language value version");
_Static_assert(offsetof(GakuUiLanguageV1, structSize) == 4, "language value size");
_Static_assert(offsetof(GakuUiLanguageV1, language) == 8, "language value choice");
_Static_assert(offsetof(GakuUiLanguageV1, reserved) == 12, "language value reserved");
_Static_assert(sizeof(GakuLanguageServiceV1) == 16, "optional language service layout");
_Static_assert(sizeof(GakuLifecycleServiceV1) == 24, "optional lifecycle service layout");
_Static_assert(offsetof(GakuLifecycleServiceV1, context) == 8, "lifecycle owner token");
_Static_assert(offsetof(GakuLifecycleServiceV1, isStopping) == 16, "lifecycle cancellation getter");
_Static_assert(offsetof(GakuLanguageServiceV1, getLanguage) == 8, "language service getter");
_Static_assert(GAKU_LANGUAGE_ENGLISH == 0u && GAKU_LANGUAGE_SIMPLIFIED_CHINESE == 1u &&
    GAKU_LANGUAGE_TRADITIONAL_CHINESE == 2u && GAKU_LANGUAGE_JAPANESE == 3u, "stable language ids");
_Static_assert(sizeof(GakuDesktopWindowService) == 40, "desktop window layout");
_Static_assert(sizeof(GakuFileDialogRequest) == 32, "file request layout");
_Static_assert(sizeof(GakuFileDialogResult) == 20, "file result layout");
_Static_assert(sizeof(GakuFileDialogService) == 40, "file service layout");
_Static_assert(sizeof(GakuRenderFrameBinding) == 40, "render frame binding layout");
_Static_assert(sizeof(GakuRenderFrameService) == 32, "render frame service layout");
_Static_assert(sizeof(GakuRenderFrameStatusService) == 16, "render frame status service layout");
_Static_assert(GAKU_METADATA_ABI_VERSION == 1u, "metadata abi version");
_Static_assert(GAKU_PLUGINS_SERVICE_VERSION == 1u, "plugins service version");
_Static_assert(GAKU_DEPENDENCY_REQUIRED == 1u && GAKU_DEPENDENCY_OPTIONAL == 2u &&
    GAKU_DEPENDENCY_CONFLICT == 3u, "stable dependency kinds");
_Static_assert(GAKU_PLUGIN_STATE_NOT_FOUND == 0u && GAKU_PLUGIN_STATE_DISABLED == 1u &&
    GAKU_PLUGIN_STATE_DEPENDENCY_ERROR == 2u && GAKU_PLUGIN_STATE_INCOMPATIBLE == 3u &&
    GAKU_PLUGIN_STATE_QUERIED == 4u && GAKU_PLUGIN_STATE_ACTIVATED == 5u &&
    GAKU_PLUGIN_STATE_RUNTIME_READY == 6u && GAKU_PLUGIN_STATE_FAILED == 7u, "stable plugin load states");
_Static_assert(sizeof(GakuPluginLocalizedTextV1) == 16, "localized text v1 layout");
_Static_assert(offsetof(GakuPluginLocalizedTextV1, languageTag) == 0, "localized text languageTag");
_Static_assert(offsetof(GakuPluginLocalizedTextV1, text) == 8, "localized text body");
_Static_assert(sizeof(GakuPluginDependencyV1) == 32, "dependency v1 layout");
_Static_assert(offsetof(GakuPluginDependencyV1, pluginId) == 0, "dependency pluginId");
_Static_assert(offsetof(GakuPluginDependencyV1, displayName) == 8, "dependency displayName");
_Static_assert(offsetof(GakuPluginDependencyV1, versionRange) == 16, "dependency versionRange");
_Static_assert(offsetof(GakuPluginDependencyV1, kind) == 24, "dependency kind");
_Static_assert(offsetof(GakuPluginDependencyV1, reserved) == 28, "dependency reserved");
_Static_assert(sizeof(GakuPluginMetadataV1) == 144, "plugin metadata v1 layout");
_Static_assert(offsetof(GakuPluginMetadataV1, abiVersion) == 0, "metadata abiVersion");
_Static_assert(offsetof(GakuPluginMetadataV1, structSize) == 4, "metadata structSize");
_Static_assert(offsetof(GakuPluginMetadataV1, originalAuthor) == 8, "metadata originalAuthor");
_Static_assert(offsetof(GakuPluginMetadataV1, maintainerCount) == 16, "metadata maintainerCount");
_Static_assert(offsetof(GakuPluginMetadataV1, maintainers) == 24, "metadata maintainers");
_Static_assert(offsetof(GakuPluginMetadataV1, subVersion) == 32, "metadata subVersion");
_Static_assert(offsetof(GakuPluginMetadataV1, releaseChannel) == 40, "metadata releaseChannel");
_Static_assert(offsetof(GakuPluginMetadataV1, summary) == 48, "metadata summary");
_Static_assert(offsetof(GakuPluginMetadataV1, localizedSummaryCount) == 56, "metadata localizedSummaryCount");
_Static_assert(offsetof(GakuPluginMetadataV1, localizedSummaries) == 64, "metadata localizedSummaries");
_Static_assert(offsetof(GakuPluginMetadataV1, repositoryUrl) == 72, "metadata repositoryUrl");
_Static_assert(offsetof(GakuPluginMetadataV1, releaseUrl) == 80, "metadata releaseUrl");
_Static_assert(offsetof(GakuPluginMetadataV1, issuesUrl) == 88, "metadata issuesUrl");
_Static_assert(offsetof(GakuPluginMetadataV1, license) == 96, "metadata license");
_Static_assert(offsetof(GakuPluginMetadataV1, minLoaderVersion) == 104, "metadata minLoaderVersion");
_Static_assert(offsetof(GakuPluginMetadataV1, dependencyCount) == 112, "metadata dependencyCount");
_Static_assert(offsetof(GakuPluginMetadataV1, dependencies) == 120, "metadata dependencies");
_Static_assert(offsetof(GakuPluginMetadataV1, auxiliaryDllCount) == 128, "metadata auxiliaryDllCount");
_Static_assert(offsetof(GakuPluginMetadataV1, auxiliaryDlls) == 136, "metadata auxiliaryDlls");
_Static_assert(sizeof(GakuHostInfoV1) == 80, "host info v1 layout");
_Static_assert(offsetof(GakuHostInfoV1, abiVersion) == 0, "host info abiVersion");
_Static_assert(offsetof(GakuHostInfoV1, structSize) == 4, "host info structSize");
_Static_assert(offsetof(GakuHostInfoV1, pluginAbiVersion) == 8, "host info pluginAbiVersion");
_Static_assert(offsetof(GakuHostInfoV1, pluginCount) == 12, "host info pluginCount");
_Static_assert(offsetof(GakuHostInfoV1, loaderVersion) == 16, "host info loaderVersion");
_Static_assert(sizeof(GakuPluginSummaryV1) == 340, "plugin summary v1 layout");
_Static_assert(offsetof(GakuPluginSummaryV1, abiVersion) == 0, "plugin summary abiVersion");
_Static_assert(offsetof(GakuPluginSummaryV1, structSize) == 4, "plugin summary structSize");
_Static_assert(offsetof(GakuPluginSummaryV1, state) == 8, "plugin summary state");
_Static_assert(offsetof(GakuPluginSummaryV1, hasMetadata) == 12, "plugin summary hasMetadata");
_Static_assert(offsetof(GakuPluginSummaryV1, id) == 16, "plugin summary id");
_Static_assert(offsetof(GakuPluginSummaryV1, name) == 81, "plugin summary name");
_Static_assert(offsetof(GakuPluginSummaryV1, version) == 209, "plugin summary version");
_Static_assert(offsetof(GakuPluginSummaryV1, subVersion) == 273, "plugin summary subVersion");
_Static_assert(sizeof(GakuPluginsServiceV1) == 32, "plugins service v1 layout");
_Static_assert(offsetof(GakuPluginsServiceV1, abiVersion) == 0, "plugins service abiVersion");
_Static_assert(offsetof(GakuPluginsServiceV1, structSize) == 4, "plugins service structSize");
_Static_assert(offsetof(GakuPluginsServiceV1, getHostInfo) == 8, "plugins service getHostInfo");
_Static_assert(offsetof(GakuPluginsServiceV1, findPlugin) == 16, "plugins service findPlugin");
_Static_assert(offsetof(GakuPluginsServiceV1, getPluginAt) == 24, "plugins service getPluginAt");
int main(void) { return GAKU_ABI_VERSION == 1u ? 0 : 1; }
