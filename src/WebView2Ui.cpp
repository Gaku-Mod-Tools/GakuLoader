// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

#include "WebView2Ui.hpp"
#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>
#include <wrl.h>
#include <atomic>
#include <cwctype>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;

namespace GakuLoader::WebView2Ui {
namespace {

using FnGetVersion = HRESULT(STDAPICALLTYPE*)(PCWSTR, LPWSTR*);
using FnCreateEnvironment = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);

struct EnvironmentState {
    EnvironmentConfig config;
    ComPtr<ICoreWebView2Environment> environment;
    std::atomic_bool ready{false};
    std::atomic_bool failed{false};
    bool initializing = false;
    std::atomic<uint64_t> generation{0};
    std::string diagnostic;
    std::vector<std::string> pendingPanels;
};

struct PanelState {
    std::string id;
    HWND parent = nullptr;
    PanelConfig config;
    Callbacks callbacks;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    std::atomic_bool ready{false};
    std::atomic_bool documentReady{false};
    std::atomic_bool visible{false};
    std::atomic<uint64_t> generation{0};
    std::string diagnostic;
    std::wstring navigationUri;
    std::wstring trustedPrefix;
    std::wstring entryUrl;
    bool initializing = false;
    bool failed = false;
    bool hasBounds = false;
    bool hasFocus = false;
    bool wantsTextInput = false;
    RECT lastBounds{};
};

EnvironmentState g_env;
std::unordered_map<std::string, std::unique_ptr<PanelState>> g_panels;
float g_dpiScale = 1.0f;
HMODULE g_loader = nullptr;
FnCreateEnvironment g_createEnvironment = nullptr;
#if defined(GAKULOADER_WEBVIEW2_TEST)
TestHooks g_testHooks;
#endif

std::string Narrow(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
        nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), out.data(), bytes,
        nullptr, nullptr);
    return out;
}

PanelState* FindPanel(const std::string& panelId) {
    const auto it = g_panels.find(panelId);
    return it == g_panels.end() ? nullptr : it->second.get();
}

PanelState* FindPanelByGeneration(const std::string& panelId, uint64_t generation) {
    PanelState* panel = FindPanel(panelId);
    if (!panel || panel->generation.load() != generation) return nullptr;
    return panel;
}

bool LoadLoader() {
    if (g_createEnvironment) return true;
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&LoadLoader), &self)) return false;
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(self, path, ARRAYSIZE(path));
    if (!length || length >= ARRAYSIZE(path)) return false;
    const std::filesystem::path loader = std::filesystem::path(path).parent_path() / L"WebView2Loader.dll";
    g_loader = LoadLibraryW(loader.c_str());
    if (!g_loader) return false;
    const auto getVersion = reinterpret_cast<FnGetVersion>(
        GetProcAddress(g_loader, "GetAvailableCoreWebView2BrowserVersionString"));
    g_createEnvironment = reinterpret_cast<FnCreateEnvironment>(
        GetProcAddress(g_loader, "CreateCoreWebView2EnvironmentWithOptions"));
    if (!getVersion || !g_createEnvironment) {
        FreeLibrary(g_loader);
        g_loader = nullptr;
        g_createEnvironment = nullptr;
        return false;
    }
    LPWSTR version = nullptr;
    const HRESULT result = getVersion(nullptr, &version);
    if (version) CoTaskMemFree(version);
    if (FAILED(result) || !version) {
        FreeLibrary(g_loader);
        g_loader = nullptr;
        g_createEnvironment = nullptr;
        return false;
    }
    return true;
}

bool TrustedOrigin(const PanelState& panel, const std::wstring& uri) {
    const std::wstring& prefix = panel.trustedPrefix;
    if (prefix.empty() || uri.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i)
        if (std::towlower(uri[i]) != std::towlower(prefix[i])) return false;
    return true;
}

void FailPanel(PanelState& panel, const std::string& diagnostic) {
    panel.diagnostic = diagnostic;
    panel.ready.store(false);
    panel.documentReady.store(false);
    panel.visible.store(false);
    panel.initializing = false;
    panel.failed = true;
    panel.hasFocus = false;
    panel.wantsTextInput = false;
    if (panel.controller) panel.controller->put_IsVisible(FALSE);
    if (panel.callbacks.onFocusChanged) panel.callbacks.onFocusChanged(false);
    if (panel.callbacks.onTextInputChanged) panel.callbacks.onTextInputChanged(false);
    if (panel.callbacks.onReady) panel.callbacks.onReady(false, diagnostic);
}

void FailEnvironment(const std::string& diagnostic) {
    g_env.diagnostic = diagnostic;
    g_env.ready.store(false);
    g_env.failed.store(true);
    g_env.initializing = false;
    g_env.environment.Reset();
    const auto pending = std::move(g_env.pendingPanels);
    g_env.pendingPanels.clear();
    for (const auto& panelId : pending) {
        if (PanelState* panel = FindPanel(panelId)) {
            FailPanel(*panel, diagnostic);
        }
    }
}

void ConfigureWebview(PanelState& panel) {
    ComPtr<ICoreWebView2Settings> settings;
    if (SUCCEEDED(panel.webview->get_Settings(&settings)) && settings) {
        settings->put_AreDefaultContextMenusEnabled(FALSE);
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_IsZoomControlEnabled(FALSE);
        settings->put_AreDevToolsEnabled(g_env.config.devToolsEnabled ? TRUE : FALSE);
        ComPtr<ICoreWebView2Settings3> settings3;
        if (SUCCEEDED(settings.As(&settings3)) && settings3)
            settings3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
    }
}

void ApplyControllerScale(PanelState& panel) {
    if (!panel.controller) return;
    ComPtr<ICoreWebView2Controller3> controller3;
    if (SUCCEEDED(panel.controller.As(&controller3)) && controller3) {
        const float scale = panel.config.dpiScale > 0.0f ? panel.config.dpiScale : g_dpiScale;
        controller3->put_RasterizationScale(scale > 0.0f ? scale : 1.0f);
        controller3->put_BoundsMode(COREWEBVIEW2_BOUNDS_MODE_USE_RAW_PIXELS);
        controller3->put_ShouldDetectMonitorScaleChanges(FALSE);
    }
}

void HandleBrowserProcessExited(const std::string& diagnostic) {
    g_env.generation.fetch_add(1);
    g_env.ready.store(false);
    g_env.failed.store(true);
    g_env.initializing = false;
    g_env.diagnostic = diagnostic;
    g_env.pendingPanels.clear();
    for (auto& [id, panel] : g_panels) {
        (void)id;
        if (panel) FailPanel(*panel, diagnostic);
    }
    g_env.environment.Reset();
}

HRESULT OnControllerCreated(const std::string& panelId, uint64_t generation,
    HRESULT result, ICoreWebView2Controller* controller) {
    PanelState* panel = FindPanelByGeneration(panelId, generation);
    if (!panel) {
        if (controller) controller->Close();
        return S_OK;
    }
    if (FAILED(result) || !controller) {
        FailPanel(*panel, "WebView2 controller creation failed (HRESULT " +
            std::to_string(static_cast<long>(result)) + ").");
        return S_OK;
    }
    panel->controller = controller;
    if (FAILED(panel->controller->get_CoreWebView2(&panel->webview)) || !panel->webview) {
        FailPanel(*panel, "WebView2 core could not be acquired.");
        return S_OK;
    }
    ConfigureWebview(*panel);
    ApplyControllerScale(*panel);

    ComPtr<ICoreWebView2Controller4> controller4;
    if (SUCCEEDED(panel->controller.As(&controller4)) && controller4)
        controller4->put_AllowExternalDrop(FALSE);

    panel->controller->add_GotFocus(
        Callback<ICoreWebView2FocusChangedEventHandler>(
            [panelId, generation](ICoreWebView2Controller*, IUnknown*) -> HRESULT {
                PanelState* p = FindPanelByGeneration(panelId, generation);
                if (!p) return S_OK;
                p->hasFocus = true;
                p->wantsTextInput = true;
                if (p->callbacks.onFocusChanged) p->callbacks.onFocusChanged(true);
                if (p->callbacks.onTextInputChanged) p->callbacks.onTextInputChanged(true);
                return S_OK;
            }).Get(), nullptr);
    panel->controller->add_LostFocus(
        Callback<ICoreWebView2FocusChangedEventHandler>(
            [panelId, generation](ICoreWebView2Controller*, IUnknown*) -> HRESULT {
                PanelState* p = FindPanelByGeneration(panelId, generation);
                if (!p) return S_OK;
                p->hasFocus = false;
                p->wantsTextInput = false;
                if (p->callbacks.onFocusChanged) p->callbacks.onFocusChanged(false);
                if (p->callbacks.onTextInputChanged) p->callbacks.onTextInputChanged(false);
                return S_OK;
            }).Get(), nullptr);
    panel->controller->add_MoveFocusRequested(
        Callback<ICoreWebView2MoveFocusRequestedEventHandler>(
            [panelId, generation](ICoreWebView2Controller*, ICoreWebView2MoveFocusRequestedEventArgs* args) -> HRESULT {
                PanelState* p = FindPanelByGeneration(panelId, generation);
                if (!p || !args) return S_OK;
                COREWEBVIEW2_MOVE_FOCUS_REASON reason{};
                args->get_Reason(&reason);
                if (reason == COREWEBVIEW2_MOVE_FOCUS_REASON_NEXT ||
                    reason == COREWEBVIEW2_MOVE_FOCUS_REASON_PREVIOUS) {
                    args->put_Handled(TRUE);
                    p->hasFocus = false;
                    p->wantsTextInput = false;
                    if (p->callbacks.onFocusChanged) p->callbacks.onFocusChanged(false);
                    if (p->callbacks.onTextInputChanged) p->callbacks.onTextInputChanged(false);
                    if (p->callbacks.onTabLeave)
                        p->callbacks.onTabLeave(reason == COREWEBVIEW2_MOVE_FOCUS_REASON_NEXT);
                }
                return S_OK;
            }).Get(), nullptr);
    panel->controller->add_AcceleratorKeyPressed(
        Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
            [panelId, generation](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
                if (!FindPanelByGeneration(panelId, generation) || !args) return S_OK;
                UINT key = 0;
                args->get_VirtualKey(&key);
                if (key == VK_TAB) args->put_Handled(FALSE);
                return S_OK;
            }).Get(), nullptr);

    ComPtr<ICoreWebView2_3> webview3;
    if (FAILED(panel->webview.As(&webview3)) || !webview3) {
        FailPanel(*panel, "WebView2 Runtime does not support virtual host folder mapping.");
        return S_OK;
    }
    const HRESULT mapped = webview3->SetVirtualHostNameToFolderMapping(panel->config.virtualHost.c_str(),
        panel->config.assetsDir.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
    if (FAILED(mapped)) {
        FailPanel(*panel, "WebView2 virtual host mapping failed (HRESULT " +
            std::to_string(static_cast<long>(mapped)) + ").");
        return S_OK;
    }

    panel->webview->add_WebMessageReceived(
        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [panelId, generation](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                PanelState* p = FindPanelByGeneration(panelId, generation);
                if (!p || !args || !p->callbacks.onMessage) return S_OK;
                LPWSTR source = nullptr;
                args->get_Source(&source);
                const bool trusted = source && TrustedOrigin(*p, source);
                if (source) CoTaskMemFree(source);
                if (!trusted) return S_OK;
                LPWSTR json = nullptr;
                if (SUCCEEDED(args->get_WebMessageAsJson(&json)) && json) {
                    p->callbacks.onMessage(Narrow(json));
                    CoTaskMemFree(json);
                }
                return S_OK;
            }).Get(), nullptr);

    panel->webview->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [panelId, generation](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                PanelState* p = FindPanelByGeneration(panelId, generation);
                if (!p || !args) return S_OK;
                LPWSTR uri = nullptr;
                args->get_Uri(&uri);
                const bool trusted = uri && TrustedOrigin(*p, uri);
                if (!trusted) {
                    args->put_Cancel(TRUE);
                    p->navigationUri.clear();
                } else {
                    p->navigationUri = uri;
                    p->documentReady.store(false);
                }
                if (uri) CoTaskMemFree(uri);
                return S_OK;
            }).Get(), nullptr);

    panel->webview->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [panelId, generation](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                PanelState* p = FindPanelByGeneration(panelId, generation);
                if (!p || !args) return S_OK;
                const bool trusted = TrustedOrigin(*p, p->navigationUri);
                if (!trusted) return S_OK;
                BOOL success = FALSE;
                args->get_IsSuccess(&success);
                if (success) {
                    p->documentReady.store(true);
                    if (p->callbacks.onDocumentReady) p->callbacks.onDocumentReady();
                } else {
                    COREWEBVIEW2_WEB_ERROR_STATUS status{};
                    args->get_WebErrorStatus(&status);
                    FailPanel(*p, "WebView2 navigation failed (web error status " +
                        std::to_string(static_cast<int>(status)) +
                        "). Check the frontend assets, or set [host] ui=imgui.");
                }
                return S_OK;
            }).Get(), nullptr);

    panel->webview->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [panelId, generation](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                if (!FindPanelByGeneration(panelId, generation) || !args) return S_OK;
                args->put_Handled(TRUE);
                return S_OK;
            }).Get(), nullptr);

    panel->webview->add_PermissionRequested(
        Callback<ICoreWebView2PermissionRequestedEventHandler>(
            [panelId, generation](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT {
                if (!FindPanelByGeneration(panelId, generation) || !args) return S_OK;
                args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
                return S_OK;
            }).Get(), nullptr);

    panel->webview->add_ProcessFailed(
        Callback<ICoreWebView2ProcessFailedEventHandler>(
            [panelId, generation](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* args) -> HRESULT {
                PanelState* p = FindPanelByGeneration(panelId, generation);
                if (!p) return S_OK;
                COREWEBVIEW2_PROCESS_FAILED_KIND kind{};
                if (args) args->get_ProcessFailedKind(&kind);
                const std::string diag = "WebView2 process failed (kind " +
                    std::to_string(static_cast<int>(kind)) +
                    "). Falling back to the ImGui plugin panel.";
                if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED) {
                    HandleBrowserProcessExited(diag);
                } else {
                    FailPanel(*p, diag);
                }
                return S_OK;
            }).Get(), nullptr);

    RECT bounds{};
    if (panel->hasBounds) bounds = panel->lastBounds;
    else GetClientRect(panel->parent, &bounds);
    panel->controller->put_Bounds(bounds);
    panel->lastBounds = bounds;
    panel->hasBounds = true;
    panel->controller->put_IsVisible(FALSE);
    panel->ready.store(true);
    panel->documentReady.store(false);
    panel->initializing = false;
    panel->failed = false;
    if (panel->callbacks.onReady) panel->callbacks.onReady(true, "");
    const HRESULT navigated = panel->webview->Navigate(panel->entryUrl.c_str());
    if (FAILED(navigated)) {
        FailPanel(*panel, "WebView2 navigation request failed (HRESULT " +
            std::to_string(static_cast<long>(navigated)) + ").");
    }
    return S_OK;
}

void RequestControllerForPanel(PanelState& panel) {
    if (!g_env.environment) return;
    const std::string panelId = panel.id;
    const uint64_t generation = panel.generation.load();
    const HRESULT hr = g_env.environment->CreateCoreWebView2Controller(panel.parent,
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [panelId, generation](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                return OnControllerCreated(panelId, generation, result, controller);
            }).Get());
    if (FAILED(hr)) {
        FailPanel(panel, "WebView2 controller request failed (HRESULT " +
            std::to_string(static_cast<long>(hr)) + ").");
    }
}

HRESULT OnEnvironmentCreated(uint64_t envGeneration, HRESULT result, ICoreWebView2Environment* environment) {
    if (envGeneration != g_env.generation.load()) return S_OK;
    if (FAILED(result) || !environment) {
        FailEnvironment("WebView2 environment creation failed (HRESULT " +
            std::to_string(static_cast<long>(result)) +
            "). Install the Evergreen WebView2 Runtime, or set [host] ui=imgui.");
        return S_OK;
    }
    g_env.environment = environment;
    g_env.ready.store(true);
    g_env.failed.store(false);
    g_env.initializing = false;
    g_env.diagnostic.clear();

    const auto pending = std::move(g_env.pendingPanels);
    g_env.pendingPanels.clear();
    for (const auto& panelId : pending) {
        if (PanelState* panel = FindPanel(panelId)) {
            if (!panel->failed && panel->initializing) {
                RequestControllerForPanel(*panel);
            }
        }
    }
    return S_OK;
}

} // namespace

bool EnsureEnvironment(const EnvironmentConfig& config) {
#if defined(GAKULOADER_WEBVIEW2_TEST)
    if (g_env.ready.load()) return true;
#else
    if (g_env.ready.load() && g_env.environment) return true;
#endif
    if (g_env.initializing) return true;
    if (g_env.failed.load()) return false;

    g_env.config = config;
    g_env.diagnostic.clear();

#if defined(GAKULOADER_WEBVIEW2_TEST)
    if (g_testHooks.ensureEnvironment) g_testHooks.ensureEnvironment(config);
    if (g_testHooks.failEnvironment) {
        FailEnvironment("WebView2 test environment failure.");
        return false;
    }
    g_env.generation.fetch_add(1);
    g_env.ready.store(true);
    g_env.failed.store(false);
    g_env.initializing = false;
    return true;
#else
    if (!LoadLoader()) {
        FailEnvironment("WebView2 Runtime or WebView2Loader.dll is unavailable. Install the Evergreen WebView2 "
            "Runtime and keep WebView2Loader.dll beside version.dll, or set [host] ui=imgui.");
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(config.userDataDir, ec);
    if (ec) {
        FailEnvironment("Cannot create the WebView2 user data folder at " + Narrow(config.userDataDir.wstring()) +
            " (" + ec.message() + ").");
        return false;
    }
    const uint64_t envGeneration = g_env.generation.fetch_add(1) + 1;
    g_env.initializing = true;

    auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    if (options) {
        options->put_Language(config.languageTag.c_str());
        if (config.devToolsEnabled) options->put_AdditionalBrowserArguments(L"--auto-open-devtools-for-tabs");
    }
    ComPtr<ICoreWebView2EnvironmentOptions> optionsBase;
    if (options) options.As(&optionsBase);
    const HRESULT hr = g_createEnvironment(nullptr, config.userDataDir.c_str(), optionsBase.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [envGeneration](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
                return OnEnvironmentCreated(envGeneration, result, environment);
            }).Get());
    if (FAILED(hr)) {
        FailEnvironment("WebView2 environment request failed (HRESULT " +
            std::to_string(static_cast<long>(hr)) + ").");
        return false;
    }
    return true;
#endif
}

bool InitializePanel(const std::string& panelId, HWND parent,
    const EnvironmentConfig& envConfig, const PanelConfig& panelConfig, Callbacks callbacks) {
    if (panelId.empty()) return false;
    DestroyPanel(panelId);

    auto owned = std::make_unique<PanelState>();
    PanelState& panel = *owned;
    panel.id = panelId;
    panel.parent = parent;
    panel.config = panelConfig;
    panel.callbacks = std::move(callbacks);
    panel.generation.store(1);
    panel.trustedPrefix = L"https://" + panelConfig.virtualHost + L"/";
    panel.entryUrl = panel.trustedPrefix + panelConfig.entryPath;
    g_dpiScale = panelConfig.dpiScale > 0.0f ? panelConfig.dpiScale : g_dpiScale;
    g_panels[panelId] = std::move(owned);

    if (!parent) {
        FailPanel(panel, "WebView2 has no parent window.");
        return false;
    }
    if (panelConfig.virtualHost.empty() || panelConfig.entryPath.empty() ||
        panelConfig.entryPath.front() == L'/' || panelConfig.entryPath.front() == L'\\' ||
        panelConfig.entryPath.find(L"..") != std::wstring::npos) {
        FailPanel(panel, "WebView2 panel assets configuration is invalid.");
        return false;
    }

#if defined(GAKULOADER_WEBVIEW2_TEST)
    if (!EnsureEnvironment(envConfig)) {
        FailPanel(panel, g_env.diagnostic);
        return false;
    }
    if (g_testHooks.initializePanel) g_testHooks.initializePanel(panelId, panelConfig);
    if (g_testHooks.failInitialize) {
        FailPanel(panel, "WebView2 test initialization failure.");
        return false;
    }
    panel.ready.store(true);
    panel.documentReady.store(!g_testHooks.failDocument);
    if (panel.callbacks.onReady) panel.callbacks.onReady(true, "");
    if (panel.documentReady.load() && panel.callbacks.onDocumentReady) panel.callbacks.onDocumentReady();
    return true;
#else
    const std::filesystem::path entry = panelConfig.assetsDir / panelConfig.entryPath;
    std::error_code ec;
    if (!std::filesystem::exists(entry, ec) || ec) {
        FailPanel(panel, "Web panel assets are missing at " + Narrow(entry.wstring()) +
            ". Build the plugin frontend or set [host] ui=imgui.");
        return false;
    }
    if (!EnsureEnvironment(envConfig)) {
        FailPanel(panel, g_env.diagnostic);
        return false;
    }
    panel.initializing = true;
    if (g_env.ready.load() && g_env.environment) {
        RequestControllerForPanel(panel);
    } else {
        g_env.pendingPanels.push_back(panelId);
    }
    return true;
#endif
}

void SetVisible(const std::string& panelId, bool visible) {
    PanelState* panel = FindPanel(panelId);
    if (!panel) return;
#if defined(GAKULOADER_WEBVIEW2_TEST)
    if (g_testHooks.visible) g_testHooks.visible(panelId, visible);
    if (visible && !panel->documentReady.load()) return;
    panel->visible.store(visible);
    if (!visible) {
        panel->hasFocus = false;
        panel->wantsTextInput = false;
        if (panel->callbacks.onFocusChanged) panel->callbacks.onFocusChanged(false);
        if (panel->callbacks.onTextInputChanged) panel->callbacks.onTextInputChanged(false);
    }
    return;
#else
    if (!panel->controller) return;
    if (visible && !panel->documentReady.load()) return;
    if (visible && (!panel->hasBounds || panel->lastBounds.right <= panel->lastBounds.left ||
        panel->lastBounds.bottom <= panel->lastBounds.top)) return;
    if (panel->visible.load() == visible) return;
    panel->controller->put_IsVisible(visible ? TRUE : FALSE);
    panel->visible.store(visible);
    if (!visible) {
        panel->hasFocus = false;
        panel->wantsTextInput = false;
        if (panel->callbacks.onFocusChanged) panel->callbacks.onFocusChanged(false);
        if (panel->callbacks.onTextInputChanged) panel->callbacks.onTextInputChanged(false);
    }
#endif
}

void SetBounds(const std::string& panelId, const RECT& clientBounds) {
    PanelState* panel = FindPanel(panelId);
    if (!panel) return;
#if defined(GAKULOADER_WEBVIEW2_TEST)
    if (g_testHooks.bounds) g_testHooks.bounds(panelId, clientBounds);
    panel->lastBounds = clientBounds;
    panel->hasBounds = true;
    return;
#else
    if (!panel->controller) return;
    if (panel->hasBounds && panel->lastBounds.left == clientBounds.left &&
        panel->lastBounds.top == clientBounds.top &&
        panel->lastBounds.right == clientBounds.right &&
        panel->lastBounds.bottom == clientBounds.bottom) return;
    panel->controller->put_Bounds(clientBounds);
    panel->lastBounds = clientBounds;
    panel->hasBounds = true;
#endif
}

void SetDpiScale(float scale) {
    g_dpiScale = scale > 0.0f ? scale : 1.0f;
#if defined(GAKULOADER_WEBVIEW2_TEST)
    if (g_testHooks.dpi) g_testHooks.dpi(g_dpiScale);
#endif
    for (auto& [id, panel] : g_panels) {
        (void)id;
        if (!panel) continue;
        panel->config.dpiScale = g_dpiScale;
        ApplyControllerScale(*panel);
    }
}

void NotifyParentMoved() {
    for (auto& [id, panel] : g_panels) {
        (void)id;
        if (panel && panel->controller) {
            panel->controller->NotifyParentWindowPositionChanged();
        }
    }
}

void Focus(const std::string& panelId) {
    PanelState* panel = FindPanel(panelId);
    if (!panel) return;
#if defined(GAKULOADER_WEBVIEW2_TEST)
    if (g_testHooks.focus) g_testHooks.focus(panelId);
    panel->hasFocus = true;
    return;
#else
    if (panel->controller && panel->documentReady.load())
        panel->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
#endif
}

void PostMessageJson(const std::string& panelId, const std::string& json) {
    PanelState* panel = FindPanel(panelId);
    if (!panel) return;
#if defined(GAKULOADER_WEBVIEW2_TEST)
    if (g_testHooks.postMessage) g_testHooks.postMessage(panelId, json);
    return;
#else
    if (!panel->webview || !panel->documentReady.load()) return;
    const int wide = MultiByteToWideChar(CP_UTF8, 0, json.c_str(), static_cast<int>(json.size()), nullptr, 0);
    if (wide <= 0) return;
    std::wstring buffer(static_cast<size_t>(wide), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, json.c_str(), static_cast<int>(json.size()), buffer.data(), wide);
    panel->webview->PostWebMessageAsJson(buffer.c_str());
#endif
}

void DestroyPanel(const std::string& panelId) {
    const auto it = g_panels.find(panelId);
    if (it == g_panels.end()) return;
    auto panel = std::move(it->second);
    g_panels.erase(it);
    if (!panel) return;
    panel->generation.fetch_add(1);
    panel->ready.store(false);
    panel->documentReady.store(false);
    panel->visible.store(false);
    panel->initializing = false;
#if defined(GAKULOADER_WEBVIEW2_TEST)
    if (g_testHooks.destroyPanel) g_testHooks.destroyPanel(panelId);
#endif
    if (panel->controller) panel->controller->Close();
    panel->webview.Reset();
    panel->controller.Reset();
}

void Shutdown() {
    g_env.generation.fetch_add(1);
    g_env.ready.store(false);
    g_env.failed.store(false);
    g_env.initializing = false;
    g_env.pendingPanels.clear();
    g_env.diagnostic.clear();

    std::vector<std::string> ids;
    ids.reserve(g_panels.size());
    for (const auto& [id, panel] : g_panels) {
        (void)panel;
        ids.push_back(id);
    }
    for (const auto& id : ids) {
        DestroyPanel(id);
    }
#if defined(GAKULOADER_WEBVIEW2_TEST)
    if (g_testHooks.shutdown) g_testHooks.shutdown();
#endif
    g_env.environment.Reset();
    g_env.config = {};
}

bool IsEnvironmentReady() { return g_env.ready.load(); }
bool HasEnvironmentFailed() { return g_env.failed.load(); }
std::string LastEnvironmentDiagnostic() { return g_env.diagnostic; }

bool HasPanel(const std::string& panelId) { return FindPanel(panelId) != nullptr; }
bool IsReady(const std::string& panelId) {
    const PanelState* panel = FindPanel(panelId);
    return panel && panel->ready.load();
}
bool IsDocumentReady(const std::string& panelId) {
    const PanelState* panel = FindPanel(panelId);
    return panel && panel->documentReady.load();
}
bool IsVisible(const std::string& panelId) {
    const PanelState* panel = FindPanel(panelId);
    return panel && panel->visible.load();
}
std::string LastDiagnostic(const std::string& panelId) {
    const PanelState* panel = FindPanel(panelId);
    return panel ? panel->diagnostic : g_env.diagnostic;
}

#if defined(GAKULOADER_WEBVIEW2_TEST)
void ConfigureTest(const TestHooks& hooks) { g_testHooks = hooks; }
void SimulateProcessFailedForTest(const std::string& panelId, const std::string& diagnostic, bool browserExited) {
    if (browserExited) {
        HandleBrowserProcessExited(diagnostic);
        return;
    }
    if (PanelState* panel = FindPanel(panelId)) FailPanel(*panel, diagnostic);
}
void SimulateFocusForTest(const std::string& panelId, bool focused) {
    PanelState* panel = FindPanel(panelId);
    if (!panel) return;
    panel->hasFocus = focused;
    panel->wantsTextInput = focused;
    if (panel->callbacks.onFocusChanged) panel->callbacks.onFocusChanged(focused);
    if (panel->callbacks.onTextInputChanged) panel->callbacks.onTextInputChanged(focused);
}
void SimulateTabLeaveForTest(const std::string& panelId, bool forward) {
    PanelState* panel = FindPanel(panelId);
    if (!panel) return;
    panel->hasFocus = false;
    panel->wantsTextInput = false;
    if (panel->callbacks.onFocusChanged) panel->callbacks.onFocusChanged(false);
    if (panel->callbacks.onTextInputChanged) panel->callbacks.onTextInputChanged(false);
    if (panel->callbacks.onTabLeave) panel->callbacks.onTabLeave(forward);
}
#endif

} // namespace GakuLoader::WebView2Ui
