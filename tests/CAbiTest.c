#include "GakuPlugin.h"
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
int main(void) { return GAKU_ABI_VERSION == 1u ? 0 : 1; }
