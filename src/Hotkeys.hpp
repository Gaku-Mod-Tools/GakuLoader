#pragma once
#include "GakuPlugin.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Discrete hotkey registry. Pure registry + edge logic: no Win32 input calls,
// no UI, no threads. The loader's poller supplies key state and dispatches the
// returned actions on the Unity main thread; the manager toggle is UI-only and
// must not wait for a game frame.
namespace GakuLoader::Hotkeys {

constexpr uint32_t kManagerKey = 0x70; // VK_F1, reserved by the loader

struct PollContext {
    bool foregroundOwnedByThisProcess{false};
    bool hostWindowWantsTextInput{false};
    bool (*isKeyDown)(uint32_t virtualKey){nullptr};
};
struct Action {
    void* owner{nullptr};
    GakuHotkeyFn callback{nullptr};
    void* userData{nullptr};
};
struct PollResult {
    std::vector<Action> actions;
    bool managerToggle{false};
};
struct Row {
    std::string pluginId, actionId, displayName, combo;
    uint32_t virtualKey{0}, modifiers{0};
    bool conflicted{false}, unbound{false};
};

// Reads the optional [hotkeys] section: "<pluginId>.<actionId>=Ctrl+M".
// An empty value unbinds the action without disabling the plugin.
void LoadOverrides(const std::filesystem::path& config);
GakuStatus Register(void* owner, const char* pluginId, const GakuHotkeyBinding* binding);
GakuStatus Unregister(void* owner, const char* actionId);
void RetireOwner(void* owner);
void Poll(const PollContext& context, PollResult& output);
std::vector<Row> Snapshot();

// Parsing/formatting exposed for the manager and configuration tests.
bool ParseCombo(const std::wstring& text, uint32_t& virtualKey, uint32_t& modifiers);
std::string ComboName(uint32_t virtualKey, uint32_t modifiers);
}
