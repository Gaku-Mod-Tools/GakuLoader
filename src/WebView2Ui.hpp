// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

#pragma once
// WebView2 host for a plugin content panel. Owns the environment/controller/webview
// COM lifecycle on the desktop UI thread (the same STA that pumps the control
// window). The app-local WebView2Loader.dll is resolved dynamically so the proxy
// version.dll stays loadable when it is absent, and both Debug/Release CRTs work.
//
// Assets are served from a provider-owned virtual host mapped to a local folder;
// navigation and web messages are restricted to that origin. There is no remote
// content and no arbitrary native command surface.

#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace GakuLoader::WebView2Ui {

struct Config {
    std::filesystem::path assetsDir;
    std::filesystem::path userDataDir;
    std::wstring entryPath = L"index.html";
    std::wstring virtualHost;
    std::wstring languageTag;
    bool devToolsEnabled = false;
    float dpiScale = 1.0f;
};

struct Callbacks {
    // A trusted web message arrived (JSON text). UI thread.
    std::function<void(const std::string&)> onMessage;
    // Asynchronous initialization finished. On failure, diagnostic is actionable.
    std::function<void(bool ok, const std::string& diagnostic)> onReady;
    // The entry document finished loading; start a fresh bridge session. UI thread.
    std::function<void()> onDocumentReady;
    std::function<void(bool focused)> onFocusChanged;
    std::function<void(bool active)> onTextInputChanged;
    std::function<void(bool forward)> onTabLeave;
};

// Kick off asynchronous creation. Returns false (with LastDiagnostic) when the
// runtime, loader, or assets are missing; otherwise returns true and reports the
// final result through Callbacks::onReady. Must be called on the UI thread.
bool Initialize(HWND parent, const Config& config, Callbacks callbacks);

void SetVisible(bool visible);
void SetBounds(const RECT& clientBounds);
void SetDpiScale(float scale);
void NotifyParentMoved();
void Focus();
void PostMessageJson(const std::string& json);
// Release controller/environment on the UI thread. Safe to call repeatedly.
void Shutdown();

bool IsReady();
bool IsDocumentReady();
bool IsVisible();
std::string LastDiagnostic();

#if defined(GAKULOADER_WEBVIEW2_TEST)
struct TestHooks {
    bool failInitialize = false;
    bool failDocument = false;
    std::function<void(const Config&)> initialize;
    std::function<void(bool)> visible;
    std::function<void(const RECT&)> bounds;
    std::function<void(float)> dpi;
    std::function<void()> focus;
    std::function<void(const std::string&)> postMessage;
    std::function<void()> shutdown;
};
void ConfigureTest(const TestHooks& hooks);
void SimulateProcessFailedForTest(const std::string& diagnostic);
void SimulateFocusForTest(bool focused);
void SimulateTabLeaveForTest(bool forward);
#endif

} // namespace GakuLoader::WebView2Ui
