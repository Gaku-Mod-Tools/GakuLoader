#pragma once
#include "GakuPlugin.h"
#include "GakuFileDrop.h"

namespace GakuLoader::DesktopUi {
// Starts the standalone Win32/D3D11 desktop window. Never call from DllMain.
bool Initialize();
// Called before plugin shutdown, outside DllMain. No game presentation hooks.
void Shutdown();
const GakuDesktopUiService* GetService();
const GakuDesktopWindowService* GetWindowService();
const GakuAppearanceService* GetAppearanceService();
GakuStatus SetFileDropTarget(const char* pluginId, const GakuFileDropTarget* target);
GakuStatus PollFileDrop(const char* pluginId, uint32_t resultSize, GakuFileDropResult* result,
    uint32_t capacity, char* paths);
// Owner identity is supplied by the host service wrapper. Copies the id before
// posting and selects that plugin's panel even when another panel is selected.
GakuStatus RequestShowPanel(const char* pluginId);
// F1: show the manager page, or hide the control window when the manager is
// already showing. Safe from any thread; never waits for a game frame.
void ToggleManager();
// True while an ImGui text field in the control window owns keyboard input.
bool WantsTextInput();

#if defined(GAKULOADER_DESKTOP_UI_TEST)
// Compiled only by the WARP smoke, never exposed through the plugin ABI.
struct TestHooks {
    bool startHidden;
    void (*inspectFrame)(void* device, void* context, void* swapchain);
    void (*beginCpuFrame)() = nullptr;
    void (*presented)() = nullptr;
    const char* (*clipboardGet)(void*) = nullptr;
    void (*clipboardSet)(void*, const char*) = nullptr;
    uint32_t (*clipboardSequence)() = nullptr;
    void* clipboardUserData = nullptr;
    void (*inspectUiItem)(const char* key, const char* value, float left, float top, float right, float bottom) = nullptr;
};
void ConfigureTest(const TestHooks& hooks);
#endif
}
