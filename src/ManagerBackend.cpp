// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

#include "ManagerBackend.hpp"
#include "Hotkeys.hpp"
#include "LanguageSettings.hpp"
#include "ManagerStrings.hpp"
#include "HostConfig.hpp"
#include "PluginMetadata.hpp"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace GakuLoader::ManagerBackend {
namespace {
using nlohmann::json;
using GakuLoader::ManagerBridge::Result;

std::wstring Widen(const std::string& text) {
    if (text.empty()) return {};
    const int wide = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(wide), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), wide);
    return out;
}

std::string Narrow(const std::wstring& text) {
    if (text.empty()) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), bytes,
        nullptr, nullptr);
    return out;
}

std::string RequireString(const json& params, const char* key) {
    if (!params.contains(key) || !params[key].is_string())
        throw std::runtime_error(std::string("Missing string parameter: ") + key);
    return params[key].get<std::string>();
}

bool RequireBool(const json& params, const char* key) {
    if (!params.contains(key) || !params[key].is_boolean())
        throw std::runtime_error(std::string("Missing boolean parameter: ") + key);
    return params[key].get<bool>();
}

int RequireInt(const json& params, const char* key) {
    if (!params.contains(key) || !params[key].is_number_integer())
        throw std::runtime_error(std::string("Missing integer parameter: ") + key);
    return params[key].get<int>();
}

bool AllowedScale(int percent) {
    static constexpr int kScales[] = {50, 75, 90, 100, 110, 125, 150, 175, 200, 250, 300};
    for (int allowed : kScales) if (allowed == percent) return true;
    return false;
}

const std::pair<const char*, ManagerStrings::Key> kStringKeys[] = {
    {"Manager", ManagerStrings::Key::Manager}, {"PluginList", ManagerStrings::Key::PluginList},
    {"Appearance", ManagerStrings::Key::Appearance}, {"Hotkeys", ManagerStrings::Key::Hotkeys},
    {"Settings", ManagerStrings::Key::Settings}, {"Plugin", ManagerStrings::Key::Plugin},
    {"Version", ManagerStrings::Key::Version}, {"State", ManagerStrings::Key::State},
    {"Panel", ManagerStrings::Key::Panel}, {"Ready", ManagerStrings::Key::Ready},
    {"Activated", ManagerStrings::Key::Activated}, {"Inactive", ManagerStrings::Key::Inactive},
    {"Failed", ManagerStrings::Key::Failed}, {"Yes", ManagerStrings::Key::Yes},
    {"No", ManagerStrings::Key::No}, {"Waiting", ManagerStrings::Key::Waiting},
    {"Running", ManagerStrings::Key::Running}, {"Stopping", ManagerStrings::Key::Stopping},
    {"Theme", ManagerStrings::Key::Theme}, {"Scale", ManagerStrings::Key::Scale},
    {"Languages", ManagerStrings::Key::Languages}, {"Dark", ManagerStrings::Key::Dark},
    {"Sapphire", ManagerStrings::Key::Sapphire}, {"Paper", ManagerStrings::Key::Paper},
    {"LanguageHelp", ManagerStrings::Key::LanguageHelp}, {"NoHotkeys", ManagerStrings::Key::NoHotkeys},
    {"Action", ManagerStrings::Key::Action}, {"Name", ManagerStrings::Key::Name},
    {"Combo", ManagerStrings::Key::Combo}, {"Active", ManagerStrings::Key::Active},
    {"Conflict", ManagerStrings::Key::Conflict}, {"Unbound", ManagerStrings::Key::Unbound},
    {"Edit", ManagerStrings::Key::Edit}, {"Reset", ManagerStrings::Key::Reset},
    {"Unbind", ManagerStrings::Key::Unbind}, {"Cancel", ManagerStrings::Key::Cancel},
    {"Apply", ManagerStrings::Key::Apply}, {"Capture", ManagerStrings::Key::Capture},
    {"CaptureHelp", ManagerStrings::Key::CaptureHelp}, {"CaptureButton", ManagerStrings::Key::CaptureButton},
    {"InvalidCombo", ManagerStrings::Key::InvalidCombo}, {"SaveFailed", ManagerStrings::Key::SaveFailed},
    {"HotkeyHelp", ManagerStrings::Key::HotkeyHelp}, {"Console", ManagerStrings::Key::Console},
    {"Trace", ManagerStrings::Key::Trace}, {"NextStart", ManagerStrings::Key::NextStart},
    {"PluginEnable", ManagerStrings::Key::PluginEnable}, {"PluginEnableHelp", ManagerStrings::Key::PluginEnableHelp},
    {"File", ManagerStrings::Key::File}, {"Hide", ManagerStrings::Key::Hide},
    {"View", ManagerStrings::Key::View}, {"Plugins", ManagerStrings::Key::Plugins},
    {"SelectPanel", ManagerStrings::Key::SelectPanel}, {"FailedHelp", ManagerStrings::Key::FailedHelp},
    {"NoPanel", ManagerStrings::Key::NoPanel}, {"Count", ManagerStrings::Key::Count},
    {"Runtime", ManagerStrings::Key::Runtime}, {"SummaryHelp", ManagerStrings::Key::SummaryHelp},
    {"ConfigFile", ManagerStrings::Key::ConfigFile}, {"Saved", ManagerStrings::Key::Saved},
    {"DefaultBinding", ManagerStrings::Key::DefaultBinding}, {"CustomBinding", ManagerStrings::Key::CustomBinding},
    {"ScaleLimit", ManagerStrings::Key::ScaleLimit}, {"AppleLight", ManagerStrings::Key::AppleLight},
    {"AppleDark", ManagerStrings::Key::AppleDark},
};
} // namespace

Backend::Backend(Context context) : ctx_(std::move(context)) { RegisterHandlers(); }

json Backend::SnapshotJson(const DesktopHostSnapshot& snapshot) const {
    const std::string langTag = Narrow(LanguageSettings::kTags[LanguageSettings::Current()]);
    json plugins = json::array();
    for (const auto& plugin : snapshot.plugins) {
        json deps = json::array();
        for (const auto& dep : plugin.dependencies) {
            deps.push_back(json{
                {"kind", dep.kind},
                {"pluginId", dep.pluginId},
                {"displayName", dep.displayName},
                {"versionRange", dep.versionRange},
                {"targetState", dep.targetState},
                {"actualVersion", dep.actualVersion},
                {"satisfied", dep.satisfied},
                {"cycleDegraded", dep.cycleDegraded},
                {"diagnostic", dep.diagnostic}
            });
        }
        plugins.push_back(json{
            {"id", plugin.id},
            {"name", plugin.name},
            {"version", ManagerStrings::FormatVersion(plugin.version)},
            {"activated", plugin.activated},
            {"runtimeReady", plugin.runtimeReady},
            {"failed", plugin.failed},
            {"hasPanel", plugin.hasPanel},
            {"loadState", plugin.loadState},
            {"statusReason", plugin.statusReason},
            {"dllFilename", plugin.dllFilename},
            {"hasMetadata", plugin.hasMetadata},
            {"subVersion", plugin.subVersion},
            {"releaseChannel", plugin.releaseChannel},
            {"summary", Metadata::ResolveLocalizedSummary(plugin.summary, plugin.localizedSummaries, langTag)},
            {"originalAuthor", plugin.originalAuthor},
            {"maintainers", plugin.maintainers},
            {"repositoryUrl", plugin.repositoryUrl},
            {"releaseUrl", plugin.releaseUrl},
            {"issuesUrl", plugin.issuesUrl},
            {"license", plugin.license},
            {"minLoaderVersion", plugin.minLoaderVersion},
            {"dependencies", std::move(deps)}
        });
    }
    json files = json::array();
    for (const auto& file : snapshot.pluginFiles) {
        files.push_back(Narrow(file));
    }
    json auxFiles = json::array();
    for (const auto& file : snapshot.auxiliaryFiles) {
        auxFiles.push_back(Narrow(file));
    }
    return json{{"available", true}, {"plugins", plugins}, {"pluginFiles", files},
        {"auxiliaryFiles", auxFiles}, {"runtimeReady", snapshot.runtimeReady},
        {"shuttingDown", snapshot.shuttingDown}, {"activated", snapshot.activated}};
}

json Backend::RowsJson() const {
    json rows = json::array();
    for (const auto& row : Hotkeys::Snapshot()) {
        rows.push_back(json{{"pluginId", row.pluginId}, {"actionId", row.actionId},
            {"displayName", row.displayName}, {"combo", row.unbound ? "" : row.combo},
            {"virtualKey", row.virtualKey}, {"modifiers", row.modifiers},
            {"conflicted", row.conflicted}, {"unbound", row.unbound}, {"overridden", row.overridden}});
    }
    return json{{"rows", rows}};
}

json Backend::SettingsJson() const {
    const bool console = ReadConfigFlag(ctx_.configPath, L"host", L"console", false);
    const bool trace = ReadConfigFlag(ctx_.configPath, L"diagnostics", L"trace", false);
    json plugins = json::array();
    DesktopHostSnapshot snapshot;
    if (ctx_.snapshot && ctx_.snapshot(snapshot)) {
        for (const auto& file : snapshot.pluginFiles) {
            const std::string filename = Narrow(file);
            plugins.push_back(json{{"filename", filename},
                {"enabled", ReadConfigFlag(ctx_.configPath, L"plugins", file.c_str(), true)}});
        }
    }
    return json{{"console", console}, {"trace", trace}, {"plugins", plugins}};
}

json Backend::AppearanceJson(const AppearanceState& state) const {
    return json{{"theme", state.theme}, {"uiScale", state.uiScale},
        {"scaleLimited", state.scaleLimited}, {"dpiScale", state.dpiScale}};
}

json Backend::LanguageJson() const {
    const uint32_t language = LanguageSettings::Current();
    json names = json::array();
    for (uint32_t i = 0; i < 4; ++i) names.push_back(LanguageSettings::kNames[i]);
    return json{{"language", language}, {"tag", Narrow(LanguageSettings::kTags[language])},
        {"name", LanguageSettings::kNames[language]}, {"names", names}};
}

json Backend::StringsJson() const {
    json strings = json::object();
    for (const auto& entry : kStringKeys) strings[entry.first] = ManagerStrings::Text(entry.second);
    return json{{"language", LanguageSettings::Current()}, {"strings", strings}};
}

void Backend::PushSettingsResult(bool ok, const std::string& key) {
    if (ctx_.setSaveFailed) ctx_.setSaveFailed(!ok);
    bridge_.EmitEvent("shell.saveFailed", json{{"failed", !ok}});
    bridge_.EmitEvent("settings.saveResult", json{{"ok", ok}, {"key", key}});
    bridge_.EmitEvent("settings.changed", SettingsJson());
}

void Backend::RegisterHandlers() {
    auto& b = bridge_;
    const auto config = ctx_.configPath;

    b.Register("host.snapshot", [this](const json&) {
        DesktopHostSnapshot snapshot;
        if (!ctx_.snapshot || !ctx_.snapshot(snapshot))
            return Result::Ok(json{{"available", false}, {"plugins", json::array()},
                {"pluginFiles", json::array()}, {"auxiliaryFiles", json::array()},
                {"runtimeReady", false}, {"shuttingDown", ctx_.shuttingDown && ctx_.shuttingDown()},
                {"activated", false}});
        return Result::Ok(SnapshotJson(snapshot));
    });
    b.Register("hotkeys.rows", [this](const json&) { return Result::Ok(RowsJson()); });
    b.Register("settings.get", [this](const json&) { return Result::Ok(SettingsJson()); });
    b.Register("appearance.get", [this](const json&) {
        return Result::Ok(AppearanceJson(ctx_.getAppearance ? ctx_.getAppearance() : AppearanceState{}));
    });
    b.Register("language.get", [this](const json&) { return Result::Ok(LanguageJson()); });
    b.Register("strings.get", [this](const json&) { return Result::Ok(StringsJson()); });
    b.Register("config.path", [this, config](const json&) {
        const auto utf8 = config.u8string();
        return Result::Ok(json{{"path", std::string(utf8.begin(), utf8.end())}});
    });
    b.Register("shell.state", [this](const json&) {
        return Result::Ok(json{{"selectedPanel", ctx_.selectedPanel ? ctx_.selectedPanel() : ctx_.managerPanelId},
            {"managerPanelId", ctx_.managerPanelId}, {"mode", ctx_.uiMode ? ctx_.uiMode() : "imgui"},
            {"webviewReady", ctx_.webviewReady && ctx_.webviewReady()},
            {"saveFailed", ctx_.getSaveFailed && ctx_.getSaveFailed()}});
    });

    b.Register("shell.hide", [this](const json&) {
        if (ctx_.hideWindow) ctx_.hideWindow();
        return Result::Ok(json::object());
    });
    b.Register("shell.selectPanel", [this](const json& params) {
        const auto id = RequireString(params, "id");
        if (id != ctx_.managerPanelId) {
            DesktopHostSnapshot snapshot;
            const bool known = ctx_.snapshot && ctx_.snapshot(snapshot) &&
                std::any_of(snapshot.plugins.begin(), snapshot.plugins.end(),
                    [&](const DesktopPluginPanel& panel) { return panel.id == id; });
            if (!known) return Result::Fail("unknown_panel", "Panel is not registered.");
        }
        if (ctx_.selectPanel) ctx_.selectPanel(id);
        EmitSelection(true);
        return Result::Ok(json{{"selectedPanel", ctx_.selectedPanel ? ctx_.selectedPanel() : id}});
    });
    b.Register("shell.setTextInputActive", [this](const json& params) {
        if (ctx_.setTextInput) ctx_.setTextInput(RequireBool(params, "active"));
        return Result::Ok(json::object());
    });

    b.Register("appearance.setTheme", [this](const json& params) {
        const int theme = RequireInt(params, "theme");
        if (theme < 0 || theme > 4) return Result::Fail("invalid_theme", "Theme must be 0..4.");
        if (ctx_.applyTheme && !ctx_.applyTheme(theme))
            return Result::Fail("apply_failed", "Theme could not be applied.");
        return Result::Ok(json{{"theme", theme}});
    });
    b.Register("appearance.setScale", [this](const json& params) {
        const int percent = RequireInt(params, "percent");
        if (!AllowedScale(percent)) return Result::Fail("invalid_scale", "Scale must be one of the supported percentages.");
        if (ctx_.applyScale && !ctx_.applyScale(percent))
            return Result::Fail("apply_failed", "Scale could not be applied.");
        return Result::Ok(json{{"percent", percent}});
    });
    b.Register("language.set", [this](const json& params) {
        const int language = RequireInt(params, "language");
        if (language < 0 || language > 3) return Result::Fail("invalid_language", "Language must be 0..3.");
        const bool saved = ctx_.setLanguage ? ctx_.setLanguage(static_cast<uint32_t>(language)) : false;
        if (ctx_.setSaveFailed) ctx_.setSaveFailed(!saved);
        bridge_.EmitEvent("shell.saveFailed", json{{"failed", !saved}});
        if (saved) {
            bridge_.EmitEvent("language.changed", LanguageJson());
            bridge_.EmitEvent("strings.changed", StringsJson());
        }
        if (!saved) return Result::Fail("save_failed", "Could not save the configuration.");
        return Result::Ok(LanguageJson());
    });

    b.Register("hotkeys.setOverride", [this, config](const json& params) {
        const auto plugin = RequireString(params, "pluginId");
        const auto action = RequireString(params, "actionId");
        uint32_t vk = 0, mods = 0;
        if (params.contains("combo") && params["combo"].is_string()) {
            const auto combo = params["combo"].get<std::string>();
            if (!combo.empty()) {
                if (!Hotkeys::ParseCombo(Widen(combo), vk, mods))
                    return Result::Fail("invalid_combo", "Invalid combo or reserved F1.");
                if (vk == Hotkeys::kManagerKey) return Result::Fail("invalid_combo", "Invalid combo or reserved F1.");
            }
        } else if (params.contains("virtualKey") && params["virtualKey"].is_number_integer()) {
            vk = static_cast<uint32_t>(params["virtualKey"].get<int>());
            mods = params.value("modifiers", 0u);
            if (vk == Hotkeys::kManagerKey) return Result::Fail("invalid_combo", "Invalid combo or reserved F1.");
        } else {
            return Result::Fail("invalid_request", "Provide combo or virtualKey.");
        }
        const auto status = Hotkeys::SetOverride(config, plugin.c_str(), action.c_str(), vk, mods);
        const bool ok = status == GAKU_OK;
        if (ctx_.setSaveFailed) ctx_.setSaveFailed(!ok);
        bridge_.EmitEvent("shell.saveFailed", json{{"failed", !ok}});
        bridge_.EmitEvent("hotkeys.rowsChanged", RowsJson());
        if (!ok) return Result::Fail("save_failed", "Could not save the configuration.", json{{"status", status}});
        return Result::Ok(json{{"virtualKey", vk}, {"modifiers", mods}});
    });
    b.Register("hotkeys.unbind", [this, config](const json& params) {
        const auto plugin = RequireString(params, "pluginId");
        const auto action = RequireString(params, "actionId");
        const auto status = Hotkeys::SetOverride(config, plugin.c_str(), action.c_str(), 0, 0);
        const bool ok = status == GAKU_OK;
        if (ctx_.setSaveFailed) ctx_.setSaveFailed(!ok);
        bridge_.EmitEvent("shell.saveFailed", json{{"failed", !ok}});
        bridge_.EmitEvent("hotkeys.rowsChanged", RowsJson());
        if (!ok) return Result::Fail("save_failed", "Could not save the configuration.");
        return Result::Ok(json::object());
    });
    b.Register("hotkeys.resetOverride", [this, config](const json& params) {
        const auto plugin = RequireString(params, "pluginId");
        const auto action = RequireString(params, "actionId");
        const auto status = Hotkeys::ResetOverride(config, plugin.c_str(), action.c_str());
        const bool ok = status == GAKU_OK;
        if (ctx_.setSaveFailed) ctx_.setSaveFailed(!ok);
        bridge_.EmitEvent("shell.saveFailed", json{{"failed", !ok}});
        bridge_.EmitEvent("hotkeys.rowsChanged", RowsJson());
        if (!ok) return Result::Fail("save_failed", "Could not save the configuration.");
        return Result::Ok(json::object());
    });
    b.Register("hotkeys.setEditorActive", [this](const json& params) {
        SetEditorActive(RequireBool(params, "active"));
        return Result::Ok(json::object());
    });
    b.Register("hotkeys.beginCapture", [this](const json& params) {
        const auto plugin = RequireString(params, "pluginId");
        const auto action = RequireString(params, "actionId");
        std::string combo;
        if (params.contains("combo")) {
            if (!params["combo"].is_string()) return Result::Fail("invalid_request", "combo must be a string.");
            combo = params["combo"].get<std::string>();
        }
        const auto rows = Hotkeys::Snapshot();
        const bool known = std::any_of(rows.begin(), rows.end(), [&](const Hotkeys::Row& row) {
            return row.pluginId == plugin && row.actionId == action;
        });
        if (!known) return Result::Fail("unknown_hotkey", "Hotkey action is not registered.");
        BeginCapture(plugin, action, combo);
        return Result::Ok(json{{"capturing", true}});
    });
    b.Register("hotkeys.endCapture", [this](const json&) {
        EndCapture(true);
        return Result::Ok(json::object());
    });

    b.Register("settings.setConsole", [this, config](const json& params) {
        const bool enabled = RequireBool(params, "enabled");
        const bool ok = WritePrivateProfileStringW(L"host", L"console", enabled ? L"true" : L"false",
            config.c_str()) != FALSE;
        PushSettingsResult(ok, "console");
        if (!ok) return Result::Fail("save_failed", "Could not save the configuration.");
        return Result::Ok(json{{"console", enabled}});
    });
    b.Register("settings.setTrace", [this, config](const json& params) {
        const bool enabled = RequireBool(params, "enabled");
        const bool ok = WritePrivateProfileStringW(L"diagnostics", L"trace", enabled ? L"true" : L"false",
            config.c_str()) != FALSE;
        PushSettingsResult(ok, "trace");
        if (!ok) return Result::Fail("save_failed", "Could not save the configuration.");
        return Result::Ok(json{{"trace", enabled}});
    });
    b.Register("settings.setPluginEnabled", [this, config](const json& params) {
        const auto filename = RequireString(params, "filename");
        const bool enabled = RequireBool(params, "enabled");
        DesktopHostSnapshot snapshot;
        if (!ctx_.snapshot || !ctx_.snapshot(snapshot))
            return Result::Fail("unavailable", "Plugin list is unavailable.");
        const auto wide = Widen(filename);
        const bool known = std::any_of(snapshot.pluginFiles.begin(), snapshot.pluginFiles.end(),
            [&](const std::wstring& file) { return lstrcmpiW(file.c_str(), wide.c_str()) == 0; });
        if (!known) return Result::Fail("unknown_plugin", "Plugin file was not discovered.");
        const bool ok = WritePrivateProfileStringW(L"plugins", Widen(filename).c_str(),
            enabled ? L"true" : L"false", config.c_str()) != FALSE;
        PushSettingsResult(ok, filename);
        if (!ok) return Result::Fail("save_failed", "Could not save the configuration.");
        return Result::Ok(json{{"filename", filename}, {"enabled", enabled}});
    });
}

void Backend::BeginCapture(const std::string& pluginId, const std::string& actionId, const std::string& combo) {
    editorActive_ = true;
    capturing_ = true;
    capInvalid_ = false;
    capPlugin_ = pluginId;
    capAction_ = actionId;
    capCombo_ = combo;
    for (int i = 0; i < 256; ++i) capDown_[i] = (GetAsyncKeyState(i) & 0x8000) != 0;
    Hotkeys::SetCaptureActive(true);
    bridge_.EmitEvent("hotkeys.captureStateChanged",
        json{{"capturing", true}, {"pluginId", pluginId}, {"actionId", actionId}});
}

void Backend::EndCapture(bool cancelled) {
    if (!capturing_) return;
    capturing_ = false;
    Hotkeys::SetCaptureActive(false);
    bridge_.EmitEvent("hotkeys.captureStateChanged",
        json{{"capturing", false}, {"pluginId", capPlugin_}, {"actionId", capAction_}});
    if (cancelled) bridge_.EmitEvent("hotkeys.captureCancelled", json::object());
}

void Backend::SetEditorActive(bool active) {
    editorActive_ = active;
    if (!active) {
        EndCapture(true);
        Hotkeys::SetCaptureActive(false);
        return;
    }
    const HWND host = ctx_.hostWindow ? ctx_.hostWindow() : nullptr;
    Hotkeys::SetCaptureActive(host && GetForegroundWindow() == host);
}

void Backend::PollCapture() {
    if (!capturing_) return;
    const HWND host = ctx_.hostWindow ? ctx_.hostWindow() : nullptr;
    if (!host || GetForegroundWindow() != host) { EndCapture(true); return; }
    // Re-assert each loop so a focus message that clears capture self-heals,
    // exactly as the ImGui editor does while its modal is open.
    Hotkeys::SetCaptureActive(true);
    uint32_t modifiers = 0;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) modifiers |= GAKU_HOTKEY_MOD_CTRL;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) modifiers |= GAKU_HOTKEY_MOD_SHIFT;
    if (GetAsyncKeyState(VK_MENU) & 0x8000) modifiers |= GAKU_HOTKEY_MOD_ALT;
    if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) modifiers |= GAKU_HOTKEY_MOD_WIN;
    for (uint32_t key = 8; key < 256; ++key) {
        const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
        const bool edge = down && !capDown_[key];
        capDown_[key] = down;
        if (!edge || key == VK_CONTROL || key == VK_SHIFT || key == VK_MENU ||
            (key >= VK_LSHIFT && key <= VK_RMENU) || key == VK_LWIN || key == VK_RWIN) continue;
        if (key == VK_ESCAPE) { EndCapture(true); return; }
        const auto combo = Hotkeys::ComboName(key, modifiers);
        uint32_t parsedKey = 0, parsedMods = 0;
        if (!Hotkeys::ParseCombo(std::wstring(combo.begin(), combo.end()), parsedKey, parsedMods)) continue;
        capCombo_ = combo;
        capInvalid_ = key == Hotkeys::kManagerKey;
        capturing_ = false;
        Hotkeys::SetCaptureActive(false);
        bridge_.EmitEvent("hotkeys.captureStateChanged",
            json{{"capturing", false}, {"pluginId", capPlugin_}, {"actionId", capAction_}});
        bridge_.EmitEvent("hotkeys.captured", json{{"combo", capCombo_}, {"invalid", capInvalid_}});
        return;
    }
}

void Backend::EmitSnapshot(bool force) {
    DesktopHostSnapshot snapshot;
    if (!ctx_.snapshot || !ctx_.snapshot(snapshot)) return;
    std::string signature = (snapshot.runtimeReady ? "R" : "r");
    signature += snapshot.shuttingDown ? "S" : "s";
    signature += snapshot.activated ? "A" : "a";
    signature += ":L" + std::to_string(LanguageSettings::Current());
    for (const auto& plugin : snapshot.plugins) {
        signature += "|" + plugin.id + ":" + plugin.name + ":" + plugin.version + ":" +
            (plugin.activated ? "1" : "0") + (plugin.runtimeReady ? "1" : "0") +
            (plugin.failed ? "1" : "0") + (plugin.hasPanel ? "1" : "0") + ":" +
            std::to_string(plugin.loadState) + ":" + plugin.statusReason + ":" +
            plugin.dllFilename + ":" + (plugin.hasMetadata ? "1" : "0") + ":" +
            plugin.subVersion + ":" + plugin.releaseChannel + ":" + plugin.summary + ":" +
            plugin.originalAuthor + ":" + plugin.repositoryUrl + ":" + plugin.releaseUrl + ":" +
            plugin.issuesUrl + ":" + plugin.license + ":" + plugin.minLoaderVersion;
        for (const auto& m : plugin.maintainers) signature += "~" + m;
        for (const auto& loc : plugin.localizedSummaries) signature += "^" + loc.first + "=" + loc.second;
        for (const auto& dep : plugin.dependencies) {
            signature += ";" + std::to_string(dep.kind) + ":" + dep.pluginId + ":" +
                dep.displayName + ":" + dep.versionRange + ":" + std::to_string(dep.targetState) + ":" +
                dep.actualVersion + ":" + (dep.satisfied ? "1" : "0") +
                (dep.cycleDegraded ? "1" : "0") + ":" + dep.diagnostic;
        }
    }
    for (const auto& file : snapshot.pluginFiles) {
        signature += "#" + Narrow(file);
    }
    for (const auto& file : snapshot.auxiliaryFiles) {
        signature += "@" + Narrow(file);
    }
    if (!force && signature == lastSnapshotSig_) return;
    lastSnapshotSig_ = signature;
    bridge_.EmitEvent("host.snapshotChanged", SnapshotJson(snapshot));
}

void Backend::EmitAppearance(bool force) {
    if (!ctx_.getAppearance) return;
    const auto state = ctx_.getAppearance();
    if (!force && haveAppearance_ && state.theme == lastAppearance_.theme &&
        std::fabs(state.uiScale - lastAppearance_.uiScale) < 0.001f &&
        state.scaleLimited == lastAppearance_.scaleLimited) return;
    haveAppearance_ = true;
    lastAppearance_ = state;
    bridge_.EmitEvent("appearance.changed", AppearanceJson(state));
}

void Backend::EmitSelection(bool force) {
    const auto selected = ctx_.selectedPanel ? ctx_.selectedPanel() : ctx_.managerPanelId;
    if (!force && selected == lastSelected_) return;
    lastSelected_ = selected;
    bridge_.EmitEvent("shell.panelSelected", json{{"id", selected}});
}

void Backend::EmitSaveFailed(bool force) {
    const bool failed = ctx_.getSaveFailed && ctx_.getSaveFailed();
    if (!force && haveSaveFailed_ && failed == lastSaveFailed_) return;
    haveSaveFailed_ = true;
    lastSaveFailed_ = failed;
    bridge_.EmitEvent("shell.saveFailed", json{{"failed", failed}});
}

void Backend::Tick() {
    if (editorActive_ && !capturing_) {
        const HWND host = ctx_.hostWindow ? ctx_.hostWindow() : nullptr;
        Hotkeys::SetCaptureActive(host && GetForegroundWindow() == host);
    }
    PollCapture();
    EmitSnapshot(false);
    EmitAppearance(false);
    EmitSelection(false);
    EmitSaveFailed(false);
}

void Backend::OnDocumentReady() {
    bridge_.Reset();
    EndCapture(true);
    editorActive_ = false;
    Hotkeys::SetCaptureActive(false);
    bridge_.EmitEvent("shell.ready", json{{"mode", ctx_.uiMode ? ctx_.uiMode() : "webview2"},
        {"managerPanelId", ctx_.managerPanelId}});
    EmitSnapshot(true);
    EmitAppearance(true);
    EmitSelection(true);
    EmitSaveFailed(true);
    bridge_.EmitEvent("language.changed", LanguageJson());
    bridge_.EmitEvent("strings.changed", StringsJson());
    bridge_.EmitEvent("hotkeys.rowsChanged", RowsJson());
    bridge_.EmitEvent("settings.changed", SettingsJson());
}

void Backend::Shutdown() {
    EndCapture(true);
    editorActive_ = false;
    Hotkeys::SetCaptureActive(false);
    bridge_.Invalidate("host_shutdown");
}

} // namespace GakuLoader::ManagerBackend
