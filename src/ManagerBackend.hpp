// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

#pragma once
// Backend wiring for the Manager bridge. Registers the fixed allow-list of
// commands/queries against the existing native owners (Hotkeys, LanguageSettings,
// DesktopAppearance via hooks, the INI helpers, SnapshotDesktopHost) and pushes
// change events. It never exposes plugin pointers, hook leases, or the live
// plugin vector; host state is read only through the non-blocking snapshot copy.
//
// The Context hooks are the small seam into DesktopUi live state (selection,
// window, appearance apply, text-input, save banner). Tests stub them and drive
// the real Hotkeys/LanguageSettings/INI owners against a temporary config.

#include "ManagerBridge.hpp"
#include "Host.hpp"
#include <filesystem>
#include <functional>
#include <string>

namespace GakuLoader::ManagerBackend {

struct AppearanceState {
    int theme{0};
    float uiScale{1.0f};
    bool scaleLimited{false};
    float dpiScale{1.0f};
};

struct Context {
    std::filesystem::path configPath;
    std::string managerPanelId;
    std::function<bool(DesktopHostSnapshot&)> snapshot;
    std::function<bool()> shuttingDown;
    std::function<std::string()> selectedPanel;
    std::function<void(const std::string&)> selectPanel;
    std::function<void()> hideWindow;
    std::function<HWND()> hostWindow;
    std::function<std::string()> uiMode;
    std::function<bool()> webviewReady;
    std::function<AppearanceState()> getAppearance;
    std::function<bool(int)> applyTheme;        // theme enum; returns accepted
    std::function<bool(int)> applyScale;        // integer percent; returns accepted
    std::function<bool(uint32_t)> setLanguage;  // returns saved
    std::function<void(bool)> setTextInput;
    std::function<bool()> getSaveFailed;
    std::function<void(bool)> setSaveFailed;
};

class Backend {
public:
    explicit Backend(Context context);
    ManagerBridge::Bridge& bridge() { return bridge_; }
    // Push the initial state once the entry document is ready.
    void OnDocumentReady();
    // Per-UI-loop maintenance: native hotkey capture poll + change-event watch.
    void Tick();
    // Invalidate the bridge on shutdown (rejects further requests).
    void Shutdown();

private:
    void RegisterHandlers();
    void EmitSnapshot(bool force);
    void EmitAppearance(bool force);
    void EmitSelection(bool force);
    void EmitSaveFailed(bool force);
    void PollCapture();
    void BeginCapture(const std::string& pluginId, const std::string& actionId, const std::string& combo);
    void EndCapture(bool cancelled);
    void SetEditorActive(bool active);
    nlohmann::json SnapshotJson(const DesktopHostSnapshot& snapshot) const;
    nlohmann::json RowsJson() const;
    nlohmann::json SettingsJson() const;
    nlohmann::json AppearanceJson(const AppearanceState& state) const;
    nlohmann::json StringsJson() const;
    nlohmann::json LanguageJson() const;
    void PushSettingsResult(bool ok, const std::string& key);

    Context ctx_;
    ManagerBridge::Bridge bridge_;
    std::string lastSnapshotSig_, lastSelected_, lastSettingsSig_;
    AppearanceState lastAppearance_{};
    bool haveAppearance_{false}, lastSaveFailed_{false}, haveSaveFailed_{false};
    bool capturing_{false}, capInvalid_{false}, editorActive_{false};
    std::string capPlugin_, capAction_, capCombo_;
    bool capDown_[256]{};
};

} // namespace GakuLoader::ManagerBackend
