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
    RECT lastBounds{};
    float lastDpi = 0.0f;
    std::string lastPanel;
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
    configA.assetsDir = L"synthetic-assets-a";
    configA.virtualHost = L"panel-a.invalid";
    configA.entryPath = L"index.html";
    configA.dpiScale = 1.0f;

    WebView2Ui::PanelConfig configB;
    configB.assetsDir = L"synthetic-assets-b";
    configB.virtualHost = L"panel-b.invalid";
    configB.entryPath = L"index.html";
    configB.dpiScale = 1.0f;

    const HWND parent = reinterpret_cast<HWND>(uintptr_t{1});
    Check(WebView2Ui::InitializePanel("plugin.a", parent, envConfig, configA, callbacksA), "synthetic initialize A");
    Check(WebView2Ui::InitializePanel("plugin.b", parent, envConfig, configB, callbacksB), "synthetic initialize B");
    Check(envCalls == 1, "shared environment created once for two panels");
    Check(initializeCalls == 2, "both panels initialized");
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
    WebView2Ui::Shutdown();

    hooks.failDocument = false;
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
