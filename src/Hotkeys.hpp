// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

#pragma once
#include "GakuPlugin.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Discrete hotkey registry with INI overrides; no UI or owned threads. The
// loader's poller supplies key state and dispatches the
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
    bool conflicted{false}, unbound{false}, overridden{false};
};

// Reads [hotkeys]. New v2:<hex pluginId>:<hex actionId> keys preserve exact
// identities; old dotted aliases apply only when unambiguous. Empty unbinds;
// <default> on a v2 key masks legacy values and follows the plugin's default.
void LoadOverrides(const std::filesystem::path& config);
GakuStatus Register(void* owner, const char* pluginId, const GakuHotkeyBinding* binding);
GakuStatus Unregister(void* owner, const char* actionId);
void RetireOwner(void* owner);
void Poll(const PollContext& context, PollResult& output);
std::vector<Row> Snapshot();

// Manager-only editing. A successful write applies immediately and survives
// restart. virtualKey=0/modifiers=0 unbinds; reset writes <default> for this exact
// identity and preserves old INI entries. Failed writes leave
// both the live registry and its override cache unchanged.
GakuStatus SetOverride(const std::filesystem::path& config, const char* pluginId,
    const char* actionId, uint32_t virtualKey, uint32_t modifiers);
GakuStatus ResetOverride(const std::filesystem::path& config, const char* pluginId,
    const char* actionId);
// Suppress plugin dispatch and F1 while the manager captures a replacement.
// Leaving capture requires a released key before another press can dispatch.
void SetCaptureActive(bool active);

// Parsing/formatting exposed for the manager and configuration tests.
bool ParseCombo(const std::wstring& text, uint32_t& virtualKey, uint32_t& modifiers);
std::string ComboName(uint32_t virtualKey, uint32_t modifiers);
}
