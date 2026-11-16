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
    int initializeCalls = 0, visibleCalls = 0, boundsCalls = 0, dpiCalls = 0;
    int focusCalls = 0, postCalls = 0, shutdownCalls = 0;
    RECT lastBounds{};
    float lastDpi = 0.0f;
    bool lastVisible = false;
    hooks.initialize = [&](const WebView2Ui::Config& config) {
        ++initializeCalls;
        Check(config.virtualHost == L"panel.invalid", "initialize virtual host");
        Check(config.entryPath == L"index.html", "initialize entry path");
    };
    hooks.visible = [&](bool visible) { ++visibleCalls; lastVisible = visible; };
    hooks.bounds = [&](const RECT& bounds) { ++boundsCalls; lastBounds = bounds; };
    hooks.dpi = [&](float scale) { ++dpiCalls; lastDpi = scale; };
    hooks.focus = [&] { ++focusCalls; };
    hooks.postMessage = [&](const std::string& json) { ++postCalls; Check(json == "{}", "post message payload"); };
    hooks.shutdown = [&] { ++shutdownCalls; };
    WebView2Ui::ConfigureTest(hooks);

    bool ready = false, document = false, focused = false, textInput = false, tabLeft = false;
    std::string failure;
    WebView2Ui::Callbacks callbacks;
    callbacks.onReady = [&](bool ok, const std::string& diagnostic) { ready = ok; if (!ok) failure = diagnostic; };
    callbacks.onDocumentReady = [&] { document = true; };
    callbacks.onFocusChanged = [&](bool value) { focused = value; };
    callbacks.onTextInputChanged = [&](bool value) { textInput = value; };
    callbacks.onTabLeave = [&](bool) { tabLeft = true; };

    WebView2Ui::Config config;
    config.assetsDir = L"synthetic-assets";
    config.userDataDir = L"synthetic-user-data";
    config.virtualHost = L"panel.invalid";
    config.entryPath = L"index.html";
    config.dpiScale = 1.0f;
    const HWND parent = reinterpret_cast<HWND>(uintptr_t{1});
    Check(WebView2Ui::Initialize(parent, config, callbacks), "synthetic initialize");
    Check(ready && document && WebView2Ui::IsReady() && WebView2Ui::IsDocumentReady(), "synthetic ready");
    Check(initializeCalls == 1, "initialize hook called");

    WebView2Ui::SetBounds(RECT{10, 20, 300, 400});
    Check(boundsCalls == 1 && lastBounds.left == 10 && lastBounds.right == 300, "bounds hook");
    WebView2Ui::SetVisible(true);
    Check(visibleCalls == 1 && lastVisible && WebView2Ui::IsVisible(), "visible hook");
    WebView2Ui::SetDpiScale(1.5f);
    Check(dpiCalls == 1 && lastDpi == 1.5f, "dpi hook");
    WebView2Ui::Focus();
    Check(focusCalls == 1, "focus hook");
    WebView2Ui::SimulateFocusForTest(true);
    Check(focused && textInput, "focus callback");
    WebView2Ui::SimulateTabLeaveForTest(true);
    Check(tabLeft && !focused && !textInput, "tab leave callback");
    WebView2Ui::PostMessageJson("{}");
    Check(postCalls == 1, "post message hook");

    WebView2Ui::SimulateProcessFailedForTest("renderer gone");
    Check(!ready && !WebView2Ui::IsReady() && !WebView2Ui::IsVisible(), "process failure fallback");
    Check(failure == "renderer gone", "process failure diagnostic");
    WebView2Ui::Shutdown();
    Check(shutdownCalls >= 1 && !WebView2Ui::IsReady(), "shutdown hook");

    hooks.failDocument = true;
    WebView2Ui::ConfigureTest(hooks);
    Check(WebView2Ui::Initialize(parent, config, callbacks), "initialize with failed document");
    Check(WebView2Ui::IsReady() && !WebView2Ui::IsDocumentReady(), "document failure state");
    WebView2Ui::SetVisible(true);
    Check(!WebView2Ui::IsVisible(), "hidden until document ready");
    WebView2Ui::Shutdown();

    hooks.failDocument = false;
    hooks.failInitialize = true;
    WebView2Ui::ConfigureTest(hooks);
    Check(!WebView2Ui::Initialize(parent, config, callbacks), "initialize failure");
    Check(!WebView2Ui::LastDiagnostic().empty(), "initialize failure diagnostic");
    WebView2Ui::Shutdown();

    std::printf("PASS: %d checks - web panel host synthetic lifecycle/bounds/focus/DPI/failure seams\n", g_checks);
    return 0;
}
