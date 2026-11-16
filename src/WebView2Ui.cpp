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
#include <utility>

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;

namespace GakuLoader::WebView2Ui {
namespace {

using FnGetVersion = HRESULT(STDAPICALLTYPE*)(PCWSTR, LPWSTR*);
using FnCreateEnvironment = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);

struct State {
    HWND parent = nullptr;
    Callbacks callbacks;
    Config config;
    ComPtr<ICoreWebView2Environment> environment;
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
    bool hasBounds = false;
    bool hasFocus = false;
    bool wantsTextInput = false;
    RECT lastBounds{};
};
State g_state;
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

bool TrustedOrigin(const std::wstring& uri) {
    const std::wstring& prefix = g_state.trustedPrefix;
    if (prefix.empty() || uri.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i)
        if (std::towlower(uri[i]) != std::towlower(prefix[i])) return false;
    return true;
}

void Fail(const std::string& diagnostic) {
    g_state.diagnostic = diagnostic;
    g_state.ready.store(false);
    g_state.documentReady.store(false);
    g_state.visible.store(false);
    g_state.initializing = false;
    g_state.hasFocus = false;
    g_state.wantsTextInput = false;
    if (g_state.controller) g_state.controller->put_IsVisible(FALSE);
    if (g_state.callbacks.onFocusChanged) g_state.callbacks.onFocusChanged(false);
    if (g_state.callbacks.onTextInputChanged) g_state.callbacks.onTextInputChanged(false);
    if (g_state.callbacks.onReady) g_state.callbacks.onReady(false, diagnostic);
}

void ConfigureWebview() {
    ComPtr<ICoreWebView2Settings> settings;
    if (SUCCEEDED(g_state.webview->get_Settings(&settings)) && settings) {
        settings->put_AreDefaultContextMenusEnabled(FALSE);
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_IsZoomControlEnabled(FALSE);
        settings->put_AreDevToolsEnabled(g_state.config.devToolsEnabled ? TRUE : FALSE);
        ComPtr<ICoreWebView2Settings3> settings3;
        if (SUCCEEDED(settings.As(&settings3)) && settings3)
            settings3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
    }
}

void ApplyControllerScale() {
    ComPtr<ICoreWebView2Controller3> controller3;
    if (SUCCEEDED(g_state.controller.As(&controller3)) && controller3) {
        controller3->put_RasterizationScale(g_state.config.dpiScale > 0.0f ? g_state.config.dpiScale : 1.0f);
        controller3->put_BoundsMode(COREWEBVIEW2_BOUNDS_MODE_USE_RAW_PIXELS);
        controller3->put_ShouldDetectMonitorScaleChanges(FALSE);
    }
}

HRESULT OnControllerCreated(HRESULT result, ICoreWebView2Controller* controller, uint64_t generation) {
    if (generation != g_state.generation.load()) return S_OK;
    if (FAILED(result) || !controller) {
        Fail("WebView2 controller creation failed (HRESULT " +
             std::to_string(static_cast<long>(result)) + ").");
        return S_OK;
    }
    g_state.controller = controller;
    if (FAILED(g_state.controller->get_CoreWebView2(&g_state.webview)) || !g_state.webview) {
        Fail("WebView2 core could not be acquired.");
        return S_OK;
    }
    ConfigureWebview();
    ApplyControllerScale();

    ComPtr<ICoreWebView2Controller4> controller4;
    if (SUCCEEDED(g_state.controller.As(&controller4)) && controller4)
        controller4->put_AllowExternalDrop(FALSE);

    g_state.controller->add_GotFocus(
        Callback<ICoreWebView2FocusChangedEventHandler>(
            [generation](ICoreWebView2Controller*, IUnknown*) -> HRESULT {
                if (generation != g_state.generation.load()) return S_OK;
                g_state.hasFocus = true;
                g_state.wantsTextInput = true;
                if (g_state.callbacks.onFocusChanged) g_state.callbacks.onFocusChanged(true);
                if (g_state.callbacks.onTextInputChanged) g_state.callbacks.onTextInputChanged(true);
                return S_OK;
            }).Get(), nullptr);
    g_state.controller->add_LostFocus(
        Callback<ICoreWebView2FocusChangedEventHandler>(
            [generation](ICoreWebView2Controller*, IUnknown*) -> HRESULT {
                if (generation != g_state.generation.load()) return S_OK;
                g_state.hasFocus = false;
                g_state.wantsTextInput = false;
                if (g_state.callbacks.onFocusChanged) g_state.callbacks.onFocusChanged(false);
                if (g_state.callbacks.onTextInputChanged) g_state.callbacks.onTextInputChanged(false);
                return S_OK;
            }).Get(), nullptr);
    g_state.controller->add_MoveFocusRequested(
        Callback<ICoreWebView2MoveFocusRequestedEventHandler>(
            [generation](ICoreWebView2Controller*, ICoreWebView2MoveFocusRequestedEventArgs* args) -> HRESULT {
                if (generation != g_state.generation.load() || !args) return S_OK;
                COREWEBVIEW2_MOVE_FOCUS_REASON reason{};
                args->get_Reason(&reason);
                if (reason == COREWEBVIEW2_MOVE_FOCUS_REASON_NEXT ||
                    reason == COREWEBVIEW2_MOVE_FOCUS_REASON_PREVIOUS) {
                    args->put_Handled(TRUE);
                    g_state.hasFocus = false;
                    g_state.wantsTextInput = false;
                    if (g_state.callbacks.onFocusChanged) g_state.callbacks.onFocusChanged(false);
                    if (g_state.callbacks.onTextInputChanged) g_state.callbacks.onTextInputChanged(false);
                    if (g_state.callbacks.onTabLeave)
                        g_state.callbacks.onTabLeave(reason == COREWEBVIEW2_MOVE_FOCUS_REASON_NEXT);
                }
                return S_OK;
            }).Get(), nullptr);
    g_state.controller->add_AcceleratorKeyPressed(
        Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
            [generation](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
                if (generation != g_state.generation.load() || !args) return S_OK;
                UINT key = 0;
                args->get_VirtualKey(&key);
                if (key == VK_TAB) args->put_Handled(FALSE);
                return S_OK;
            }).Get(), nullptr);

    ComPtr<ICoreWebView2_3> webview3;
    if (FAILED(g_state.webview.As(&webview3)) || !webview3) {
        Fail("WebView2 Runtime does not support virtual host folder mapping.");
        return S_OK;
    }
    const HRESULT mapped = webview3->SetVirtualHostNameToFolderMapping(g_state.config.virtualHost.c_str(),
        g_state.config.assetsDir.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
    if (FAILED(mapped)) {
        Fail("WebView2 virtual host mapping failed (HRESULT " +
             std::to_string(static_cast<long>(mapped)) + ").");
        return S_OK;
    }

    g_state.webview->add_WebMessageReceived(
        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [generation](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                if (generation != g_state.generation.load()) return S_OK;
                if (!args || !g_state.callbacks.onMessage) return S_OK;
                LPWSTR source = nullptr;
                args->get_Source(&source);
                const bool trusted = source && TrustedOrigin(source);
                if (source) CoTaskMemFree(source);
                if (!trusted) return S_OK;
                LPWSTR json = nullptr;
                if (SUCCEEDED(args->get_WebMessageAsJson(&json)) && json) {
                    g_state.callbacks.onMessage(Narrow(json));
                    CoTaskMemFree(json);
                }
                return S_OK;
            }).Get(), nullptr);

    g_state.webview->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [generation](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                if (generation != g_state.generation.load() || !args) return S_OK;
                LPWSTR uri = nullptr;
                args->get_Uri(&uri);
                const bool trusted = uri && TrustedOrigin(uri);
                if (!trusted) {
                    args->put_Cancel(TRUE);
                    g_state.navigationUri.clear();
                } else {
                    g_state.navigationUri = uri;
                    g_state.documentReady.store(false);
                }
                if (uri) CoTaskMemFree(uri);
                return S_OK;
            }).Get(), nullptr);

    g_state.webview->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [generation](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                if (generation != g_state.generation.load() || !args) return S_OK;
                const bool trusted = TrustedOrigin(g_state.navigationUri);
                if (!trusted) return S_OK;
                BOOL success = FALSE;
                args->get_IsSuccess(&success);
                if (success) {
                    g_state.documentReady.store(true);
                    if (g_state.callbacks.onDocumentReady) g_state.callbacks.onDocumentReady();
                } else {
                    COREWEBVIEW2_WEB_ERROR_STATUS status{};
                    args->get_WebErrorStatus(&status);
                    Fail("WebView2 navigation failed (web error status " +
                         std::to_string(static_cast<int>(status)) +
                         "). Check the frontend assets, or set [host] ui=imgui.");
                }
                return S_OK;
            }).Get(), nullptr);

    g_state.webview->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [generation](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                if (generation != g_state.generation.load() || !args) return S_OK;
                args->put_Handled(TRUE);
                return S_OK;
            }).Get(), nullptr);

    g_state.webview->add_PermissionRequested(
        Callback<ICoreWebView2PermissionRequestedEventHandler>(
            [generation](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT {
                if (generation != g_state.generation.load() || !args) return S_OK;
                args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
                return S_OK;
            }).Get(), nullptr);

    g_state.webview->add_ProcessFailed(
        Callback<ICoreWebView2ProcessFailedEventHandler>(
            [generation](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* args) -> HRESULT {
                if (generation != g_state.generation.load()) return S_OK;
                COREWEBVIEW2_PROCESS_FAILED_KIND kind{};
                if (args) args->get_ProcessFailedKind(&kind);
                Fail("WebView2 process failed (kind " + std::to_string(static_cast<int>(kind)) +
                     "). Falling back to the ImGui plugin panel.");
                return S_OK;
            }).Get(), nullptr);

    RECT bounds{};
    if (g_state.hasBounds) bounds = g_state.lastBounds;
    else GetClientRect(g_state.parent, &bounds);
    g_state.controller->put_Bounds(bounds);
    g_state.lastBounds = bounds;
    g_state.hasBounds = true;
    g_state.controller->put_IsVisible(FALSE);
    g_state.ready.store(true);
    g_state.documentReady.store(false);
    g_state.initializing = false;
    if (g_state.callbacks.onReady) g_state.callbacks.onReady(true, "");
    const HRESULT navigated = g_state.webview->Navigate(g_state.entryUrl.c_str());
    if (FAILED(navigated)) {
        Fail("WebView2 navigation request failed (HRESULT " +
             std::to_string(static_cast<long>(navigated)) + ").");
    }
    return S_OK;
}

HRESULT OnEnvironmentCreated(HRESULT result, ICoreWebView2Environment* environment, uint64_t generation) {
    if (generation != g_state.generation.load()) return S_OK;
    if (FAILED(result) || !environment) {
        Fail("WebView2 environment creation failed (HRESULT " +
             std::to_string(static_cast<long>(result)) +
             "). Install the Evergreen WebView2 Runtime, or set [host] ui=imgui.");
        return S_OK;
    }
    g_state.environment = environment;
    const HRESULT hr = g_state.environment->CreateCoreWebView2Controller(g_state.parent,
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [generation](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                return OnControllerCreated(result, controller, generation);
            }).Get());
    if (FAILED(hr)) {
        Fail("WebView2 controller request failed (HRESULT " +
             std::to_string(static_cast<long>(hr)) + ").");
    }
    return S_OK;
}

} // namespace

bool Initialize(HWND parent, const Config& config, Callbacks callbacks) {
    Shutdown();
    const uint64_t generation = g_state.generation.fetch_add(1) + 1;
    g_state.parent = parent;
    g_state.callbacks = std::move(callbacks);
    g_state.config = config;
    g_state.ready.store(false);
    g_state.documentReady.store(false);
    g_state.visible.store(false);
    g_state.diagnostic.clear();
    g_state.navigationUri.clear();
    g_state.initializing = false;
    g_state.hasBounds = false;
    g_state.hasFocus = false;
    g_state.wantsTextInput = false;
    g_state.lastBounds = RECT{};
    g_state.trustedPrefix = L"https://" + config.virtualHost + L"/";
    g_state.entryUrl = g_state.trustedPrefix + config.entryPath;
    if (!parent) {
        Fail("WebView2 has no parent window.");
        return false;
    }
    if (config.virtualHost.empty() || config.entryPath.empty() ||
        config.entryPath.front() == L'/' || config.entryPath.front() == L'\\' ||
        config.entryPath.find(L"..") != std::wstring::npos) {
        Fail("WebView2 panel assets configuration is invalid.");
        return false;
    }
#if defined(GAKULOADER_WEBVIEW2_TEST)
    (void)generation;
    if (g_testHooks.initialize) g_testHooks.initialize(config);
    if (g_testHooks.failInitialize) {
        Fail("WebView2 test initialization failure.");
        return false;
    }
    g_state.ready.store(true);
    g_state.documentReady.store(!g_testHooks.failDocument);
    if (g_state.callbacks.onReady) g_state.callbacks.onReady(true, "");
    if (g_state.documentReady.load() && g_state.callbacks.onDocumentReady) g_state.callbacks.onDocumentReady();
    return true;
#else
    if (!LoadLoader()) {
        Fail("WebView2 Runtime or WebView2Loader.dll is unavailable. Install the Evergreen WebView2 "
             "Runtime and keep WebView2Loader.dll beside version.dll, or set [host] ui=imgui.");
        return false;
    }
    const std::filesystem::path entry = config.assetsDir / config.entryPath;
    std::error_code ec;
    if (!std::filesystem::exists(entry, ec) || ec) {
        Fail("Web panel assets are missing at " + Narrow(entry.wstring()) +
             ". Build the plugin frontend or set [host] ui=imgui.");
        return false;
    }
    std::filesystem::create_directories(config.userDataDir, ec);
    if (ec) {
        Fail("Cannot create the WebView2 user data folder at " + Narrow(config.userDataDir.wstring()) +
             " (" + ec.message() + ").");
        return false;
    }
    g_state.initializing = true;

    auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    if (options) {
        options->put_Language(config.languageTag.c_str());
        if (config.devToolsEnabled) options->put_AdditionalBrowserArguments(L"--auto-open-devtools-for-tabs");
    }
    ComPtr<ICoreWebView2EnvironmentOptions> optionsBase;
    if (options) options.As(&optionsBase);
    const HRESULT hr = g_createEnvironment(nullptr, config.userDataDir.c_str(), optionsBase.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [generation](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
                return OnEnvironmentCreated(result, environment, generation);
            }).Get());
    if (FAILED(hr)) {
        Fail("WebView2 environment request failed (HRESULT " +
             std::to_string(static_cast<long>(hr)) + ").");
        return false;
    }
    return true;
#endif
}

void SetVisible(bool visible) {
#if defined(GAKULOADER_WEBVIEW2_TEST)
    if (g_testHooks.visible) g_testHooks.visible(visible);
    if (visible && !g_state.documentReady.load()) return;
    g_state.visible.store(visible);
    if (!visible) {
        g_state.hasFocus = false;
        g_state.wantsTextInput = false;
        if (g_state.callbacks.onFocusChanged) g_state.callbacks.onFocusChanged(false);
        if (g_state.callbacks.onTextInputChanged) g_state.callbacks.onTextInputChanged(false);
    }
    return;
#else
    if (!g_state.controller) return;
    if (visible && !g_state.documentReady.load()) return;
    if (visible && (!g_state.hasBounds || g_state.lastBounds.right <= g_state.lastBounds.left ||
        g_state.lastBounds.bottom <= g_state.lastBounds.top)) return;
    if (g_state.visible.load() == visible) return;
    g_state.controller->put_IsVisible(visible ? TRUE : FALSE);
    g_state.visible.store(visible);
    if (!visible) {
        g_state.hasFocus = false;
        g_state.wantsTextInput = false;
        if (g_state.callbacks.onFocusChanged) g_state.callbacks.onFocusChanged(false);
        if (g_state.callbacks.onTextInputChanged) g_state.callbacks.onTextInputChanged(false);
    }
#endif
}

void SetBounds(const RECT& clientBounds) {
#if defined(GAKULOADER_WEBVIEW2_TEST)
    if (g_testHooks.bounds) g_testHooks.bounds(clientBounds);
    g_state.lastBounds = clientBounds;
    g_state.hasBounds = true;
    return;
#else
    if (!g_state.controller) return;
    if (g_state.hasBounds && g_state.lastBounds.left == clientBounds.left &&
        g_state.lastBounds.top == clientBounds.top &&
        g_state.lastBounds.right == clientBounds.right &&
        g_state.lastBounds.bottom == clientBounds.bottom) return;
    g_state.controller->put_Bounds(clientBounds);
    g_state.lastBounds = clientBounds;
    g_state.hasBounds = true;
#endif
}

void SetDpiScale(float scale) {
    g_state.config.dpiScale = scale > 0.0f ? scale : 1.0f;
#if defined(GAKULOADER_WEBVIEW2_TEST)
    if (g_testHooks.dpi) g_testHooks.dpi(g_state.config.dpiScale);
#endif
    if (g_state.controller) ApplyControllerScale();
}

void NotifyParentMoved() {
    if (g_state.controller) g_state.controller->NotifyParentWindowPositionChanged();
}

void Focus() {
#if defined(GAKULOADER_WEBVIEW2_TEST)
    if (g_testHooks.focus) g_testHooks.focus();
    g_state.hasFocus = true;
    return;
#else
    if (g_state.controller && g_state.documentReady.load())
        g_state.controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
#endif
}

void PostMessageJson(const std::string& json) {
#if defined(GAKULOADER_WEBVIEW2_TEST)
    if (g_testHooks.postMessage) g_testHooks.postMessage(json);
    return;
#else
    if (!g_state.webview) return;
    if (!g_state.documentReady.load()) return;
    const int wide = MultiByteToWideChar(CP_UTF8, 0, json.c_str(), static_cast<int>(json.size()), nullptr, 0);
    if (wide <= 0) return;
    std::wstring buffer(static_cast<size_t>(wide), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, json.c_str(), static_cast<int>(json.size()), buffer.data(), wide);
    g_state.webview->PostWebMessageAsJson(buffer.c_str());
#endif
}

void Shutdown() {
    g_state.generation.fetch_add(1);
    g_state.ready.store(false);
    g_state.documentReady.store(false);
    g_state.visible.store(false);
    g_state.initializing = false;
    g_state.hasBounds = false;
    g_state.hasFocus = false;
    g_state.wantsTextInput = false;
    g_state.navigationUri.clear();
#if defined(GAKULOADER_WEBVIEW2_TEST)
    if (g_testHooks.shutdown) g_testHooks.shutdown();
#endif
    if (g_state.controller) g_state.controller->Close();
    g_state.webview.Reset();
    g_state.controller.Reset();
    g_state.environment.Reset();
    g_state.callbacks = {};
    g_state.config = {};
    g_state.trustedPrefix.clear();
    g_state.entryUrl.clear();
}

bool IsReady() { return g_state.ready.load(); }
bool IsDocumentReady() { return g_state.documentReady.load(); }
bool IsVisible() { return g_state.visible.load(); }
std::string LastDiagnostic() { return g_state.diagnostic; }

#if defined(GAKULOADER_WEBVIEW2_TEST)
void ConfigureTest(const TestHooks& hooks) { g_testHooks = hooks; }
void SimulateProcessFailedForTest(const std::string& diagnostic) { Fail(diagnostic); }
void SimulateFocusForTest(bool focused) {
    g_state.hasFocus = focused;
    g_state.wantsTextInput = focused;
    if (g_state.callbacks.onFocusChanged) g_state.callbacks.onFocusChanged(focused);
    if (g_state.callbacks.onTextInputChanged) g_state.callbacks.onTextInputChanged(focused);
}
void SimulateTabLeaveForTest(bool forward) {
    g_state.hasFocus = false;
    g_state.wantsTextInput = false;
    if (g_state.callbacks.onFocusChanged) g_state.callbacks.onFocusChanged(false);
    if (g_state.callbacks.onTextInputChanged) g_state.callbacks.onTextInputChanged(false);
    if (g_state.callbacks.onTabLeave) g_state.callbacks.onTabLeave(forward);
}
#endif

} // namespace GakuLoader::WebView2Ui
