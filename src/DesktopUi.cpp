#include "DesktopUi.hpp"
#include "DesktopDialogs.hpp"
#include "DesktopAppearance.hpp"
#include "Host.hpp"
#include "Hotkeys.hpp"
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>
#include <atomic>
#include <mutex>
#include <cstring>
#include <algorithm>
#include <string>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace GakuLoader::DesktopUi {
namespace {
constexpr wchar_t kWindowClass[] = L"GakuModLoader.DesktopControl";
constexpr UINT kShow = WM_APP + 81, kStop = WM_APP + 82, kTray = WM_APP + 83, kToggleManager = WM_APP + 84;
constexpr const char* kManagerPanel = "gakuloader.manager";
constexpr UINT kTrayShow = 1;
std::atomic_bool g_stop{true};
std::atomic_bool g_wantsTextInput{false};
std::atomic<HWND> g_window{nullptr};
HWND g_ownedWindow = nullptr; // UI worker only; also covers partial startup.
HANDLE g_worker = nullptr;
std::mutex g_lifecycle;
std::atomic<DWORD> g_uiThread{0};
ImGuiContext* g_context = nullptr;
// A plugin panel callback runs inline on this UI thread, so anything slow inside
// it freezes the whole control window. Time the dispatch and name the offender.
double NowMs() {
    static const double scale = [] {
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        return frequency.QuadPart ? 1000.0 / static_cast<double>(frequency.QuadPart) : 0.001;
    }();
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) * scale;
}
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_deviceContext = nullptr;
IDXGISwapChain* g_swapchain = nullptr;
ID3D11RenderTargetView* g_target = nullptr;
bool g_inCallback = false, g_win32Ready = false, g_dx11Ready = false;
bool g_minimized = false, g_trayAdded = false;
UINT g_taskbarCreated = 0;
UINT g_resizeWidth = 0, g_resizeHeight = 0;
DesktopHostSnapshot g_hostSnapshot;
std::string g_selectedPlugin;
std::mutex g_showRequestMutex;
std::string g_pendingPanel;
bool g_hasPendingShow{false};
const char* (*g_nativeClipboardGet)(void*) = nullptr;
void (*g_nativeClipboardSet)(void*, const char*) = nullptr;
void* g_nativeClipboardUser = nullptr;
std::string g_clipboardCache, g_clipboardPending;
uint32_t g_clipboardSequence = 0;
bool g_clipboardValid = false, g_clipboardWritePending = false;
#if defined(GAKULOADER_DESKTOP_UI_TEST)
TestHooks g_testHooks{true, nullptr};
#endif

template<class T> void Release(T*& value) { if (value) { value->Release(); value = nullptr; } }
struct ContextScope {
    ImGuiContext* previous;
    ContextScope() : previous(ImGui::GetCurrentContext()) { ImGui::SetCurrentContext(g_context); }
    ~ContextScope() { ImGui::SetCurrentContext(previous); }
};
uint32_t ClipboardSequence() {
#if defined(GAKULOADER_DESKTOP_UI_TEST)
    if (g_testHooks.clipboardSequence) return g_testHooks.clipboardSequence();
#endif
    return GetClipboardSequenceNumber();
}
const char* CachedClipboardGet(void*) {
    // The sequence query sends no clipboard-owner messages. Do not paste a
    // stale value if another app changed ownership during this CPU frame.
    if (!g_clipboardWritePending && ClipboardSequence() != g_clipboardSequence) g_clipboardValid = false;
    return g_clipboardValid ? g_clipboardCache.c_str() : "";
}
void CachedClipboardSet(void*, const char* text) {
    // ImGui callbacks run under the CPU-frame gate. Copy bytes only: a Win32
    // clipboard owner can require synchronous messages on Unity's thread.
    g_clipboardCache = text ? text : "";
    g_clipboardPending = g_clipboardCache;
    g_clipboardValid = g_clipboardWritePending = true;
}
void RefreshClipboard() {
    const uint32_t sequence = ClipboardSequence();
    if (g_clipboardWritePending || (g_clipboardValid && sequence == g_clipboardSequence)) return;
    g_clipboardValid = false; g_clipboardCache.clear();
    const char* text = g_nativeClipboardGet ? g_nativeClipboardGet(g_nativeClipboardUser) : nullptr;
    const uint32_t after = ClipboardSequence();
    if (text && after == sequence) {
        g_clipboardCache = text; g_clipboardSequence = after; g_clipboardValid = true;
    }
}
void FlushClipboard() {
    if (!g_clipboardWritePending) return;
    if (g_nativeClipboardSet) g_nativeClipboardSet(g_nativeClipboardUser, g_clipboardPending.c_str());
    g_clipboardWritePending = false;
    g_clipboardValid = false; // verify the actual OS value on next frame
}
GakuStatus Bind(const GakuImGuiAbi* abi, uint32_t size, GakuDesktopUiBinding* output) {
    if (!abi || !output) return GAKU_E_INVALID_ARGUMENT;
    if (size < sizeof(GakuDesktopUiBinding)) return GAKU_E_SIZE;
    const GakuImGuiAbi expected{IMGUI_VERSION_NUM, sizeof(ImGuiIO), sizeof(ImGuiStyle),
        sizeof(ImDrawVert), sizeof(ImDrawIdx), sizeof(ImWchar)};
    if (std::memcmp(abi, &expected, sizeof(expected)) != 0) return GAKU_E_VERSION;
    if (g_stop || GetCurrentThreadId() != g_uiThread) return GAKU_E_UNAVAILABLE;
    if (!g_inCallback || !g_context || ImGui::GetCurrentContext() != g_context) return GAKU_E_UNAVAILABLE;
    GakuDesktopUiBinding result{GAKU_SERVICE_VERSION, sizeof(result), g_context};
    ImGui::GetAllocatorFunctions(&result.alloc, &result.free, &result.allocatorUserData);
    *output = result;
    return GAKU_OK;
}
void* GetWindow() { return g_stop ? nullptr : g_window.load(std::memory_order_acquire); }
GakuStatus PostShow(const char* pluginId) {
    if (g_stop) return GAKU_E_SHUTTING_DOWN;
    const HWND window = g_window.load(std::memory_order_acquire);
    if (!window) return GAKU_E_UNAVAILABLE;
    std::lock_guard lock(g_showRequestMutex);
    const std::string previous = g_pendingPanel;
    const bool hadPrevious = g_hasPendingShow;
    g_pendingPanel = pluginId ? pluginId : "";
    g_hasPendingShow = true;
    if (PostMessageW(window, kShow, 0, 0)) return GAKU_OK;
    g_pendingPanel = previous;
    g_hasPendingShow = hadPrevious;
    return GAKU_E_UNAVAILABLE;
}
GakuStatus RequestShow(void*) { return PostShow(nullptr); }
const GakuDesktopUiService g_service{GAKU_SERVICE_VERSION, sizeof(g_service), Bind};
const GakuDesktopWindowService g_windowService{GAKU_SERVICE_VERSION, sizeof(g_windowService),
    nullptr, GetWindow, RequestShow, GAKU_DESKTOP_PRESENTATION_PLUGIN_PANEL};

GakuUiTheme g_currentTheme = GAKU_UI_THEME_DARK;
float g_currentUiScale = 1.0f;
float g_currentDpiScale = 1.0f;

GakuStatus GetAppearanceImpl(uint32_t outputSize, GakuUiAppearanceV1* output) {
    if (!output) return GAKU_E_INVALID_ARGUMENT;
    if (outputSize < sizeof(GakuUiAppearanceV1)) return GAKU_E_SIZE;
    output->apiVersion = GAKU_ABI_VERSION;
    output->structSize = sizeof(GakuUiAppearanceV1);
    output->theme = g_currentTheme;
    output->uiScale = g_currentUiScale;
    return GAKU_OK;
}
const GakuAppearanceService g_appearanceService{GAKU_SERVICE_VERSION, sizeof(g_appearanceService), GetAppearanceImpl};
void ShowControlWindow(HWND window) {
    ShowWindow(window, IsIconic(window) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(window);
}
void UpdateTray(HWND window, bool add) {
    NOTIFYICONDATAW icon{}; icon.cbSize = sizeof(icon); icon.hWnd = window; icon.uID = 1;
    icon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; icon.uCallbackMessage = kTray;
    icon.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    lstrcpynW(icon.szTip, L"GakuLoader - open desktop controls", ARRAYSIZE(icon.szTip));
    if (add) g_trayAdded = Shell_NotifyIconW(NIM_ADD, &icon) != FALSE;
    else if (g_trayAdded) { Shell_NotifyIconW(NIM_DELETE, &icon); g_trayAdded = false; }
}
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (g_taskbarCreated && message == g_taskbarCreated) {
        g_trayAdded = false;
        UpdateTray(window, true);
        if (!g_trayAdded && !IsWindowVisible(window)) ShowWindow(window, SW_SHOWMINNOACTIVE);
        return 0;
    }
    if (g_context && !g_stop) {
        ContextScope scope;
        ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam);
    }
    switch (message) {
    case WM_SIZE:
        g_minimized = wparam == SIZE_MINIMIZED;
        if (!g_minimized) { g_resizeWidth = LOWORD(lparam); g_resizeHeight = HIWORD(lparam); }
        return 0;
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize = {720, 480};
        return 0;
    case WM_DPICHANGED: {
        const auto* rectangle = reinterpret_cast<RECT*>(lparam);
        SetWindowPos(window, nullptr, rectangle->left, rectangle->top,
            rectangle->right - rectangle->left, rectangle->bottom - rectangle->top, SWP_NOZORDER | SWP_NOACTIVATE);
        g_currentDpiScale = static_cast<float>(LOWORD(wparam)) / 96.0f;
        DesktopAppearance::ApplyAppearance(g_currentTheme, g_currentUiScale, g_currentDpiScale);
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wparam & 0xFFF0) == SC_KEYMENU) return 0;
        break;
    case WM_CLOSE:
        // Closing the controls never sends a close request to the game.
        // If Explorer cannot accept a tray icon, keep a minimized taskbar entry.
        ShowWindow(window, g_trayAdded ? SW_HIDE : SW_MINIMIZE);
        return 0;
    case kShow:
        {
            std::string requested;
            bool hasRequest = false;
            { std::lock_guard lock(g_showRequestMutex);
              hasRequest = g_hasPendingShow; g_hasPendingShow = false;
              requested.swap(g_pendingPanel); }
            // Requests may coalesce while the UI is busy. The most recent
            // request wins, including host requests for Manager. Validation was
            // done before posting; selecting a copied id needs no callback gate.
            if (hasRequest) g_selectedPlugin = requested.empty() ? kManagerPanel : requested;
        }
        ShowControlWindow(window); return 0;
    case kToggleManager:
        // F1 never closes the game and never queues behind a game frame.
        if (!IsWindowVisible(window) || IsIconic(window)) {
            g_selectedPlugin = kManagerPanel;
            ShowControlWindow(window);
        } else if (g_selectedPlugin == kManagerPanel) {
            ShowWindow(window, g_trayAdded ? SW_HIDE : SW_MINIMIZE);
        } else {
            g_selectedPlugin = kManagerPanel;
            ShowControlWindow(window);
        }
        return 0;
    case kStop: return 0;
    case kTray:
        if (lparam == WM_LBUTTONUP || lparam == WM_LBUTTONDBLCLK) ShowControlWindow(window);
        else if (lparam == WM_RBUTTONUP) {
            HMENU menu = CreatePopupMenu(); AppendMenuW(menu, MF_STRING, kTrayShow, L"Open GakuLoader");
            POINT point{}; GetCursorPos(&point); SetForegroundWindow(window);
            const UINT selection = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                point.x, point.y, 0, window, nullptr);
            DestroyMenu(menu);
            if (selection == kTrayShow) ShowControlWindow(window);
        }
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
bool CreateRenderTarget() {
    ID3D11Texture2D* buffer = nullptr;
    if (FAILED(g_swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&buffer)))) return false;
    const HRESULT result = g_device->CreateRenderTargetView(buffer, nullptr, &g_target);
    Release(buffer);
    return SUCCEEDED(result);
}
bool CreatePresentation(HWND window) {
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2; desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.OutputWindow = window;
    desc.SampleDesc.Count = 1; desc.Windowed = TRUE; desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    HRESULT result = E_FAIL;
#if !defined(GAKULOADER_DESKTOP_UI_TEST)
    result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &desc, &g_swapchain, &g_device, nullptr, &g_deviceContext);
#endif
    if (FAILED(result)) result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &desc, &g_swapchain, &g_device, nullptr, &g_deviceContext);
    if (FAILED(result) || !CreateRenderTarget()) return false;
    g_context = ImGui::CreateContext();
    ContextScope scope;
    auto& io = ImGui::GetIO();
    g_nativeClipboardGet = io.GetClipboardTextFn; g_nativeClipboardSet = io.SetClipboardTextFn;
    g_nativeClipboardUser = io.ClipboardUserData;
#if defined(GAKULOADER_DESKTOP_UI_TEST)
    if (g_testHooks.clipboardGet || g_testHooks.clipboardSet) {
        g_nativeClipboardGet = g_testHooks.clipboardGet; g_nativeClipboardSet = g_testHooks.clipboardSet;
        g_nativeClipboardUser = g_testHooks.clipboardUserData;
    }
#endif
    io.GetClipboardTextFn = CachedClipboardGet; io.SetClipboardTextFn = CachedClipboardSet;
    io.ClipboardUserData = nullptr;
    ImGui::GetIO().IniFilename = nullptr; ImGui::GetIO().LogFilename = nullptr;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Use the installed system font without redistributing it.
    char fontPath[MAX_PATH]{};
    if (GetWindowsDirectoryA(fontPath, MAX_PATH) && std::strlen(fontPath) + 20 < MAX_PATH) {
        std::strcat(fontPath, "\\Fonts\\msyh.ttc");
        if (GetFileAttributesA(fontPath) != INVALID_FILE_ATTRIBUTES) {
            ImFontConfig config; config.OversampleH = config.OversampleV = 1;
            ImGui::GetIO().Fonts->TexDesiredWidth = 4096;
            ImGui::GetIO().Fonts->AddFontFromFileTTF(fontPath, 18.0f, &config,
                ImGui::GetIO().Fonts->GetGlyphRangesChineseFull());
        }
    }
    DesktopAppearance::LoadAppearanceSettings(ConfigPath(), g_currentTheme, g_currentUiScale);
    g_currentDpiScale = DesktopAppearance::GetWindowDpiScale(window);
    DesktopAppearance::ApplyAppearance(g_currentTheme, g_currentUiScale, g_currentDpiScale);
    g_win32Ready = ImGui_ImplWin32_Init(window);
    g_dx11Ready = g_win32Ready && ImGui_ImplDX11_Init(g_device, g_deviceContext);
    return g_dx11Ready;
}
void DestroyPresentation(HWND window, HINSTANCE instance) {
    if (g_context) {
        ImGuiContext* previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(g_context);
        if (g_dx11Ready) ImGui_ImplDX11_Shutdown();
        if (g_win32Ready) ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(g_context);
        ImGui::SetCurrentContext(previous == g_context ? nullptr : previous);
    }
    g_context = nullptr; g_win32Ready = g_dx11Ready = false;
    g_nativeClipboardGet = nullptr; g_nativeClipboardSet = nullptr; g_nativeClipboardUser = nullptr;
    g_clipboardCache.clear(); g_clipboardPending.clear(); g_clipboardValid = g_clipboardWritePending = false;
    if (g_deviceContext) g_deviceContext->ClearState();
    Release(g_target); Release(g_swapchain); Release(g_deviceContext); Release(g_device);
    UpdateTray(window, false);
    g_window.store(nullptr, std::memory_order_release);
    if (window) DestroyWindow(window);
    g_ownedWindow = nullptr;
    UnregisterClassW(kWindowClass, instance);
}
void DrawManagerPage(const DesktopHostSnapshot& snapshot) {
    ImGui::TextUnformatted("GakuLoader manager"); ImGui::Separator();
    ImGui::Text("%zu plugin(s) | runtime %s | %s", snapshot.plugins.size(),
        snapshot.runtimeReady ? "ready" : "waiting for IL2CPP",
        snapshot.shuttingDown ? "shutting down" : "running");
    ImGui::TextWrapped("F1 opens or hides this manager. Plugin hotkeys are registered with the loader, "
        "dispatched once per physical press on the Unity main thread, and never grabbed system-wide. "
        "A conflicting combo disables exactly the affected bindings until it is reassigned.");
    ImGui::Spacing();
    if (ImGui::BeginTable("##plugins", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Plugin"); ImGui::TableSetupColumn("Version");
        ImGui::TableSetupColumn("State"); ImGui::TableSetupColumn("Panel");
        ImGui::TableHeadersRow();
        for (const auto& plugin : snapshot.plugins) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(plugin.name.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(plugin.version.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(plugin.failed ? "failed" :
                plugin.runtimeReady ? "runtime ready" : plugin.activated ? "activated" : "not activated");
            ImGui::TableNextColumn(); ImGui::TextUnformatted(plugin.hasPanel ? "yes" : "no");
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::TextUnformatted("Hotkeys"); ImGui::Separator();
    const auto rows = Hotkeys::Snapshot();
    if (rows.empty()) {
        ImGui::TextWrapped("No plugin hotkeys registered.");
    } else if (ImGui::BeginTable("##hotkeys", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Plugin"); ImGui::TableSetupColumn("Action");
        ImGui::TableSetupColumn("Name"); ImGui::TableSetupColumn("Combo"); ImGui::TableSetupColumn("State");
        ImGui::TableHeadersRow();
        for (const auto& row : rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(row.pluginId.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(row.actionId.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(row.displayName.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(row.combo.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.unbound ? "unbound" : row.conflicted ? "conflict" : "active");
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::TextWrapped("Bindings can be reassigned in gakuloader.ini [hotkeys] as "
        "<pluginId>.<actionId>=Ctrl+M; leaving the value empty unbinds that action. "
        "Plugin enable/disable changes also take effect on the next start.");

    ImGui::Spacing();
    ImGui::TextUnformatted("Appearance"); ImGui::Separator();

    const char* themeNames[] = { "Dark", "Sapphire", "Paper & Ink" };
    int currentThemeIndex = static_cast<int>(g_currentTheme);
    if (currentThemeIndex < 0 || currentThemeIndex > 2) currentThemeIndex = 0;
    ImGui::TextUnformatted("Theme");
    const float comboWidth = (std::max)(140.0f, 180.0f * (g_currentUiScale * g_currentDpiScale));
    ImGui::SetNextItemWidth(comboWidth);
    if (ImGui::BeginCombo("##ThemeCombo", themeNames[currentThemeIndex])) {
        for (int i = 0; i < 3; ++i) {
            const bool isSelected = (currentThemeIndex == i);
            if (ImGui::Selectable(themeNames[i], isSelected)) {
                if (currentThemeIndex != i) {
                    g_currentTheme = static_cast<GakuUiTheme>(i);
                    DesktopAppearance::ApplyAppearance(g_currentTheme, g_currentUiScale, g_currentDpiScale);
                    DesktopAppearance::SaveAppearanceSettings(ConfigPath(), g_currentTheme, g_currentUiScale);
                }
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    const struct { const char* label; float scale; } scales[] = {
        { "90%", 0.90f },
        { "100%", 1.00f },
        { "110%", 1.10f },
        { "125%", 1.25f },
        { "150%", 1.50f }
    };
    int currentScaleIndex = 1;
    for (int i = 0; i < 5; ++i) {
        if (std::fabs(scales[i].scale - g_currentUiScale) < 0.01f) {
            currentScaleIndex = i;
            break;
        }
    }
    ImGui::TextUnformatted("UI Scale");
    ImGui::SetNextItemWidth(comboWidth);
    if (ImGui::BeginCombo("##ScaleCombo", scales[currentScaleIndex].label)) {
        for (int i = 0; i < 5; ++i) {
            const bool isSelected = (currentScaleIndex == i);
            if (ImGui::Selectable(scales[i].label, isSelected)) {
                if (currentScaleIndex != i) {
                    g_currentUiScale = scales[i].scale;
                    DesktopAppearance::ApplyAppearance(g_currentTheme, g_currentUiScale, g_currentDpiScale);
                    DesktopAppearance::SaveAppearanceSettings(ConfigPath(), g_currentTheme, g_currentUiScale);
                }
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}
void DrawHostShell() {
    SnapshotDesktopHost(g_hostSnapshot);
    g_wantsTextInput.store(ImGui::GetIO().WantTextInput, std::memory_order_release);
    if (g_selectedPlugin.empty()) g_selectedPlugin = kManagerPanel;
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##GakuLoaderDesktopShell", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar);
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Hide controls")) PostMessageW(g_window, WM_CLOSE, 0, 0);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Manager", "F1")) g_selectedPlugin = kManagerPanel;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Plugins")) {
            for (const auto& plugin : g_hostSnapshot.plugins) {
                if (ImGui::MenuItem(plugin.name.c_str(), nullptr, g_selectedPlugin == plugin.id)) g_selectedPlugin = plugin.id;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
    const float footer = ImGui::GetFrameHeightWithSpacing();
    const float navWidth = (std::max)(160.0f, 190.0f * (g_currentUiScale * g_currentDpiScale));
    ImGui::BeginChild("##PluginNavigation", ImVec2(navWidth, -footer), true);
    if (ImGui::Selectable("Manager", g_selectedPlugin == kManagerPanel)) g_selectedPlugin = kManagerPanel;
    ImGui::Separator();
    ImGui::TextUnformatted("Plugins");
    for (const auto& plugin : g_hostSnapshot.plugins) {
        ImGui::PushID(plugin.id.c_str());
        if (ImGui::Selectable(plugin.name.c_str(), g_selectedPlugin == plugin.id)) g_selectedPlugin = plugin.id;
        ImGui::PopID();
    }
    ImGui::EndChild(); ImGui::SameLine();
    ImGui::BeginChild("##PluginContent", ImVec2(0, -footer), true);
    if (g_selectedPlugin == kManagerPanel) {
        DrawManagerPage(g_hostSnapshot);
    } else {
        const auto selected = std::find_if(g_hostSnapshot.plugins.begin(), g_hostSnapshot.plugins.end(),
            [](const auto& plugin) { return plugin.id == g_selectedPlugin; });
        if (selected == g_hostSnapshot.plugins.end()) {
            ImGui::TextUnformatted("GakuLoader"); ImGui::Separator();
            ImGui::TextWrapped("Select the manager or a plugin from the navigation.");
        } else {
            ImGui::Text("%s  %s", selected->name.c_str(), selected->version.c_str()); ImGui::Separator();
            if (selected->failed) ImGui::TextWrapped("This plugin failed. Consult the private loader log.");
            else if (!selected->hasPanel) ImGui::TextWrapped("This plugin has no negotiated desktop control panel.");
            else {
                ImGui::PushID(selected->id.c_str());
                g_inCallback = true;
                const double panelStarted = NowMs();
                DispatchDesktopPanel(selected->id.c_str());
                const double panelElapsed = NowMs() - panelStarted;
                g_inCallback = false;
                ImGui::PopID();
                // One frame is 16.7 ms at 60 Hz; anything above that is visible
                // as a hitch. Rate-limited so a permanently slow panel reports
                // itself once every two seconds instead of every frame.
                static double lastSlowPanelReport = -1.0e9;
                if (panelElapsed >= 16.0 && panelStarted - lastSlowPanelReport >= 2000.0) {
                    lastSlowPanelReport = panelStarted;
                    LogWarn("[DesktopUI] plugin panel callback %s blocked the shell for %.1f ms",
                        selected->id.c_str(), panelElapsed);
                }
            }
        }
    }
    ImGui::EndChild();
    ImGui::Text("GakuLoader: %zu plugin(s) | Gakumas: %s", g_hostSnapshot.plugins.size(),
        g_hostSnapshot.shuttingDown ? "shutting down" : g_hostSnapshot.runtimeReady ? "runtime ready" : "waiting for IL2CPP");
    ImGui::End();
}
bool RenderFrame() {
    // The shell paints on its own thread. A gap here means the whole window
    // stopped painting — focus handover, a device reset or a blocked UI
    // thread — which is what a user experiences as "the settings froze".
    const double frameStarted = NowMs();
    static double previousFrame = 0.0;
    static double lastGapReport = -1.0e9;
    if (previousFrame > 0.0 && frameStarted - previousFrame >= 50.0 &&
        frameStarted - lastGapReport >= 1000.0) {
        lastGapReport = frameStarted;
        LogWarn("[DesktopUI] frame gap %.0f ms before painting the shell", frameStarted - previousFrame);
    }
    previousFrame = frameStarted;
    {
        ContextScope scope;
        const bool complete = TryDispatchDesktopFrame([](void*) {
#if defined(GAKULOADER_DESKTOP_UI_TEST)
            if (g_testHooks.beginCpuFrame) g_testHooks.beginCpuFrame();
#endif
            ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
            DrawHostShell();
            ImGui::Render();
        }, nullptr);
        if (!complete) return true; // retain the last complete image; no clear/present
        FlushClipboard(); // CPU gate is released before the native write
        if (g_resizeWidth && g_resizeHeight) {
            Release(g_target);
            const HRESULT result = g_swapchain->ResizeBuffers(0, g_resizeWidth, g_resizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeWidth = g_resizeHeight = 0;
            if (FAILED(result) || !CreateRenderTarget()) return false;
        }
        const ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        const float clear[4]{bg.x, bg.y, bg.z, 1.0f};
        g_deviceContext->OMSetRenderTargets(1, &g_target, nullptr);
        g_deviceContext->ClearRenderTargetView(g_target, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
#if defined(GAKULOADER_DESKTOP_UI_TEST)
        if (g_testHooks.inspectFrame) g_testHooks.inspectFrame(g_device, g_deviceContext, g_swapchain);
#endif
    }
    const HRESULT presented = g_swapchain->Present(1, 0);
#if defined(GAKULOADER_DESKTOP_UI_TEST)
    if (SUCCEEDED(presented) && g_testHooks.presented) g_testHooks.presented();
#endif
    return presented != DXGI_ERROR_DEVICE_REMOVED && presented != DXGI_ERROR_DEVICE_RESET;
}
DWORD WorkerBody() {
    g_uiThread = GetCurrentThreadId();
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW cls{}; cls.cbSize = sizeof(cls); cls.hInstance = instance; cls.lpfnWndProc = WindowProc;
    cls.lpszClassName = kWindowClass; cls.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    cls.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    if (!RegisterClassExW(&cls)) {
        LogWarn("[DesktopUI] independent window registration failed Win32=%lu", GetLastError());
        g_stop = true; DesktopDialogs::Shutdown(); return 0;
    }
    RECT area{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
    const int width = std::min(1120L, area.right - area.left), height = std::min(820L, area.bottom - area.top);
    HWND window = CreateWindowExW(WS_EX_APPWINDOW, kWindowClass, L"GakuLoader", WS_OVERLAPPEDWINDOW,
        area.left + 30, area.top + 30, width, height, nullptr, nullptr, instance, nullptr);
    g_ownedWindow = window;
    if (!window || !CreatePresentation(window)) {
        LogWarn("[DesktopUI] independent presentation unavailable; game rendering is untouched");
        g_stop = true; DesktopDialogs::Shutdown(); DestroyPresentation(window, instance); return 0;
    }
    g_window.store(window, std::memory_order_release); UpdateTray(window, true);
#if defined(GAKULOADER_DESKTOP_UI_TEST)
    if (!g_testHooks.startHidden) ShowWindow(window, SW_SHOW);
#else
    ShowWindow(window, SW_SHOW);
#endif
    LogInfo("[DesktopUI] independent Win32/D3D11 control window ready hwnd=%p thread=%lu; no game presentation hooks", window, g_uiThread.load());
    while (!g_stop) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) { g_stop = true; break; }
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        if (g_stop) break;
        bool render = !g_minimized;
#if !defined(GAKULOADER_DESKTOP_UI_TEST)
        render = render && IsWindowVisible(window);
#endif
        // Read before the frame, so the first paste sees the current clipboard.
        // Default ImGui/Win32 callbacks may block here, outside the callback gate.
        if (render) RefreshClipboard();
        if (render && !RenderFrame()) {
            LogWarn("[DesktopUI] control-window device lost; desktop stops, game rendering is untouched");
            break;
        }
        FlushClipboard();
        // Modal dialogs run with every ImGui/callback scope unwound, including
        // when the controls were minimized after submitting the request.
        DesktopDialogs::Pump(window);
        MsgWaitForMultipleObjectsEx(0, nullptr, render ? 16 : 100, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
    g_stop = true;
    DesktopDialogs::Shutdown();
    DestroyPresentation(window, instance);
    LogInfo("[DesktopUI] independent window stopped and owned presentation released");
    return 0;
}
DWORD WINAPI Worker(void*) {
    try { return WorkerBody(); }
    catch (...) {
        // A desktop allocation or plugin-owned C++ exception must not escape
        // CreateThread and terminate the game process.
        g_stop = true;
        g_inCallback = false;
        DesktopDialogs::Shutdown();
        try { LogWarn("[DesktopUI] UI worker exception; stopping independent presentation"); } catch (...) {}
        try { DestroyPresentation(g_ownedWindow, GetModuleHandleW(nullptr)); } catch (...) {}
        return 0;
    }
}
}
bool Initialize() {
    std::lock_guard lock(g_lifecycle);
    if (!g_stop.exchange(false)) return true;
    if (g_worker) { g_stop = true; DesktopDialogs::Shutdown(); return false; } // no restart while a failed/old worker is retained
    g_worker = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    if (!g_worker) { g_stop = true; DesktopDialogs::Shutdown(); return false; }
    return true;
}
void Shutdown() {
    std::lock_guard lock(g_lifecycle);
    g_stop = true;
    DesktopDialogs::Shutdown();
    const HWND window = g_window.load(std::memory_order_acquire);
    if (window) PostMessageW(window, kStop, 0, 0);
    if (!g_worker) return;
    if (GetCurrentThreadId() == g_uiThread) return;
    const DWORD waited = WaitForSingleObject(g_worker, 5000);
    if (waited == WAIT_OBJECT_0) { CloseHandle(g_worker); g_worker = nullptr; }
    else LogWarn("[DesktopUI] UI worker did not stop within 5s; resident resources retained until process exit");
}
const GakuDesktopUiService* GetService() { return &g_service; }
const GakuDesktopWindowService* GetWindowService() { return &g_windowService; }
const GakuAppearanceService* GetAppearanceService() { return &g_appearanceService; }
GakuStatus RequestShowPanel(const char* pluginId) {
    if (!pluginId || !pluginId[0]) return GAKU_E_INVALID_ARGUMENT;
    if (g_stop) return GAKU_E_SHUTTING_DOWN;
    DesktopHostSnapshot snapshot;
    if (!SnapshotDesktopHost(snapshot)) return GAKU_E_UNAVAILABLE;
    const auto panel = std::find_if(snapshot.plugins.begin(), snapshot.plugins.end(),
        [&](const auto& plugin) { return plugin.id == pluginId && plugin.activated &&
            plugin.hasPanel && !plugin.failed; });
    if (panel == snapshot.plugins.end()) return GAKU_E_UNAVAILABLE;
    return PostShow(panel->id.c_str());
}
void ToggleManager() {
    if (g_stop) return;
    const HWND window = g_window.load(std::memory_order_acquire);
    if (window) PostMessageW(window, kToggleManager, 0, 0);
}
bool WantsTextInput() { return g_wantsTextInput.load(std::memory_order_acquire); }
#if defined(GAKULOADER_DESKTOP_UI_TEST)
void ConfigureTest(const TestHooks& hooks) { g_testHooks = hooks; }
#endif
}
