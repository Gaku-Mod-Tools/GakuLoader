// Headless contract tests for the Manager WebView2 bridge core and backend.
//
// Compiles the production ManagerBridge.cpp, ManagerBackend.cpp and Hotkeys.cpp
// against minimal host stubs (no PluginHost.cpp, no ImGui, no WebView2 runtime).
// Host state is driven through the backend Context hooks; every persistence
// assertion runs against a temporary gakuloader.ini under GetTempPath().

#include "ManagerBridge.hpp"
#include "ManagerBackend.hpp"
#include "Hotkeys.hpp"
#include "LanguageSettings.hpp"
#include "HostConfig.hpp"
#include "ManagerStrings.hpp"
#include "PluginMetadata.hpp"
#include <windows.h>
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;
using GakuLoader::ManagerBridge::Bridge;
using GakuLoader::ManagerBridge::Result;
using GakuLoader::ManagerBackend::AppearanceState;
using GakuLoader::ManagerBackend::Backend;
using GakuLoader::ManagerBackend::Context;

// ---------------------------------------------------------------------------
// Host stubs. These are the only GakuLoader free functions the three compiled
// production TUs reference; PluginHost.cpp is deliberately not linked.
// ---------------------------------------------------------------------------
namespace GakuLoader {
void LogInfo(const char* format, ...) {
    char buffer[2048];
    va_list args; va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    (void)buffer;
}
void LogWarn(const char* format, ...) {
    char buffer[2048];
    va_list args; va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    (void)buffer;
}
void LogError(const char* format, ...) {
    char buffer[2048];
    va_list args; va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    (void)buffer;
}
void LogDebug(const char* format, ...) {
    char buffer[2048];
    va_list args; va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    (void)buffer;
}
bool SnapshotDesktopHost(DesktopHostSnapshot&) { return false; }
} // namespace GakuLoader

namespace {

int g_checks = 0;
void Check(bool okay, const char* what) {
    ++g_checks;
    if (!okay) throw std::runtime_error(std::string("check failed: ") + what);
}

// ---------------------------------------------------------------------------
// Request/response helpers
// ---------------------------------------------------------------------------
json RequestMsg(uint64_t id, const std::string& method, json params = json::object()) {
    return json{{"v", 1}, {"id", id}, {"type", "request"}, {"method", method}, {"params", std::move(params)}};
}

struct RawReply { bool threw = false; std::string text; };
RawReply SendRaw(Bridge& bridge, const std::string& message) {
    RawReply out;
    try { out.text = bridge.HandleMessage(message); }
    catch (...) { out.threw = true; }
    return out;
}

json Call(Bridge& bridge, uint64_t id, const std::string& method, json params = json::object()) {
    const auto raw = SendRaw(bridge, RequestMsg(id, method, std::move(params)).dump());
    Check(!raw.threw, "HandleMessage must not throw for a well-formed request");
    Check(!raw.text.empty(), "a request must produce a response");
    const auto body = json::parse(raw.text);
    Check(body.value("type", "") == "response", "response type field");
    Check(body.value("v", 0u) == 1u, "response protocol version");
    Check(body.contains("id") && body["id"].is_number_unsigned() &&
        body["id"].get<uint64_t>() == id, "response echoes the request id");
    return body;
}

json CallOk(Bridge& bridge, uint64_t id, const std::string& method, json params = json::object()) {
    const auto body = Call(bridge, id, method, std::move(params));
    Check(body.value("ok", false), "request expected to succeed");
    Check(body.contains("result"), "success carries a result");
    return body["result"];
}

json CallErr(Bridge& bridge, uint64_t id, const std::string& method, const std::string& code,
    json params = json::object()) {
    const auto body = Call(bridge, id, method, std::move(params));
    Check(!body.value("ok", true), "request expected to fail");
    Check(body.contains("error") && body["error"].is_object(), "failure carries an error object");
    const auto& error = body["error"];
    Check(error.value("code", "") == code, "structured error code");
    Check(error.contains("message") && error["message"].is_string() &&
        !error["message"].get<std::string>().empty(), "error message is non-empty");
    return error;
}

json RawBody(Bridge& bridge, const json& message, const char* what) {
    const auto raw = SendRaw(bridge, message.dump());
    Check(!raw.threw, what);
    Check(!raw.text.empty(), what);
    return json::parse(raw.text);
}

json RawErr(Bridge& bridge, const json& message, uint64_t id, const std::string& code, const char* what) {
    const auto body = RawBody(bridge, message, what);
    Check(body.value("type", "") == "response", what);
    Check(body.value("v", 0u) == 1u, what);
    Check(body.contains("id") && body["id"].is_number_unsigned() && body["id"].get<uint64_t>() == id, what);
    Check(body.contains("ok") && body["ok"].is_boolean() && !body["ok"].get<bool>(), what);
    Check(body.contains("error") && body["error"].is_object(), what);
    const auto& error = body["error"];
    Check(error.value("code", "") == code, what);
    Check(error.contains("message") && error["message"].is_string() &&
        !error["message"].get<std::string>().empty(), what);
    return error;
}

// ---------------------------------------------------------------------------
// Event capture
// ---------------------------------------------------------------------------
struct EventLog {
    std::vector<json> items;
    void Attach(Bridge& bridge) {
        bridge.SetSink([this](const std::string& text) { items.push_back(json::parse(text)); });
    }
    void Clear() { items.clear(); }
    size_t Count(const std::string& name) const {
        return static_cast<size_t>(std::count_if(items.begin(), items.end(),
            [&](const json& e) { return e.value("name", "") == name; }));
    }
    bool Has(const std::string& name) const { return Count(name) != 0; }
    json Last(const std::string& name) const {
        for (auto it = items.rbegin(); it != items.rend(); ++it)
            if (it->value("name", "") == name) return (*it)["payload"];
        throw std::runtime_error("expected event missing: " + name);
    }
    void CheckEnvelope(uint64_t sid) const {
        uint64_t expectedSeq = items.empty() ? 0 : items.front().value("seq", 0ull);
        for (const auto& event : items) {
            Check(event.value("v", 0u) == 1u, "event protocol version");
            Check(event.value("type", "") == "event", "event type field");
            Check(event.contains("name") && event["name"].is_string(), "event name field");
            Check(event.contains("payload") && event["payload"].is_object(), "event payload is an object");
            Check(event.value("seq", 0ull) == expectedSeq, "event seq increments contiguously");
            Check(event.value("sid", 0ull) == sid, "event carries the current session id");
            ++expectedSeq;
        }
    }
};

// ---------------------------------------------------------------------------
// INI helpers (mirror Hotkeys.cpp IdentityKey encoding)
// ---------------------------------------------------------------------------
std::string HexIdentity(const std::string& text) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (const unsigned char byte : text) {
        out += digits[byte >> 4];
        out += digits[byte & 15];
    }
    return out;
}
std::wstring HotkeyIniKey(const std::string& pluginId, const std::string& actionId) {
    const auto ascii = "v2:" + HexIdentity(pluginId) + ":" + HexIdentity(actionId);
    return std::wstring(ascii.begin(), ascii.end());
}
std::wstring IniValue(const fs::path& config, const wchar_t* section, const std::wstring& key) {
    return GakuLoader::ReadConfigValue(config, section, key.c_str());
}
json RowById(const json& rowsPayload, const std::string& pluginId, const std::string& actionId) {
    for (const auto& row : rowsPayload["rows"])
        if (row.value("pluginId", "") == pluginId && row.value("actionId", "") == actionId) return row;
    throw std::runtime_error("hotkey row missing: " + pluginId + "." + actionId);
}
json PluginEntry(const json& settings, const std::string& filename) {
    for (const auto& entry : settings["plugins"])
        if (entry.value("filename", "") == filename) return entry;
    throw std::runtime_error("settings plugin entry missing: " + filename);
}

// ---------------------------------------------------------------------------
// Bridge core
// ---------------------------------------------------------------------------
void TestBridgeCore() {
    Bridge bridge;
    EventLog log;
    Check(bridge.sessionId() == 1, "constructor opens session 1");
    Check(bridge.eventSeq() == 0, "fresh session has no events");
    Check(!bridge.closed(), "fresh bridge is open");
    Check(bridge.HasMethod("bridge.hello") && bridge.HasMethod("bridge.ping"), "core handshake methods");
    Check(!bridge.HasMethod("no.such.method"), "HasMethod is false for unknown names");

    const auto hello = CallOk(bridge, 7, "bridge.hello");
    Check(hello.value("protocol", 0u) == 1u, "hello reports protocol 1");
    Check(hello.value("sessionId", 0ull) == bridge.sessionId(), "hello reports the session id");
    const auto ping = CallOk(bridge, 8, "bridge.ping");
    Check(ping.value("pong", false), "ping answers pong");

    CallErr(bridge, 9, "no.such.method", "unknown_method");

    const auto badJson = SendRaw(bridge, "{definitely not json");
    Check(!badJson.threw && !badJson.text.empty(), "invalid JSON still answers");
    const auto badBody = json::parse(badJson.text);
    Check(badBody.value("id", 999ull) == 0ull, "invalid JSON correlates to id 0");
    Check(badBody["error"].value("code", "") == "invalid_json", "invalid JSON error code");
    const auto nonObject = json::parse(SendRaw(bridge, "[1,2,3]").text);
    Check(nonObject["error"].value("code", "") == "invalid_request", "non-object message rejected");

    auto missingType = RequestMsg(40, "bridge.ping");
    missingType.erase("type");
    RawErr(bridge, missingType, 0, "invalid_request", "missing type rejected");
    const json badTypes[] = {123, nullptr, true, json::array(), json::object()};
    for (const auto& value : badTypes) {
        auto message = RequestMsg(41, "bridge.ping");
        message["type"] = value;
        RawErr(bridge, message, 0, "invalid_request", "non-string type rejected");
    }
    auto unsolicited = json{{"v", 1}, {"type", "event"}, {"name", "frontend.event"}};
    Check(SendRaw(bridge, unsolicited.dump()).text.empty(), "non-request types are ignored");

    auto missingId = RequestMsg(42, "bridge.ping");
    missingId.erase("id");
    RawErr(bridge, missingId, 0, "invalid_request", "missing id rejected");
    const json badIds[] = {-3, "7", 1.5, true, nullptr, json::array(), json::object()};
    for (const auto& value : badIds) {
        auto message = RequestMsg(43, "bridge.ping");
        message["id"] = value;
        RawErr(bridge, message, 0, "invalid_request", "invalid id rejected");
    }
    CallOk(bridge, 44, "bridge.ping");
    CallOk(bridge, 18446744073709551615ull, "bridge.ping");

    CallErr(bridge, 45, "", "invalid_request");
    auto missingMethod = RequestMsg(46, "bridge.ping");
    missingMethod.erase("method");
    RawErr(bridge, missingMethod, 46, "invalid_request", "absent method rejected");
    const json badMethods[] = {42, nullptr, true, json::array(), json::object()};
    for (const auto& value : badMethods) {
        auto message = RequestMsg(47, "bridge.ping");
        message["method"] = value;
        RawErr(bridge, message, 47, "invalid_request", "non-string method rejected");
    }

    // Handler exceptions map to internal_error, never escape.
    bridge.Register("test.throwStd", [](const json&) -> Result { throw std::runtime_error("boom"); });
    const auto stdError = CallErr(bridge, 12, "test.throwStd", "internal_error");
    Check(stdError.value("message", "") == "boom", "std::exception what() travels in the error");
    bridge.Register("test.throwInt", [](const json&) -> Result { throw 42; });
    const auto intError = CallErr(bridge, 13, "test.throwInt", "internal_error");
    Check(intError.value("message", "") == "Unknown handler exception.", "non-std exception mapped");

    bridge.Register("test.nullOk", [](const json&) { return Result::Ok(nullptr); });
    const auto nullOk = CallOk(bridge, 26, "test.nullOk");
    Check(nullOk.is_object() && nullOk.empty(), "null success result normalizes to an empty object");
    bridge.Register("test.failDetails", [](const json&) {
        return Result::Fail("custom_code", "custom message", json{{"detail", 7}});
    });
    const auto details = CallErr(bridge, 27, "test.failDetails", "custom_code");
    Check(details.contains("details") && details["details"].value("detail", 0) == 7, "error details pass through");

    // Params normalization and handler replacement.
    bridge.Register("test.params", [](const json& params) {
        return Result::Ok(json{{"isObject", params.is_object()}, {"size", params.size()}});
    });
    auto noParams = RequestMsg(60, "test.params");
    noParams.erase("params");
    const auto normalized = RawBody(bridge, noParams, "absent params answered")["result"];
    Check(normalized.value("isObject", false) && normalized.value("size", 99) == 0, "absent params -> object");
    const json badParams[] = {nullptr, "text", 42, true, json::array()};
    for (const auto& value : badParams)
        CallErr(bridge, 61, "test.params", "invalid_request", value);
    const auto sized = CallOk(bridge, 62, "test.params", json{{"a", 1}, {"b", 2}});
    Check(sized.value("size", 0) == 2, "object params pass through");
    bridge.Register("test.params", [](const json&) { return Result::Ok(json{{"replaced", true}}); });
    Check(CallOk(bridge, 63, "test.params").value("replaced", false), "Register replaces a handler");

    auto missingVersion = RequestMsg(70, "bridge.ping");
    missingVersion.erase("v");
    RawErr(bridge, missingVersion, 70, "invalid_request", "absent protocol version rejected");
    const json badVersions[] = {"1", 1.5, true, nullptr, json::array(), json::object()};
    for (const auto& value : badVersions) {
        auto message = RequestMsg(71, "bridge.ping");
        message["v"] = value;
        RawErr(bridge, message, 71, "invalid_request", "non-integer protocol version rejected");
    }
    const int unsupportedVersions[] = {0, 2, -1};
    for (const int version : unsupportedVersions) {
        auto message = RequestMsg(72, "bridge.ping");
        message["v"] = version;
        RawErr(bridge, message, 72, "unsupported_protocol", "unsupported protocol version rejected");
    }

    // Events: no sink -> no sequence burn; with sink -> seq/sid envelope.
    Bridge silent;
    silent.EmitEvent("ignored", json{{"x", 1}});
    Check(silent.eventSeq() == 0, "EmitEvent without a sink does not advance seq");
    silent.Invalidate("silent_close");
    Check(silent.closed() && silent.eventSeq() == 0, "Invalidate without a sink does not advance seq");
    log.Attach(bridge);
    bridge.EmitEvent("test.first", json{{"n", 1}});
    bridge.EmitEvent("test.second", nullptr);
    bridge.EmitEvent("test.third", json{{"n", 3}});
    Check(bridge.eventSeq() == 3, "three events advance seq to 3");
    log.CheckEnvelope(bridge.sessionId());
    Check(log.items[1]["payload"].is_object() && log.items[1]["payload"].empty(), "null payload -> empty object");

    Bridge throwing;
    throwing.SetSink([](const std::string&) { throw std::runtime_error("sink failure"); });
    throwing.EmitEvent("sink.first", json::object());
    Check(throwing.eventSeq() == 1, "throwing sink still advances seq");
    throwing.EmitEvent("sink.second", json::object());
    Check(throwing.eventSeq() == 2, "sink exceptions are swallowed");
    throwing.Invalidate("sink_close");
    Check(throwing.closed() && throwing.eventSeq() == 3, "close event seq advances despite a throwing sink");
    throwing.EmitEvent("sink.after", json::object());
    Check(throwing.eventSeq() == 3, "closed throwing sink emits nothing more");

    // Invalidate: one bridge.closed, then requests reject and events stop.
    bridge.Invalidate("test_close");
    Check(bridge.closed(), "Invalidate closes the bridge");
    Check(log.Count("bridge.closed") == 1, "bridge.closed emitted once");
    Check(log.Last("bridge.closed").value("reason", "") == "test_close", "close reason travels");
    CallErr(bridge, 30, "bridge.ping", "shutting_down");
    CallErr(bridge, 31, "test.params", "shutting_down");
    const auto eventsAtClose = log.items.size();
    bridge.EmitEvent("after.close", json::object());
    bridge.Invalidate("again");
    Check(log.items.size() == eventsAtClose, "closed bridge emits nothing more");

    // Reset reopens with a fresh session.
    bridge.Reset();
    Check(!bridge.closed(), "Reset reopens the bridge");
    Check(bridge.sessionId() == 2, "Reset bumps the session id");
    Check(bridge.eventSeq() == 0, "Reset zeroes seq");
    log.Clear();
    CallOk(bridge, 32, "bridge.ping");
    bridge.EmitEvent("test.reopened", json::object());
    log.CheckEnvelope(2);
    Check(log.items.size() == 1 && log.items[0].value("seq", 0ull) == 1ull, "reopened session seq starts at 1");
    std::puts("ok: bridge core framing, correlation, errors, events, lifecycle");
}

// ---------------------------------------------------------------------------
// Backend fixture
// ---------------------------------------------------------------------------
struct StubHost {
    fs::path config;
    GakuLoader::DesktopHostSnapshot snapshot;
    bool snapshotAvailable = true;
    bool shuttingDown = false;
    std::string selected = "gakuloader.manager";
    std::string mode = "webview2";
    bool webviewReady = true;
    AppearanceState appearance{1, 1.25f, false, 1.5f};
    bool saveFailed = false;
    bool applyThemeOk = true, applyScaleOk = true, saveLanguageOk = true;
    std::vector<int> themes, scales;
    std::vector<uint32_t> languages;
    std::vector<std::string> selections;
    std::vector<bool> textInput;
    int hides = 0;
};

Context MakeContext(StubHost& host) {
    Context ctx;
    ctx.configPath = host.config;
    ctx.managerPanelId = "gakuloader.manager";
    ctx.snapshot = [&host](GakuLoader::DesktopHostSnapshot& out) {
        if (!host.snapshotAvailable) return false;
        out = host.snapshot;
        return true;
    };
    ctx.shuttingDown = [&host] { return host.shuttingDown; };
    ctx.selectedPanel = [&host] { return host.selected; };
    ctx.selectPanel = [&host](const std::string& id) { host.selections.push_back(id); host.selected = id; };
    ctx.hideWindow = [&host] { ++host.hides; };
    ctx.hostWindow = [] { return static_cast<HWND>(nullptr); }; // headless: capture cancels on Tick
    ctx.uiMode = [&host] { return host.mode; };
    ctx.webviewReady = [&host] { return host.webviewReady; };
    ctx.getAppearance = [&host] { return host.appearance; };
    ctx.applyTheme = [&host](int theme) {
        host.themes.push_back(theme);
        if (!host.applyThemeOk) return false;
        host.appearance.theme = theme;
        return true;
    };
    ctx.applyScale = [&host](int percent) {
        host.scales.push_back(percent);
        if (!host.applyScaleOk) return false;
        host.appearance.uiScale = percent / 100.0f;
        return true;
    };
    ctx.setLanguage = [&host](uint32_t language) {
        host.languages.push_back(language);
        return host.saveLanguageOk && GakuLoader::LanguageSettings::Save(host.config, language);
    };
    ctx.setTextInput = [&host](bool active) { host.textInput.push_back(active); };
    ctx.getSaveFailed = [&host] { return host.saveFailed; };
    ctx.setSaveFailed = [&host](bool failed) { host.saveFailed = failed; };
    return ctx;
}

GakuHotkeyBinding MakeBinding(const char* actionId, const char* displayName, uint32_t virtualKey, uint32_t modifiers) {
    GakuHotkeyBinding binding{};
    binding.abiVersion = GAKU_SERVICE_VERSION;
    binding.structSize = sizeof(binding);
    binding.actionId = actionId;
    binding.displayName = displayName;
    binding.virtualKey = virtualKey;
    binding.modifiers = modifiers;
    binding.callback = [](void*) {};
    return binding;
}

uint32_t g_pollKey = 0, g_pollModifiers = 0;

GakuLoader::Hotkeys::PollResult PollKey(uint32_t key, uint32_t modifiers = 0) {
    g_pollKey = key;
    g_pollModifiers = modifiers;
    GakuLoader::Hotkeys::PollContext context;
    context.foregroundOwnedByThisProcess = true;
    context.isKeyDown = [](uint32_t virtualKey) {
        if (virtualKey == g_pollKey) return true;
        if ((g_pollModifiers & GAKU_HOTKEY_MOD_CTRL) != 0 && virtualKey == VK_CONTROL) return true;
        if ((g_pollModifiers & GAKU_HOTKEY_MOD_SHIFT) != 0 && virtualKey == VK_SHIFT) return true;
        if ((g_pollModifiers & GAKU_HOTKEY_MOD_ALT) != 0 && virtualKey == VK_MENU) return true;
        if ((g_pollModifiers & GAKU_HOTKEY_MOD_WIN) != 0 && (virtualKey == VK_LWIN || virtualKey == VK_RWIN)) return true;
        return false;
    };
    GakuLoader::Hotkeys::PollContext released = context;
    released.isKeyDown = [](uint32_t) { return false; };
    GakuLoader::Hotkeys::PollResult ignored;
    GakuLoader::Hotkeys::Poll(released, ignored);
    GakuLoader::Hotkeys::PollResult output;
    GakuLoader::Hotkeys::Poll(context, output);
    return output;
}

void SeedSnapshot(StubHost& host) {
    host.snapshot.plugins.clear();
    host.snapshot.pluginFiles = {L"test.plugin.dll", L"other.plugin.dll", L"disabled.dll"};
    host.snapshot.auxiliaryFiles = {L"aux-helper.dll"};
    host.snapshot.runtimeReady = true;
    host.snapshot.shuttingDown = false;
    host.snapshot.activated = true;
    GakuLoader::DesktopPluginPanel first;
    first.id = "test.plugin"; first.name = "Test Plugin"; first.version = "1.2.3";
    first.activated = true; first.runtimeReady = true; first.failed = false; first.hasPanel = true;
    first.loadState = GAKU_PLUGIN_STATE_RUNTIME_READY;
    first.dllFilename = "test.plugin.dll";
    first.hasMetadata = true;
    first.originalAuthor = "Original Author";
    first.maintainers = {"Maintainer A", "Maintainer B"};
    first.subVersion = "build.7";
    first.releaseChannel = "stable";
    first.summary = "Default test summary";
    first.localizedSummaries = {{"en", "English test summary"}, {"zh-Hans", "ZH-Hans test summary"}};
    first.repositoryUrl = "https://example.com/repo";
    first.releaseUrl = "https://example.com/releases";
    first.issuesUrl = "https://example.com/issues";
    first.license = "MIT";
    first.minLoaderVersion = "1.1.0";
    first.dependencies = {{GAKU_DEPENDENCY_REQUIRED, "other.plugin", "Other Plugin", ">=1.0.0",
        GAKU_PLUGIN_STATE_FAILED, "", false, false, "Required dependency failed"}};
    GakuLoader::DesktopPluginPanel second;
    second.id = "other.plugin"; second.name = "Other Plugin"; second.version = "";
    second.activated = false; second.runtimeReady = false; second.failed = true; second.hasPanel = false;
    second.loadState = GAKU_PLUGIN_STATE_FAILED;
    second.dllFilename = "other.plugin.dll";
    host.snapshot.plugins = {first, second};
}

void TestBackendQueries(Backend& backend, StubHost& host) {
    auto& bridge = backend.bridge();
    const auto snapshot = CallOk(bridge, 100, "host.snapshot");
    Check(snapshot.value("available", false), "snapshot available via stub");
    Check(snapshot.value("runtimeReady", false) && snapshot.value("activated", false) &&
        !snapshot.value("shuttingDown", true), "snapshot host flags");
    Check(snapshot["plugins"].size() == 2, "snapshot plugin count");
    const auto& p0 = snapshot["plugins"][0];
    Check(p0.value("id", "") == "test.plugin", "snapshot plugin id");
    Check(p0.value("version", "") == "v1.2.3", "snapshot version formatted");
    Check(p0.value("hasPanel", false), "snapshot panel flag");
    Check(p0.value("loadState", 99u) == GAKU_PLUGIN_STATE_RUNTIME_READY, "snapshot loadState");
    Check(p0.value("subVersion", "") == "build.7", "snapshot subVersion");
    const auto langWide = GakuLoader::LanguageSettings::kTags[GakuLoader::LanguageSettings::Current()];
    std::string langTag;
    for (const wchar_t* p = langWide; *p; ++p) langTag.push_back(static_cast<char>(*p));
    const std::string expectedSummary = GakuLoader::Metadata::ResolveLocalizedSummary(
        host.snapshot.plugins[0].summary, host.snapshot.plugins[0].localizedSummaries, langTag);
    Check(p0.contains("summary") && p0.value("summary", "") == expectedSummary && !expectedSummary.empty(),
        "snapshot summary");
    Check(p0.value("originalAuthor", "") == "Original Author", "snapshot originalAuthor");
    Check(p0.contains("maintainers") && p0["maintainers"].is_array() && p0["maintainers"].size() == 2 &&
        p0["maintainers"][0] == "Maintainer A" && p0["maintainers"][1] == "Maintainer B", "snapshot maintainers");
    Check(p0.value("repositoryUrl", "") == "https://example.com/repo", "snapshot repositoryUrl");
    Check(p0.value("releaseUrl", "") == "https://example.com/releases", "snapshot releaseUrl");
    Check(p0.contains("dependencies") && p0["dependencies"].is_array() && p0["dependencies"].size() == 1,
        "snapshot dependencies");
    const auto& dep0 = p0["dependencies"][0];
    Check(dep0.value("kind", 0u) == GAKU_DEPENDENCY_REQUIRED &&
        dep0.value("pluginId", "") == "other.plugin" &&
        dep0.value("displayName", "") == "Other Plugin" &&
        dep0.value("versionRange", "") == ">=1.0.0" &&
        dep0.value("targetState", 99u) == GAKU_PLUGIN_STATE_FAILED &&
        !dep0.value("satisfied", true) &&
        !dep0.value("cycleDegraded", true) &&
        dep0.value("diagnostic", "") == "Required dependency failed", "snapshot dependency entry");
    Check(snapshot["plugins"][1].value("failed", false), "snapshot failed flag");
    Check(snapshot["plugins"][1].value("loadState", 99u) == GAKU_PLUGIN_STATE_FAILED, "snapshot second loadState");
    Check(snapshot["pluginFiles"].size() == 3 && snapshot["pluginFiles"][2] == "disabled.dll", "snapshot plugin files");
    Check(snapshot.contains("auxiliaryFiles") && snapshot["auxiliaryFiles"].is_array() &&
        snapshot["auxiliaryFiles"].size() == 1 && snapshot["auxiliaryFiles"][0] == "aux-helper.dll",
        "snapshot auxiliaryFiles");

    host.snapshotAvailable = false;
    const auto unavailable = CallOk(bridge, 101, "host.snapshot");
    Check(!unavailable.value("available", true), "unavailable snapshot reported");
    Check(unavailable["plugins"].empty() && unavailable["pluginFiles"].empty() &&
        unavailable["auxiliaryFiles"].empty(), "unavailable snapshot arrays empty");
    Check(!unavailable.value("shuttingDown", true), "unavailable snapshot not shutting down");
    host.shuttingDown = true;
    Check(CallOk(bridge, 102, "host.snapshot").value("shuttingDown", false), "shutdown flag from context hook");
    host.shuttingDown = false;
    host.snapshotAvailable = true;

    const auto appearance = CallOk(bridge, 103, "appearance.get");
    Check(appearance.value("theme", -1) == 1, "appearance theme from hook");
    Check(appearance.value("uiScale", 0.0) == 1.25, "appearance scale from hook");
    Check(!appearance.value("scaleLimited", true), "appearance scaleLimited from hook");
    Check(appearance.value("dpiScale", 0.0) == 1.5, "appearance dpi from hook");

    const auto language = CallOk(bridge, 104, "language.get");
    const uint32_t current = GakuLoader::LanguageSettings::Current();
    Check(language.value("language", 99u) == current, "language.get matches live preference");
    Check(language["names"].size() == 4, "four language names");
    const auto expectedTag = GakuLoader::LanguageSettings::kTags[current];
    Check(language.value("tag", "") == std::string(expectedTag, expectedTag + wcslen(expectedTag)), "language tag matches");

    const auto strings = CallOk(bridge, 105, "strings.get");
    Check(strings.value("language", 99u) == current, "strings language matches");
    Check(strings["strings"].contains("Manager"), "strings contain Manager key");
    Check(strings["strings"]["Manager"] == GakuLoader::ManagerStrings::Text(GakuLoader::ManagerStrings::Key::Manager),
        "Manager string matches the live language");

    const auto rows = CallOk(bridge, 106, "hotkeys.rows");
    const auto panelRow = RowById(rows, "test.plugin", "panel");
    Check(panelRow.value("combo", "") == "Ctrl+Alt+P", "default combo reported");
    Check(panelRow.value("conflicted", false), "shared default combo is conflicted");
    Check(!panelRow.value("overridden", true), "no override yet");
    Check(RowById(rows, "other.plugin", "panel").value("conflicted", false), "conflict marks both owners");

    const auto settings = CallOk(bridge, 107, "settings.get");
    Check(!settings.value("console", true) && !settings.value("trace", true), "settings default false on empty INI");
    Check(settings["plugins"].size() == 3, "settings list discovered files");
    Check(PluginEntry(settings, "disabled.dll").value("enabled", false), "plugin enable defaults true");

    const auto shell = CallOk(bridge, 108, "shell.state");
    Check(shell.value("selectedPanel", "") == "gakuloader.manager", "shell selection");
    Check(shell.value("managerPanelId", "") == "gakuloader.manager", "manager panel id");
    Check(shell.value("mode", "") == "webview2", "shell ui mode");
    Check(shell.value("webviewReady", false), "webview readiness from hook");
    Check(!shell.value("saveFailed", true), "save banner state");

    const auto configPath = CallOk(bridge, 109, "config.path");
    const auto utf8 = host.config.u8string();
    Check(configPath.value("path", "") == std::string(utf8.begin(), utf8.end()), "config.path is the temporary INI");
    std::puts("ok: backend queries (snapshot/appearance/language/strings/rows/settings/shell/config)");
}

void TestAppearanceCommands(Backend& backend, StubHost& host, EventLog& log) {
    auto& bridge = backend.bridge();
    for (int theme = 0; theme <= 4; ++theme) {
        const auto result = CallOk(bridge, 120 + theme, "appearance.setTheme", json{{"theme", theme}});
        Check(result.value("theme", -1) == theme, "setTheme echoes the accepted theme");
    }
    Check(host.themes.size() == 5 && host.themes.back() == 4, "applyTheme hook saw 0..4");
    Check(host.appearance.theme == 4, "stub appearance updated");
    CallErr(bridge, 125, "appearance.setTheme", "invalid_theme", json{{"theme", -1}});
    CallErr(bridge, 126, "appearance.setTheme", "invalid_theme", json{{"theme", 5}});
    CallErr(bridge, 127, "appearance.setTheme", "internal_error", json::object()); // missing param
    host.applyThemeOk = false;
    CallErr(bridge, 128, "appearance.setTheme", "apply_failed", json{{"theme", 2}});
    host.applyThemeOk = true;

    // All eleven canonical percentages stay valid under the planned tightening.
    const int canonical[] = {50, 75, 90, 100, 110, 125, 150, 175, 200, 250, 300};
    for (const int percent : canonical) {
        const auto result = CallOk(bridge, 130, "appearance.setScale", json{{"percent", percent}});
        Check(result.value("percent", 0) == percent, "canonical scale accepted");
    }
    Check(host.scales.size() == 11, "applyScale hook saw every canonical percent");
    CallErr(bridge, 131, "appearance.setScale", "invalid_scale", json{{"percent", 49}});
    CallErr(bridge, 132, "appearance.setScale", "invalid_scale", json{{"percent", 301}});
    CallErr(bridge, 133, "appearance.setScale", "invalid_scale", json{{"percent", 101}});
    CallErr(bridge, 135, "appearance.setScale", "invalid_scale", json{{"percent", 0}});
    CallErr(bridge, 136, "appearance.setScale", "invalid_scale", json{{"percent", -5}});
    CallErr(bridge, 137, "appearance.setScale", "internal_error", json{{"percent", "100"}});
    Check(host.scales.size() == 11, "rejected scales never reach the apply hook");
    host.applyScaleOk = false;
    CallErr(bridge, 134, "appearance.setScale", "apply_failed", json{{"percent", 100}});
    host.applyScaleOk = true;
    (void)log;
    std::puts("ok: appearance commands (valid ranges, rejections, apply failure)");
}

void TestLanguageCommands(Backend& backend, StubHost& host, EventLog& log) {
    auto& bridge = backend.bridge();
    log.Clear();
    const auto result = CallOk(bridge, 140, "language.set", json{{"language", 1}});
    Check(result.value("tag", "") == "zh-Hans", "language.set returns the new tag");
    Check(GakuLoader::LanguageSettings::Current() == 1, "live language updated");
    Check(IniValue(host.config, L"appearance", L"language") == L"zh-Hans", "language persisted to [appearance]");
    Check(log.Count("language.changed") == 1, "language.changed emitted");
    Check(log.Last("language.changed").value("language", 99u) == 1u, "language.changed payload");
    Check(log.Count("strings.changed") == 1, "strings.changed emitted");
    Check(log.Last("strings.changed")["strings"]["Manager"] == "\xE7\xAE\xA1\xE7\x90\x86\xE5\x99\xA8",
        "strings.changed carries the localized Manager label");
    Check(log.Count("shell.saveFailed") == 1 && !log.Last("shell.saveFailed").value("failed", true), "save banner cleared");
    log.CheckEnvelope(bridge.sessionId());

    CallOk(bridge, 141, "language.set", json{{"language", 0}});
    Check(IniValue(host.config, L"appearance", L"language") == L"en", "language switch persisted");
    CallErr(bridge, 142, "language.set", "invalid_language", json{{"language", 4}});
    CallErr(bridge, 143, "language.set", "invalid_language", json{{"language", -1}});

    log.Clear();
    host.saveLanguageOk = false;
    CallErr(bridge, 144, "language.set", "save_failed", json{{"language", 2}});
    Check(host.saveFailed, "failed save raises the banner");
    Check(log.Count("language.changed") == 0, "failed save emits no language change");
    Check(log.Last("shell.saveFailed").value("failed", false), "failed save banner event");
    Check(GakuLoader::LanguageSettings::Current() == 0, "failed save keeps the live preference");
    host.saveLanguageOk = true;
    CallOk(bridge, 145, "language.set", json{{"language", 0}});
    Check(!host.saveFailed, "successful save clears the banner");
    std::puts("ok: language commands (persistence, events, invalid values, save failure)");
}

void TestHotkeyCommands(Backend& backend, StubHost& host, EventLog& log) {
    auto& bridge = backend.bridge();
    const auto key = HotkeyIniKey("test.plugin", "panel");
    log.Clear();
    const auto result = CallOk(bridge, 150, "hotkeys.setOverride",
        json{{"pluginId", "test.plugin"}, {"actionId", "panel"}, {"combo", "Ctrl+Shift+M"}});
    Check(result.value("virtualKey", 0u) == 0x4Du, "override returns the virtual key");
    Check(result.value("modifiers", 0u) == (GAKU_HOTKEY_MOD_CTRL | GAKU_HOTKEY_MOD_SHIFT), "override returns modifiers");
    Check(IniValue(host.config, L"hotkeys", key) == L"Ctrl+Shift+M", "v2 hex key persisted");
    Check(log.Count("hotkeys.rowsChanged") == 1, "hotkeys.rowsChanged emitted");
    const auto row = RowById(log.Last("hotkeys.rowsChanged"), "test.plugin", "panel");
    Check(row.value("combo", "") == "Ctrl+Shift+M" && row.value("overridden", false), "row reflects the override");
    Check(!row.value("conflicted", true), "override clears the conflict");
    Check(!log.Last("shell.saveFailed").value("failed", true), "override save banner cleared");
    log.CheckEnvelope(bridge.sessionId());

    CallErr(bridge, 151, "hotkeys.setOverride", "invalid_combo",
        json{{"pluginId", "test.plugin"}, {"actionId", "panel"}, {"combo", "F1"}});
    CallErr(bridge, 152, "hotkeys.setOverride", "invalid_combo",
        json{{"pluginId", "test.plugin"}, {"actionId", "panel"}, {"combo", "Ctrl+F1"}});
    CallErr(bridge, 153, "hotkeys.setOverride", "invalid_combo",
        json{{"pluginId", "test.plugin"}, {"actionId", "panel"}, {"combo", "Foo+Bar"}});
    CallErr(bridge, 154, "hotkeys.setOverride", "invalid_combo",
        json{{"pluginId", "test.plugin"}, {"actionId", "panel"}, {"combo", "Ctrl+Shift"}});
    CallErr(bridge, 155, "hotkeys.setOverride", "invalid_request",
        json{{"pluginId", "test.plugin"}, {"actionId", "panel"}});
    Check(IniValue(host.config, L"hotkeys", key) == L"Ctrl+Shift+M", "rejected combos leave the INI untouched");

    const auto byVk = CallOk(bridge, 156, "hotkeys.setOverride",
        json{{"pluginId", "test.plugin"}, {"actionId", "panel"}, {"virtualKey", 0x4E}, {"modifiers", GAKU_HOTKEY_MOD_CTRL}});
    Check(byVk.value("virtualKey", 0u) == 0x4Eu, "virtualKey path accepted");
    Check(IniValue(host.config, L"hotkeys", key) == L"Ctrl+N", "virtualKey path persisted as combo text");
    CallErr(bridge, 157, "hotkeys.setOverride", "invalid_combo",
        json{{"pluginId", "test.plugin"}, {"actionId", "panel"}, {"virtualKey", 0x70}});
    const auto unregistered = CallErr(bridge, 158, "hotkeys.setOverride", "save_failed",
        json{{"pluginId", "ghost.plugin"}, {"actionId", "nope"}, {"combo", "Ctrl+Alt+G"}});
    Check(unregistered.contains("details") && unregistered["details"].value("status", 0) == GAKU_E_UNAVAILABLE,
        "unregistered identity reports E_UNAVAILABLE in details");

    log.Clear();
    CallOk(bridge, 160, "hotkeys.unbind", json{{"pluginId", "test.plugin"}, {"actionId", "extra"}});
    Check(IniValue(host.config, L"hotkeys", HotkeyIniKey("test.plugin", "extra")).empty(), "unbind writes an empty value");
    const auto unboundRow = RowById(log.Last("hotkeys.rowsChanged"), "test.plugin", "extra");
    Check(unboundRow.value("unbound", false) && unboundRow.value("combo", "x") == "", "unbound row reported");

    log.Clear();
    CallOk(bridge, 161, "hotkeys.resetOverride", json{{"pluginId", "test.plugin"}, {"actionId", "panel"}});
    Check(IniValue(host.config, L"hotkeys", key) == L"<default>", "reset writes <default>");
    const auto resetRow = RowById(log.Last("hotkeys.rowsChanged"), "test.plugin", "panel");
    Check(resetRow.value("combo", "") == "Ctrl+Alt+P" && !resetRow.value("overridden", true), "reset restores the plugin default");

    // Empty combo through setOverride is the unbind path.
    CallOk(bridge, 162, "hotkeys.setOverride",
        json{{"pluginId", "test.plugin"}, {"actionId", "extra"}, {"combo", ""}});
    Check(IniValue(host.config, L"hotkeys", HotkeyIniKey("test.plugin", "extra")).empty(), "empty combo unbinds");
    std::puts("ok: hotkey commands (override/unbind/reset persistence, F1 and garbage rejection)");
}

void TestSettingsCommands(Backend& backend, StubHost& host, EventLog& log) {
    auto& bridge = backend.bridge();
    log.Clear();
    const auto console = CallOk(bridge, 170, "settings.setConsole", json{{"enabled", true}});
    Check(console.value("console", false), "setConsole echoes the value");
    Check(IniValue(host.config, L"host", L"console") == L"true", "[host] console persisted");
    Check(log.Count("settings.saveResult") == 1, "settings.saveResult emitted");
    Check(log.Last("settings.saveResult").value("ok", false) &&
        log.Last("settings.saveResult").value("key", "") == "console", "saveResult payload");
    Check(log.Count("settings.changed") == 1, "settings.changed emitted");
    Check(log.Last("settings.changed").value("console", false), "settings.changed carries the new value");
    Check(!log.Last("shell.saveFailed").value("failed", true), "settings save banner cleared");
    log.CheckEnvelope(bridge.sessionId());
    CallOk(bridge, 171, "settings.setConsole", json{{"enabled", false}});
    Check(IniValue(host.config, L"host", L"console") == L"false", "console disable persisted");

    CallOk(bridge, 172, "settings.setTrace", json{{"enabled", true}});
    Check(IniValue(host.config, L"diagnostics", L"trace") == L"true", "[diagnostics] trace persisted");
    Check(log.Last("settings.saveResult").value("key", "") == "trace", "trace saveResult key");

    log.Clear();
    const auto plugin = CallOk(bridge, 173, "settings.setPluginEnabled",
        json{{"filename", "test.plugin.dll"}, {"enabled", false}});
    Check(plugin.value("filename", "") == "test.plugin.dll" && !plugin.value("enabled", true), "setPluginEnabled echoes");
    Check(IniValue(host.config, L"plugins", L"test.plugin.dll") == L"false", "[plugins] filename persisted");
    Check(log.Last("settings.saveResult").value("key", "") == "test.plugin.dll", "plugin saveResult key is the filename");
    Check(!PluginEntry(log.Last("settings.changed"), "test.plugin.dll").value("enabled", true), "settings.changed reflects disable");
    Check(PluginEntry(log.Last("settings.changed"), "other.plugin.dll").value("enabled", false), "untouched plugin stays enabled");
    CallOk(bridge, 174, "settings.setPluginEnabled", json{{"filename", "test.plugin.dll"}, {"enabled", true}});
    Check(IniValue(host.config, L"plugins", L"test.plugin.dll") == L"true", "plugin re-enable persisted");
    CallErr(bridge, 175, "settings.setPluginEnabled", "unknown_plugin",
        json{{"filename", "not-discovered.dll"}, {"enabled", false}});
    Check(IniValue(host.config, L"plugins", L"not-discovered.dll").empty(), "unknown plugin is not persisted");
    const auto caseInsensitive = CallOk(bridge, 176, "settings.setPluginEnabled",
        json{{"filename", "TEST.PLUGIN.DLL"}, {"enabled", false}});
    Check(caseInsensitive.value("filename", "") == "TEST.PLUGIN.DLL", "filename echo preserves caller casing");
    Check(IniValue(host.config, L"plugins", L"test.plugin.dll") == L"false", "plugin filename match is case-insensitive");
    Check(!PluginEntry(log.Last("settings.changed"), "test.plugin.dll").value("enabled", true),
        "case-insensitive disable is reflected in settings.changed");
    CallOk(bridge, 177, "settings.setPluginEnabled", json{{"filename", "test.plugin.dll"}, {"enabled", true}});
    host.snapshotAvailable = false;
    CallErr(bridge, 178, "settings.setPluginEnabled", "unavailable",
        json{{"filename", "test.plugin.dll"}, {"enabled", false}});
    Check(CallOk(bridge, 179, "settings.get")["plugins"].empty(), "unavailable snapshot lists no plugin settings");
    host.snapshotAvailable = true;
    std::puts("ok: settings commands (console/trace/plugin persistence + saveResult/changed events)");
}

void TestShellAndCapture(Backend& backend, StubHost& host, EventLog& log) {
    auto& bridge = backend.bridge();
    log.Clear();
    const auto selected = CallOk(bridge, 180, "shell.selectPanel", json{{"id", "other.plugin"}});
    Check(selected.value("selectedPanel", "") == "other.plugin", "selectPanel echoes the id");
    Check(host.selections.size() == 1 && host.selections[0] == "other.plugin", "selectPanel hook invoked");
    Check(log.Count("shell.panelSelected") == 1, "shell.panelSelected emitted");
    Check(log.Last("shell.panelSelected").value("id", "") == "other.plugin", "panelSelected payload");
    CallOk(bridge, 181, "shell.selectPanel", json{{"id", "gakuloader.manager"}});
    Check(host.selected == "gakuloader.manager", "manager reselection");
    const auto selectionsBefore = host.selections.size();
    CallErr(bridge, 182, "shell.selectPanel", "unknown_panel", json{{"id", "ghost.panel"}});
    Check(host.selections.size() == selectionsBefore && host.selected == "gakuloader.manager",
        "unknown panel does not invoke the selection hook");
    Check(log.Count("shell.panelSelected") == 2, "unknown panel emits no selection event");
    host.snapshotAvailable = false;
    CallErr(bridge, 183, "shell.selectPanel", "unknown_panel", json{{"id", "test.plugin"}});
    CallOk(bridge, 184, "shell.selectPanel", json{{"id", "gakuloader.manager"}});
    host.snapshotAvailable = true;

    CallOk(bridge, 185, "shell.setTextInputActive", json{{"active", true}});
    CallOk(bridge, 186, "shell.setTextInputActive", json{{"active", false}});
    Check(host.textInput.size() == 2 && host.textInput[0] && !host.textInput[1], "textInput hook saw both edges");
    CallOk(bridge, 187, "shell.hide");
    Check(host.hides == 1, "shell.hide hook invoked");

    CallOk(bridge, 188, "hotkeys.setOverride",
        json{{"pluginId", "test.plugin"}, {"actionId", "extra"}, {"combo", "Ctrl+U"}});
    Check(PollKey('U', GAKU_HOTKEY_MOD_CTRL).actions.size() == 1, "bound hotkey dispatches when idle");
    Check(PollKey(VK_F1).managerToggle, "F1 toggles when idle");

    log.Clear();
    CallErr(bridge, 189, "hotkeys.beginCapture", "unknown_hotkey",
        json{{"pluginId", "ghost.plugin"}, {"actionId", "panel"}});
    CallErr(bridge, 190, "hotkeys.beginCapture", "unknown_hotkey",
        json{{"pluginId", "test.plugin"}, {"actionId", "ghost"}});
    CallErr(bridge, 191, "hotkeys.beginCapture", "invalid_request",
        json{{"pluginId", "test.plugin"}, {"actionId", "panel"}, {"combo", 42}});
    Check(log.items.empty(), "rejected capture begins emit nothing");

    const auto capture = CallOk(bridge, 192, "hotkeys.beginCapture",
        json{{"pluginId", "test.plugin"}, {"actionId", "panel"}, {"combo", ""}});
    Check(capture.value("capturing", false), "beginCapture reports capturing");
    Check(log.Count("hotkeys.captureStateChanged") == 1, "captureStateChanged emitted on begin");
    Check(log.Last("hotkeys.captureStateChanged").value("capturing", false) &&
        log.Last("hotkeys.captureStateChanged").value("pluginId", "") == "test.plugin" &&
        log.Last("hotkeys.captureStateChanged").value("actionId", "") == "panel", "capture begin payload");
    Check(PollKey('U', GAKU_HOTKEY_MOD_CTRL).actions.empty(), "capture suppresses plugin dispatch");
    Check(!PollKey(VK_F1).managerToggle, "capture suppresses the manager toggle");

    CallOk(bridge, 193, "hotkeys.endCapture");
    Check(log.Count("hotkeys.captureStateChanged") == 2, "captureStateChanged emitted on end");
    Check(!log.Last("hotkeys.captureStateChanged").value("capturing", true), "capture end payload");
    Check(log.Count("hotkeys.captureCancelled") == 1, "explicit end cancels");
    Check(PollKey('U', GAKU_HOTKEY_MOD_CTRL).actions.size() == 1, "dispatch resumes after capture");
    const auto eventsAfterEnd = log.items.size();
    CallOk(bridge, 194, "hotkeys.endCapture");
    Check(log.items.size() == eventsAfterEnd, "endCapture without capture is a no-op");
    log.CheckEnvelope(bridge.sessionId());

    Check(bridge.HasMethod("hotkeys.setEditorActive"), "editor suppression method registered");
    CallOk(bridge, 195, "hotkeys.setEditorActive", json{{"active", true}});
    Check(log.items.size() == eventsAfterEnd, "editor activation emits no capture event");
    CallOk(bridge, 196, "hotkeys.setEditorActive", json{{"active", false}});
    Check(log.items.size() == eventsAfterEnd, "editor deactivation without capture is a no-op");
    CallErr(bridge, 197, "hotkeys.setEditorActive", "internal_error", json::object());
    CallErr(bridge, 198, "hotkeys.setEditorActive", "internal_error", json{{"active", "true"}});

    log.Clear();
    CallOk(bridge, 199, "hotkeys.beginCapture",
        json{{"pluginId", "test.plugin"}, {"actionId", "panel"}});
    CallOk(bridge, 200, "hotkeys.setEditorActive", json{{"active", false}});
    Check(log.Count("hotkeys.captureStateChanged") == 2, "editor deactivation cancels active capture");
    Check(log.Count("hotkeys.captureCancelled") == 1, "editor deactivation emits captureCancelled");
    const auto eventsAfterEditorCancel = log.items.size();
    CallOk(bridge, 201, "hotkeys.endCapture");
    Check(log.items.size() == eventsAfterEditorCancel, "capture already ended by editor deactivation");
    std::puts("ok: shell commands, hotkey capture lifecycle, editor suppression");
}

void TestTickAndDocumentReady(Backend& backend, StubHost& host, EventLog& log) {
    auto& bridge = backend.bridge();
    log.Clear();
    backend.Tick();
    Check(log.Count("host.snapshotChanged") == 1, "first Tick pushes the snapshot");
    Check(log.Count("appearance.changed") == 1, "first Tick pushes appearance");
    Check(log.Count("shell.saveFailed") == 1, "first Tick pushes the save banner");
    const auto afterFirst = log.items.size();
    backend.Tick();
    Check(log.items.size() == afterFirst, "idle Tick emits nothing");

    host.snapshot.runtimeReady = false;
    backend.Tick();
    Check(log.Count("host.snapshotChanged") == 2, "snapshot change pushes once");
    Check(!log.Last("host.snapshotChanged").value("runtimeReady", true), "changed snapshot payload");
    host.appearance.theme = 3;
    backend.Tick();
    Check(log.Count("appearance.changed") == 2 && log.Last("appearance.changed").value("theme", 0) == 3,
        "appearance change pushes once");
    host.selected = "other.plugin";
    backend.Tick();
    Check(log.Count("shell.panelSelected") >= 1 && log.Last("shell.panelSelected").value("id", "") == "other.plugin",
        "selection change pushes");
    host.saveFailed = true;
    backend.Tick();
    Check(log.Last("shell.saveFailed").value("failed", false), "save banner change pushes");
    const auto afterChanges = log.items.size();
    backend.Tick();
    Check(log.items.size() == afterChanges, "Tick after changes settles");

    // Headless capture cancel: hostWindow hook returns nullptr, so Tick must
    // cancel a physical capture deterministically.
    log.Clear();
    CallOk(bridge, 220, "hotkeys.beginCapture", json{{"pluginId", "test.plugin"}, {"actionId", "panel"}});
    backend.Tick();
    Check(log.Count("hotkeys.captureStateChanged") == 2, "Tick cancels capture without a host window");
    Check(!log.Last("hotkeys.captureStateChanged").value("capturing", true), "cancelled capture state");
    Check(log.Count("hotkeys.captureCancelled") == 1, "Tick cancel emits captureCancelled");

    // OnDocumentReady: fresh session, forced state push, fixed event set.
    log.Clear();
    const auto sidBefore = bridge.sessionId();
    backend.OnDocumentReady();
    Check(bridge.sessionId() == sidBefore + 1, "OnDocumentReady bumps the session");
    log.CheckEnvelope(bridge.sessionId());
    Check(log.items.size() == 9, "OnDocumentReady emits the full state set");
    Check(log.items[0].value("name", "") == "shell.ready", "shell.ready leads the ready set");
    Check(log.items[0]["payload"].value("mode", "") == "webview2", "shell.ready mode");
    for (const char* name : {"host.snapshotChanged", "appearance.changed", "shell.panelSelected",
        "shell.saveFailed", "language.changed", "strings.changed", "hotkeys.rowsChanged", "settings.changed"})
        Check(log.Has(name), "ready set contains the expected event");
    const auto hello = CallOk(bridge, 210, "bridge.hello");
    Check(hello.value("sessionId", 0ull) == bridge.sessionId(), "hello reports the reopened session");
    std::puts("ok: Tick change-watch, headless capture cancel, OnDocumentReady state push");
}

void TestBackendLifecycle(Backend& backend, EventLog& log) {
    auto& bridge = backend.bridge();
    CallOk(bridge, 300, "hotkeys.beginCapture", json{{"pluginId", "test.plugin"}, {"actionId", "panel"}});
    log.Clear();
    backend.Shutdown();
    Check(bridge.closed(), "Shutdown closes the bridge");
    Check(log.items.size() == 3, "shutdown cancels capture then closes");
    Check(log.items[0].value("name", "") == "hotkeys.captureStateChanged" &&
        !log.items[0]["payload"].value("capturing", true), "capture cancelled first");
    Check(log.items[1].value("name", "") == "hotkeys.captureCancelled", "captureCancelled second");
    Check(log.items[2].value("name", "") == "bridge.closed" &&
        log.items[2]["payload"].value("reason", "") == "host_shutdown", "bridge.closed last with reason");
    CallErr(bridge, 301, "host.snapshot", "shutting_down");
    CallErr(bridge, 302, "bridge.ping", "shutting_down");
    CallErr(bridge, 303, "settings.setConsole", "shutting_down", json{{"enabled", true}});
    backend.Shutdown();
    Check(log.items.size() == 3, "second Shutdown is a no-op");
    std::puts("ok: backend lifecycle (capture cancel, close event, shutting_down rejection)");
}

// ---------------------------------------------------------------------------
// Entry
// ---------------------------------------------------------------------------
void TestForwardRequest() {
    using GakuLoader::ManagerBridge::ForwardRequest;
    const std::string request = "{\"v\":1,\"id\":7,\"type\":\"request\",\"method\":\"motion.ping\",\"params\":{}}";
    bool sawRaw = false;
    const auto handler = [&](const std::string& raw) {
        sawRaw = raw == request;
        return std::string("{\"v\":1,\"id\":7,\"type\":\"response\",\"ok\":true,\"result\":{\"pong\":true}}");
    };
    const auto response = json::parse(ForwardRequest(request, handler));
    Check(sawRaw, "ForwardRequest passes the original request JSON to the provider");
    Check(response.value("ok", false) && response["result"].value("pong", false), "ForwardRequest passes provider response through");
    Check(json::parse(ForwardRequest("not-json", handler))["error"].value("code", "") == "invalid_json", "ForwardRequest rejects malformed JSON");
    Check(json::parse(ForwardRequest("{\"v\":1,\"id\":7,\"type\":\"event\"}", handler))["error"].value("code", "") == "invalid_request", "ForwardRequest rejects non-request");
    Check(json::parse(ForwardRequest("{\"v\":1,\"type\":\"request\",\"method\":\"x\"}", handler))["error"].value("code", "") == "invalid_request", "ForwardRequest requires id");
    Check(json::parse(ForwardRequest("{\"v\":2,\"id\":7,\"type\":\"request\",\"method\":\"x\"}", handler))["error"].value("code", "") == "unsupported_protocol", "ForwardRequest rejects protocol version");
    Check(json::parse(ForwardRequest("{\"v\":1,\"id\":7,\"type\":\"request\"}", handler))["error"].value("code", "") == "invalid_request", "ForwardRequest requires method");
    Check(json::parse(ForwardRequest("{\"v\":1,\"id\":7,\"type\":\"request\",\"method\":\"x\",\"params\":[]}", handler))["error"].value("code", "") == "invalid_request", "ForwardRequest requires object params");
    Check(json::parse(ForwardRequest(request, nullptr))["error"].value("code", "") == "internal_error", "ForwardRequest requires a handler");
    Check(json::parse(ForwardRequest(request, [](const std::string&) { return std::string(); }))["error"].value("code", "") == "internal_error", "ForwardRequest rejects empty provider response");
    Check(json::parse(ForwardRequest(request, [](const std::string&) { return std::string("{\"v\":1,\"id\":8,\"type\":\"response\",\"ok\":true}"); }))["error"].value("code", "") == "internal_error", "ForwardRequest rejects mismatched response id");
    Check(json::parse(ForwardRequest(request, [](const std::string&) -> std::string { throw std::runtime_error("boom"); }))["error"].value("code", "") == "internal_error", "ForwardRequest contains provider exceptions");
    std::puts("ok: web panel raw request forwarding contract");
}

struct TempRoot {
    fs::path path;
    ~TempRoot() { std::error_code ignored; fs::remove_all(path, ignored); }
};

int RunAll(const fs::path& root) {
    const auto config = root / L"gakuloader.ini";
    const auto missing = root / L"missing.ini";
    GakuLoader::LanguageSettings::Load(missing); // deterministic system fallback start

    TestBridgeCore();
    TestForwardRequest();

    StubHost host;
    host.config = config;
    SeedSnapshot(host);

    // Real Hotkeys registry: two owners sharing one default combo (conflict),
    // plus a second action used for unbind/reset coverage.
    static int ownerA = 1, ownerB = 2;
    const auto panelBinding = MakeBinding("panel", "Show Panel", 'P', GAKU_HOTKEY_MOD_CTRL | GAKU_HOTKEY_MOD_ALT);
    const auto extraBinding = MakeBinding("extra", "Extra Action", 'U', GAKU_HOTKEY_MOD_CTRL);
    const auto otherBinding = MakeBinding("panel", "Other Panel", 'P', GAKU_HOTKEY_MOD_CTRL | GAKU_HOTKEY_MOD_ALT);
    Check(GakuLoader::Hotkeys::Register(&ownerA, "test.plugin", &panelBinding) == GAKU_OK, "register test.plugin.panel");
    Check(GakuLoader::Hotkeys::Register(&ownerA, "test.plugin", &extraBinding) == GAKU_OK, "register test.plugin.extra");
    Check(GakuLoader::Hotkeys::Register(&ownerB, "other.plugin", &otherBinding) == GAKU_OK, "register other.plugin.panel");

    Backend backend(MakeContext(host));
    EventLog log;
    log.Attach(backend.bridge());

    TestBackendQueries(backend, host);
    TestAppearanceCommands(backend, host, log);
    TestLanguageCommands(backend, host, log);
    TestHotkeyCommands(backend, host, log);
    TestSettingsCommands(backend, host, log);
    TestShellAndCapture(backend, host, log);
    TestTickAndDocumentReady(backend, host, log);
    TestBackendLifecycle(backend, log);

    GakuLoader::Hotkeys::RetireOwner(&ownerA);
    GakuLoader::Hotkeys::RetireOwner(&ownerB);
    std::printf("PASS: %d checks - manager bridge core + backend contract (headless, temporary INI, stubbed host)\n", g_checks);
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    fs::path root;
    if (argc >= 2) {
        root = argv[1];
    } else {
        wchar_t base[MAX_PATH + 2]{};
        const DWORD length = GetTempPathW(static_cast<DWORD>(std::size(base)), base);
        if (length == 0 || length > MAX_PATH) { std::fputs("GetTempPathW failed\n", stderr); return 2; }
        char unique[96];
        std::snprintf(unique, sizeof(unique), "gaku-bridge-tests-%lu-%llu",
            GetCurrentProcessId(), static_cast<unsigned long long>(GetTickCount64()));
        root = fs::path(base) / unique;
    }
    std::error_code error;
    fs::create_directories(root, error);
    if (error) { std::fputs("cannot create temporary test root\n", stderr); return 2; }
    TempRoot cleanup{root};
    try {
        return RunAll(root);
    } catch (const std::exception& exception) {
        std::fprintf(stderr, "FAIL: %s\n", exception.what());
        return 1;
    }
}
