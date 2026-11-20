#include "WebView2Ui.hpp"
#include <cassert>
#include <cstdio>
#include <string>

namespace {
int g_checks = 0;
void Check(bool condition, const char* message) {
    ++g_checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
}

int main() {
    using namespace GakuLoader;
    WebView2Ui::TestHooks hooks;
    int envCalls = 0, initializeCalls = 0, visibleCalls = 0, boundsCalls = 0, dpiCalls = 0;
    int focusCalls = 0, postCalls = 0, destroyPanelCalls = 0, shutdownCalls = 0;
    int docScriptCalls = 0, execScriptCalls = 0;
    RECT lastBounds{};
    float lastDpi = 0.0f;
    std::string lastPanel, lastDocScript, lastExecScript;
    bool lastVisible = false;
    hooks.ensureEnvironment = [&](const WebView2Ui::EnvironmentConfig&) { ++envCalls; };
    hooks.initializePanel = [&](const std::string& panelId, const WebView2Ui::PanelConfig& config) {
        ++initializeCalls;
        lastPanel = panelId;
        Check(config.entryPath == L"index.html", "initialize entry path");
    };
    hooks.visible = [&](const std::string& panelId, bool visible) {
        ++visibleCalls;
        lastPanel = panelId;
        lastVisible = visible;
    };
    hooks.bounds = [&](const std::string& panelId, const RECT& bounds) {
        ++boundsCalls;
        lastPanel = panelId;
        lastBounds = bounds;
    };
    hooks.dpi = [&](float scale) { ++dpiCalls; lastDpi = scale; };
    hooks.focus = [&](const std::string& panelId) { ++focusCalls; lastPanel = panelId; };
    hooks.postMessage = [&](const std::string& panelId, const std::string& json) {
        ++postCalls;
        lastPanel = panelId;
        Check(json == "{}", "post message payload");
    };
    hooks.documentScript = [&](const std::string& panelId, const std::string& script) {
        ++docScriptCalls;
        lastPanel = panelId;
        lastDocScript = script;
    };
    hooks.executeScript = [&](const std::string& panelId, const std::string& script) {
        ++execScriptCalls;
        lastPanel = panelId;
        lastExecScript = script;
    };
    hooks.destroyPanel = [&](const std::string& panelId) { ++destroyPanelCalls; lastPanel = panelId; };
    hooks.shutdown = [&] { ++shutdownCalls; };
    WebView2Ui::ConfigureTest(hooks);

    bool readyA = false, documentA = false, focusedA = false, textInputA = false, tabLeftA = false;
    bool readyB = false, documentB = false;
    std::string failureA, failureB;
    WebView2Ui::Callbacks callbacksA;
    callbacksA.onReady = [&](bool ok, const std::string& diagnostic) { readyA = ok; if (!ok) failureA = diagnostic; };
    callbacksA.onDocumentReady = [&] { documentA = true; };
    callbacksA.onFocusChanged = [&](bool value) { focusedA = value; };
    callbacksA.onTextInputChanged = [&](bool value) { textInputA = value; };
    callbacksA.onTabLeave = [&](bool) { tabLeftA = true; };

    WebView2Ui::Callbacks callbacksB;
    callbacksB.onReady = [&](bool ok, const std::string& diagnostic) { readyB = ok; if (!ok) failureB = diagnostic; };
    callbacksB.onDocumentReady = [&] { documentB = true; };

    WebView2Ui::EnvironmentConfig envConfig;
    envConfig.userDataDir = L"synthetic-shared-user-data";

    WebView2Ui::PanelConfig configA;
    configA.assetsDir = L"C:\\synthetic-assets-a";
    configA.virtualHost = L"panel-a.invalid";
    configA.entryPath = L"index.html";
    configA.dpiScale = 1.0f;

    WebView2Ui::PanelConfig configB;
    configB.assetsDir = L"C:\\synthetic-assets-b";
    configB.virtualHost = L"panel-b.invalid";
    configB.entryPath = L"index.html";
    configB.dpiScale = 1.0f;

    const HWND parent = reinterpret_cast<HWND>(uintptr_t{1});
    Check(WebView2Ui::InitializePanel("plugin.a", parent, envConfig, configA, callbacksA), "synthetic initialize A");
    Check(WebView2Ui::InitializePanel("plugin.b", parent, envConfig, configB, callbacksB), "synthetic initialize B");
    Check(envCalls == 1, "shared environment created once for two panels");
    Check(initializeCalls == 2, "both panels initialized");
    Check(docScriptCalls == 2, "typography guidance injected on document created for both panels");
    Check(readyA && documentA && WebView2Ui::IsReady("plugin.a") && WebView2Ui::IsDocumentReady("plugin.a"), "panel A ready");
    Check(readyB && documentB && WebView2Ui::IsReady("plugin.b") && WebView2Ui::IsDocumentReady("plugin.b"), "panel B ready");

    WebView2Ui::SetBounds("plugin.a", RECT{10, 20, 300, 400});
    Check(boundsCalls == 1 && lastPanel == "plugin.a" && lastBounds.left == 10 && lastBounds.right == 300, "bounds hook");
    WebView2Ui::SetVisible("plugin.a", true);
    Check(visibleCalls == 1 && lastVisible && WebView2Ui::IsVisible("plugin.a") && !WebView2Ui::IsVisible("plugin.b"), "visible hook isolated");
    WebView2Ui::SetDpiScale(1.5f);
    Check(dpiCalls == 1 && lastDpi == 1.5f, "dpi hook");
    WebView2Ui::Focus("plugin.a");
    Check(focusCalls == 1 && lastPanel == "plugin.a", "focus hook");
    WebView2Ui::SimulateFocusForTest("plugin.a", true);
    Check(focusedA && textInputA, "focus callback");
    WebView2Ui::SimulateTabLeaveForTest("plugin.a", true);
    Check(tabLeftA && !focusedA && !textInputA, "tab leave callback");
    WebView2Ui::PostMessageJson("plugin.b", "{}");
    Check(postCalls == 1 && lastPanel == "plugin.b", "post message hook isolated to B");

    const std::string guidanceScript = WebView2Ui::BuildTypographyGuidanceScript({1, 1.25f, 1.5f, 3});
    for (const char* token : {
        "--gaku-font-sans", "Microsoft YaHei UI",
        "--gaku-font-mono", "Cascadia Mono",
        "--gaku-ui-scale", "'--gaku-ui-scale':'1.25'",
        "--gaku-text-micro", "11px",
        "--gaku-text-xs", "12px",
        "--gaku-text-sm", "13px",
        "--gaku-text-base", "14px",
        "--gaku-text-md", "15px",
        "--gaku-text-lg", "18px",
        "--gaku-text-title", "22px",
        "--gaku-leading-tight", "1.35",
        "--gaku-leading-base", "1.5",
        "--gaku-control-height", "34px",
        "--gaku-control-height-sm", "28px",
        "--gaku-row-height", "56px",
        "--gaku-radius-panel", "0px",
        "window.__GAKU_UI_GUIDANCE__", "version:1", "uiScale:1.25", "dpiScale:1.5", "theme:3", "apple-light",
        "typography:", "metrics:", "getGakuUiGuidance", "gaku:ui-guidance-changed"
    }) {
        Check(guidanceScript.find(token) != std::string::npos, token);
    }
    const int docBeforeGuidance = docScriptCalls;
    const int execBeforeGuidance = execScriptCalls;
    WebView2Ui::SetUiGuidance({1, 1.25f, 1.5f, 3});
    Check(docScriptCalls == docBeforeGuidance + 2, "SetUiGuidance updates document-created scripts for both panels");
    Check(execScriptCalls == execBeforeGuidance + 2, "SetUiGuidance executes live update on both ready panels");
    Check(lastExecScript.find("'--gaku-ui-scale':'1.25'") != std::string::npos, "live guidance carries uiScale");
    Check(WebView2Ui::GetUiGuidance().theme == 3 && WebView2Ui::GetUiGuidance().uiScale == 1.25f, "GetUiGuidance reflects latest guidance");

    // Single-panel renderer failure must not break sibling panels or the shared environment.
    WebView2Ui::SimulateProcessFailedForTest("plugin.a", "renderer gone", false);
    Check(!readyA && !WebView2Ui::IsReady("plugin.a") && !WebView2Ui::IsVisible("plugin.a"), "panel A process failure fallback");
    Check(failureA == "renderer gone", "panel A failure diagnostic");
    Check(readyB && WebView2Ui::IsReady("plugin.b") && WebView2Ui::IsEnvironmentReady(), "panel B and shared env unaffected");

    WebView2Ui::DestroyPanel("plugin.a");
    Check(destroyPanelCalls == 1 && !WebView2Ui::HasPanel("plugin.a") && WebView2Ui::HasPanel("plugin.b"), "destroy single panel keeps B");
    Check(WebView2Ui::IsEnvironmentReady(), "destroy single panel keeps shared environment");

    WebView2Ui::Shutdown();
    Check(shutdownCalls >= 1 && !WebView2Ui::HasPanel("plugin.b") && !WebView2Ui::IsEnvironmentReady(), "shutdown releases all");

    hooks.failDocument = true;
    WebView2Ui::ConfigureTest(hooks);
    Check(WebView2Ui::InitializePanel("plugin.a", parent, envConfig, configA, callbacksA), "initialize with failed document");
    Check(WebView2Ui::IsReady("plugin.a") && !WebView2Ui::IsDocumentReady("plugin.a"), "document failure state");
    WebView2Ui::SetVisible("plugin.a", true);
    Check(!WebView2Ui::IsVisible("plugin.a"), "hidden until document ready");
    const int execBeforeUndoc = execScriptCalls;
    WebView2Ui::SetUiGuidance({1, 1.1f, 1.0f, 4});
    Check(execScriptCalls == execBeforeUndoc, "SetUiGuidance skips ExecuteScript before document ready");
    WebView2Ui::Shutdown();

    hooks.failDocument = false;
    WebView2Ui::ConfigureTest(hooks);

    WebView2Ui::PanelConfig badConfig = configA;
    badConfig.assetsDir.clear();
    Check(!WebView2Ui::InitializePanel("plugin.bad.dir", parent, envConfig, badConfig, callbacksA),
        "reject empty assetsDir");
    Check(!WebView2Ui::LastDiagnostic("plugin.bad.dir").empty(), "empty assetsDir diagnostic");

    badConfig = configA;
    badConfig.virtualHost = L"bad/host";
    Check(!WebView2Ui::InitializePanel("plugin.bad.host.slash", parent, envConfig, badConfig, callbacksA),
        "reject virtualHost with slash");

    badConfig = configA;
    badConfig.virtualHost = L"bad:host";
    Check(!WebView2Ui::InitializePanel("plugin.bad.host.port", parent, envConfig, badConfig, callbacksA),
        "reject virtualHost with colon");

    badConfig = configA;
    badConfig.entryPath = L"C:\\index.html";
    Check(!WebView2Ui::InitializePanel("plugin.bad.entry.abs", parent, envConfig, badConfig, callbacksA),
        "reject absolute entryPath");

    badConfig = configA;
    badConfig.entryPath = L"..\\escape.html";
    Check(!WebView2Ui::InitializePanel("plugin.bad.entry.rel", parent, envConfig, badConfig, callbacksA),
        "reject parent traversal entryPath");

    badConfig = configA;
    badConfig.entryPath = L"C:index.html";
    Check(!WebView2Ui::InitializePanel("plugin.bad.entry.drive", parent, envConfig, badConfig, callbacksA),
        "reject drive-relative entryPath");

    badConfig = configA;
    badConfig.entryPath = L"index.html?preview=1";
    Check(!WebView2Ui::InitializePanel("plugin.bad.entry.query", parent, envConfig, badConfig, callbacksA),
        "reject query string in entryPath");

    badConfig = configA;
    badConfig.virtualHost = L".bad.invalid";
    Check(!WebView2Ui::InitializePanel("plugin.bad.host.dot", parent, envConfig, badConfig, callbacksA),
        "reject leading dot virtualHost");

    badConfig = configA;
    badConfig.virtualHost = L"bad.invalid.";
    Check(!WebView2Ui::InitializePanel("plugin.bad.host.trail", parent, envConfig, badConfig, callbacksA),
        "reject trailing dot virtualHost");

    badConfig = configA;
    badConfig.virtualHost = L"bad..host.invalid";
    Check(!WebView2Ui::InitializePanel("plugin.bad.host.dotdot", parent, envConfig, badConfig, callbacksA),
        "reject consecutive dots in virtualHost");

    badConfig = configA;
    badConfig.virtualHost = L"bad%host.invalid";
    Check(!WebView2Ui::InitializePanel("plugin.bad.host.percent", parent, envConfig, badConfig, callbacksA),
        "reject percent in virtualHost");

    badConfig = configA;
    badConfig.entryPath = L".";
    Check(!WebView2Ui::InitializePanel("plugin.bad.entry.dot", parent, envConfig, badConfig, callbacksA),
        "reject current-dir entryPath");

    badConfig = configA;
    badConfig.entryPath = L"sub\\.";
    Check(!WebView2Ui::InitializePanel("plugin.bad.entry.subdot", parent, envConfig, badConfig, callbacksA),
        "reject trailing dot segment in entryPath");

    badConfig = configA;
    badConfig.assetsDir = L"relative-assets";
    Check(!WebView2Ui::InitializePanel("plugin.bad.dir.rel", parent, envConfig, badConfig, callbacksA),
        "reject relative assetsDir");
    Check(!WebView2Ui::LastDiagnostic("plugin.bad.dir.rel").empty(), "relative assetsDir diagnostic");

    badConfig = configA;
    badConfig.assetsDir = L"C:drive-relative";
    Check(!WebView2Ui::InitializePanel("plugin.bad.dir.drive", parent, envConfig, badConfig, callbacksA),
        "reject drive-relative assetsDir");

    badConfig = configA;
    badConfig.assetsDir = L"\\root-relative";
    Check(!WebView2Ui::InitializePanel("plugin.bad.dir.root", parent, envConfig, badConfig, callbacksA),
        "reject root-relative assetsDir");

    badConfig = configA;
    badConfig.assetsDir = L"C:\\synthetic\\..\\escape";
    Check(!WebView2Ui::InitializePanel("plugin.bad.dir.dotdot", parent, envConfig, badConfig, callbacksA),
        "reject parent traversal segment in assetsDir");
    WebView2Ui::Shutdown();

    hooks.failInitialize = true;
    WebView2Ui::ConfigureTest(hooks);
    Check(!WebView2Ui::InitializePanel("plugin.a", parent, envConfig, configA, callbacksA), "initialize failure");
    Check(!WebView2Ui::LastDiagnostic("plugin.a").empty(), "initialize failure diagnostic");
    WebView2Ui::Shutdown();

    hooks.failInitialize = false;
    hooks.failEnvironment = true;
    WebView2Ui::ConfigureTest(hooks);
    Check(!WebView2Ui::EnsureEnvironment(envConfig), "environment failure");
    Check(WebView2Ui::HasEnvironmentFailed() && !WebView2Ui::LastEnvironmentDiagnostic().empty(), "environment diagnostic");
    WebView2Ui::Shutdown();

    std::printf("PASS: %d checks - shared WebView2 environment and multi-panel lifecycle/isolation seams\n", g_checks);
    return 0;
}
