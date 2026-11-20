// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

#pragma once
// WebView2 host for plugin content panels. Owns a single shared
// CoreWebView2Environment and per-plugin CoreWebView2Controller/WebView
// instances on the desktop UI thread (the same STA that pumps the control
// window). The app-local WebView2Loader.dll is resolved dynamically so the
// proxy version.dll stays loadable when it is absent, and both Debug/Release
// CRTs work.
//
// Each plugin panel serves assets from its own provider-owned virtual host
// mapped to a local folder; navigation and web messages are restricted to that
// plugin's origin, keeping DOM/storage, native bridge permissions, and
// lifecycles isolated across plugins while sharing the browser environment.

#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace GakuLoader::WebView2Ui {

struct EnvironmentConfig {
    std::filesystem::path userDataDir;
    std::wstring languageTag;
    bool devToolsEnabled = false;
};

struct PanelConfig {
    std::filesystem::path assetsDir;
    std::wstring entryPath = L"index.html";
    std::wstring virtualHost;
    float dpiScale = 1.0f;
    float uiScale = 1.0f;
    uint32_t theme = 0;
};

struct UiGuidance {
    uint32_t version = 1;
    float uiScale = 1.0f;
    float dpiScale = 1.0f;
    uint32_t theme = 0;
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

// Warm up or ensure the shared CoreWebView2Environment on the UI thread.
// Returns false (with LastEnvironmentDiagnostic) if the runtime, loader, or
// userDataDir is unavailable; otherwise returns true and starts async creation
// if not already started. Safe to call repeatedly.
bool EnsureEnvironment(const EnvironmentConfig& config);

bool IsValidPanelConfig(const PanelConfig& panelConfig);

// Create and initialize a per-plugin WebView2 panel on the UI thread, sharing
// the single CoreWebView2Environment. Replaces any prior panel with the same
// panelId. Returns false (with LastDiagnostic(panelId)) on immediate failure;
// otherwise returns true and reports the final result via Callbacks::onReady.
bool InitializePanel(const std::string& panelId, HWND parent,
    const EnvironmentConfig& envConfig, const PanelConfig& panelConfig, Callbacks callbacks);

void SetVisible(const std::string& panelId, bool visible);
void SetBounds(const std::string& panelId, const RECT& clientBounds);
void SetDpiScale(float scale);
std::string BuildTypographyGuidanceScript(const UiGuidance& guidance);
void SetUiGuidance(const UiGuidance& guidance);
UiGuidance GetUiGuidance();
void NotifyParentMoved();
void Focus(const std::string& panelId);
void PostMessageJson(const std::string& panelId, const std::string& json);

// Destroy a single plugin panel (controller + webview) while keeping the shared
// CoreWebView2Environment alive for other panels. Safe to call repeatedly.
void DestroyPanel(const std::string& panelId);

// Release all plugin panels and the shared CoreWebView2Environment on the UI
// thread. Safe to call repeatedly.
void Shutdown();

bool IsEnvironmentReady();
bool HasEnvironmentFailed();
std::string LastEnvironmentDiagnostic();

bool HasPanel(const std::string& panelId);
bool IsReady(const std::string& panelId);
bool IsDocumentReady(const std::string& panelId);
bool IsVisible(const std::string& panelId);
std::string LastDiagnostic(const std::string& panelId);

#if defined(GAKULOADER_WEBVIEW2_TEST)
struct TestHooks {
    bool failInitialize = false;
    bool failEnvironment = false;
    bool failDocument = false;
    std::function<void(const EnvironmentConfig&)> ensureEnvironment;
    std::function<void(const std::string&, const PanelConfig&)> initializePanel;
    std::function<void(const std::string&, bool)> visible;
    std::function<void(const std::string&, const RECT&)> bounds;
    std::function<void(float)> dpi;
    std::function<void(const std::string&)> focus;
    std::function<void(const std::string&, const std::string&)> postMessage;
    std::function<void(const std::string&, const std::string&)> documentScript;
    std::function<void(const std::string&, const std::string&)> executeScript;
    std::function<void(const std::string&)> destroyPanel;
    std::function<void()> shutdown;
};
void ConfigureTest(const TestHooks& hooks);
void SimulateProcessFailedForTest(const std::string& panelId, const std::string& diagnostic, bool browserExited = false);
void SimulateFocusForTest(const std::string& panelId, bool focused);
void SimulateTabLeaveForTest(const std::string& panelId, bool forward);
#endif

} // namespace GakuLoader::WebView2Ui
