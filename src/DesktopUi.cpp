// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

#include "DesktopUi.hpp"
#include "DesktopDialogs.hpp"
#include "DesktopAppearance.hpp"
#include "DesktopFileDrop.hpp"
#include "Host.hpp"
#include "Hotkeys.hpp"
#include "DesktopFonts.hpp"
#include "ManagerStrings.hpp"
#include <windows.h>
#include <shellapi.h>
#include <ole2.h>
#include <shlobj.h>
#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>
#include <atomic>
#include <mutex>
#include <cstring>
#include <new>
#include <algorithm>
#include <string>
#include <functional>
#include <cmath>
#include <cstdio>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace GakuLoader::DesktopUi {
namespace {
constexpr wchar_t kWindowClass[] = L"GakuModLoader.DesktopControl";
constexpr UINT kShow = WM_APP + 81, kStop = WM_APP + 82, kTray = WM_APP + 83, kToggleManager = WM_APP + 84;
constexpr const char* kManagerPanel = "gakuloader.manager";
constexpr UINT kTrayShow = 1;
// Shell HDROP transport uses this system message to copy its global memory.
// It is not named in the public Windows SDK headers.
constexpr UINT kCopyGlobalData = 0x0049;
void AllowShellFileDrops(HWND window) {
    // Explorer normally runs at medium integrity while DMM may launch the game
    // elevated. DragAcceptFiles alone cannot pass that UIPI boundary. Limit the
    // exception to the independent control HWND and the two Shell drop messages.
    for (const UINT message : {static_cast<UINT>(WM_DROPFILES), kCopyGlobalData}) {
        CHANGEFILTERSTRUCT filter{sizeof(filter)};
        if (!ChangeWindowMessageFilterEx(window, message, MSGFLT_ALLOW, &filter)) {
            LogWarn("[DesktopUI] file-drop UIPI filter failed hwnd=%p message=0x%04x error=%lu",
                window, message, GetLastError());
        } else {
            LogInfo("[DesktopUI] file-drop UIPI filter allowed hwnd=%p message=0x%04x status=%lu",
                window, message, filter.ExtStatus);
        }
    }
}
// UIPI keeps a host that runs above Explorer's integrity level from receiving
// any IDropTarget call, and ChangeWindowMessageFilterEx cannot lift it (it only
// whitelists WM_DROPFILES/WM_COPYGLOBALDATA). Probe once so the log states which
// transport is actually live instead of leaving a silent, permanently dark
// hover state.
struct HostIntegrity {
    bool known = false;
    bool elevated = false;
    uint32_t rid = 0;
};
HostIntegrity ReadHostIntegrity() {
    HostIntegrity result;
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return result;
    DWORD elevated = 0, written = 0;
    if (GetTokenInformation(token, TokenElevation, &elevated, sizeof(elevated), &written))
        result.elevated = elevated != 0;
    alignas(TOKEN_MANDATORY_LABEL) unsigned char buffer[sizeof(TOKEN_MANDATORY_LABEL) + SECURITY_MAX_SID_SIZE]{};
    if (GetTokenInformation(token, TokenIntegrityLevel, buffer, sizeof(buffer), &written)) {
        const auto* label = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(buffer);
        if (label->Label.Sid) {
            const DWORD count = *GetSidSubAuthorityCount(label->Label.Sid);
            if (count) {
                result.rid = *GetSidSubAuthority(label->Label.Sid, count - 1);
                // The elevation query above writes to the same counter, so a
                // successful TokenElevation must not be mistaken for a parsed
                // integrity SID: the RID decides whether OLE may be armed.
                result.known = true;
            }
        }
    }
    CloseHandle(token);
    return result;
}
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
DesktopFileDrop::Mailbox g_fileDrop;
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
// ---------------------------------------------------------------- file drop
// Two transports feed the same mailbox: the legacy WM_DROPFILES message and an
// OLE IDropTarget on this window. OLE is what makes hover feedback possible at
// all (the shell only posts WM_DROPFILES after the button is released) and its
// points are unambiguously screen pixels, converted exactly once here.
struct DropPaths {
    std::vector<std::string> paths;
    size_t bytes = 0;
    UINT count = 0;
    const char* failure = nullptr;
    UINT failureIndex = 0;
};
std::string PackPaths(const std::vector<std::string>& paths) {
    std::string packed;
    for (const auto& path : paths) { packed.append(path); packed.push_back('\0'); }
    return packed;
}
void CollectDropPaths(HDROP drop, DropPaths& out) {
    out.count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    if (!out.count) { out.failure = "empty-batch"; return; }
    if (out.count > GAKU_FILE_DROP_MAX_FILES) { out.failure = "too-many-files"; return; }
    for (UINT i = 0; i < out.count; ++i) {
        const UINT length = DragQueryFileW(drop, i, nullptr, 0);
        if (!length || length > 32767) {
            out.failure = "invalid-path-length"; out.failureIndex = i; return;
        }
        std::wstring wide(static_cast<size_t>(length) + 1, L'\0');
        if (DragQueryFileW(drop, i, wide.data(), length + 1) != length) {
            out.failure = "path-copy-failed"; out.failureIndex = i; return;
        }
        const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
            static_cast<int>(length), nullptr, 0, nullptr, nullptr);
        if (bytes <= 0) { out.failure = "invalid-unicode"; out.failureIndex = i; return; }
        if (out.bytes + static_cast<size_t>(bytes) + 1 > GAKU_FILE_DROP_MAX_BYTES) {
            out.failure = "payload-too-large"; out.failureIndex = i; return;
        }
        std::string path(static_cast<size_t>(bytes), '\0');
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
            static_cast<int>(length), path.data(), bytes, nullptr, nullptr)) {
            out.failure = "utf8-copy-failed"; out.failureIndex = i; return;
        }
        out.bytes += static_cast<size_t>(bytes) + 1;
        out.paths.push_back(std::move(path));
    }
}
bool ReadDataObjectPaths(IDataObject* data, DropPaths& out) {
    if (!data) { out.failure = "no-data-object"; return false; }
    FORMATETC format{static_cast<CLIPFORMAT>(CF_HDROP), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium{};
    if (FAILED(data->GetData(&format, &medium))) { out.failure = "no-hdrop"; return false; }
    if (medium.tymed == TYMED_HGLOBAL && medium.hGlobal)
        CollectDropPaths(static_cast<HDROP>(medium.hGlobal), out);
    else out.failure = "unsupported-medium";
    ReleaseStgMedium(&medium);
    return out.failure == nullptr;
}
// Shared by both transports so the three-stage log line (received -> result /
// not-consumed -> consumed) stays identical to the analysis operators rely on.
bool FinishDrop(HWND window, const char* source, const DropPaths& drop, POINT point) {
    LogInfo("[DesktopUI][FileDrop] received source=%s hwnd=%p owner=%s point=%ld,%ld files=%u",
        source, window, g_selectedPlugin.c_str(), point.x, point.y, drop.count);
    RECT client{};
    if (!GetClientRect(window, &client) || !PtInRect(&client, point)) {
        LogWarn("[DesktopUI][FileDrop] rejected reason=outside-client source=%s point=%ld,%ld",
            source, point.x, point.y);
        return false;
    }
    const auto result = g_fileDrop.AcceptDetailed(g_selectedPlugin.c_str(),
        static_cast<float>(point.x), static_cast<float>(point.y), drop.paths);
    const auto& target = g_fileDrop.Target();
    LogInfo("[DesktopUI][FileDrop] result=%s source=%s selected=%s hasTarget=%d targetOwner=%s target=%.1f,%.1f,%.1f,%.1f bytes=%zu",
        DesktopFileDrop::AcceptResultName(result), source, g_selectedPlugin.c_str(),
        g_fileDrop.TargetOwner()[0] != '\0' ? 1 : 0, g_fileDrop.TargetOwner(),
        target.left, target.top, target.right, target.bottom, drop.bytes);
    if (result != DesktopFileDrop::AcceptResult::Accepted) {
        // Event-level visibility for a drop delivered here but not consumed by
        // the selected plugin's target. No file paths, no per-frame logging.
        LogWarn("[DesktopUI][FileDrop] not-consumed reason=%s source=%s selected=%s targetOwner=%s files=%u",
            DesktopFileDrop::AcceptResultName(result), source, g_selectedPlugin.c_str(),
            g_fileDrop.TargetOwner(), drop.count);
        return false;
    }
    return true;
}
// OLE drop target: hover state for plugins, plus the drop itself. Every method
// runs on this window's UI thread, the same thread that owns the mailbox.
class ShellDropTarget final : public IDropTarget {
    LONG refs_ = 1;
    HWND window_ = nullptr;
    bool inside_ = false;
public:
    explicit ShellDropTarget(HWND window) : window_(window) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        if (id == IID_IUnknown || id == IID_IDropTarget) {
            *out = static_cast<IDropTarget*>(this); AddRef(); return S_OK;
        }
        *out = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&refs_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG remaining = InterlockedDecrement(&refs_);
        if (!remaining) delete this;
        return static_cast<ULONG>(remaining);
    }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD, POINTL point, DWORD* effect) override {
        if (!effect) return E_POINTER;
        *effect = DROPEFFECT_NONE;
        inside_ = false;
        DropPaths drop;
        if (!ReadDataObjectPaths(data, drop)) {
            g_fileDrop.EndDrag();
            LogWarn("[DesktopUI][FileDrop] drag-enter rejected reason=%s index=%u",
                drop.failure ? drop.failure : "unknown", drop.failureIndex);
            return S_OK;
        }
        POINT client{point.x, point.y};
        const bool converted = ScreenToClient(window_, &client) != FALSE;
        g_fileDrop.BeginDrag(PackPaths(drop.paths), drop.count);
        if (converted) g_fileDrop.UpdateDrag(static_cast<float>(client.x), static_cast<float>(client.y));
        LogInfo("[DesktopUI][FileDrop] drag-enter files=%u bytes=%zu screen=%ld,%ld client=%ld,%ld owner=%s targetOwner=%s",
            drop.count, drop.bytes, static_cast<long>(point.x), static_cast<long>(point.y),
            client.x, client.y, g_selectedPlugin.c_str(), g_fileDrop.TargetOwner());
        *effect = g_fileDrop.TargetOwner()[0] ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL point, DWORD* effect) override {
        if (!effect) return E_POINTER;
        POINT client{point.x, point.y};
        if (ScreenToClient(window_, &client))
            g_fileDrop.UpdateDrag(static_cast<float>(client.x), static_cast<float>(client.y));
        const bool inside = g_fileDrop.DragInsideTarget();
        if (inside != inside_ && g_fileDrop.DragActive()) {
            inside_ = inside;
            // State flip only; DragOver fires continuously while the shell drag runs.
            LogInfo("[DesktopUI][FileDrop] drag-hover inside-target=%d point=%ld,%ld",
                inside ? 1 : 0, client.x, client.y);
        }
        *effect = g_fileDrop.TargetOwner()[0] ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override {
        if (g_fileDrop.DragActive()) LogInfo("[DesktopUI][FileDrop] drag-leave files=%u", g_fileDrop.DragCount());
        g_fileDrop.EndDrag(); inside_ = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD, POINTL point, DWORD* effect) override {
        if (!effect) return E_POINTER;
        *effect = DROPEFFECT_NONE;
        DropPaths drop;
        const bool read = ReadDataObjectPaths(data, drop);
        POINT client{point.x, point.y};
        const bool converted = ScreenToClient(window_, &client) != FALSE;
        if (read && converted) {
            // The effect reports whether the payload was taken, so a source that
            // asked for a move never deletes a file this target ignored.
            if (FinishDrop(window_, "ole-drop", drop, client)) *effect = DROPEFFECT_COPY;
        } else {
            LogWarn("[DesktopUI][FileDrop] rejected reason=%s index=%u source=ole-drop point=%ld,%ld",
                read ? "coordinate-conversion" : (drop.failure ? drop.failure : "unknown"),
                drop.failureIndex, static_cast<long>(point.x), static_cast<long>(point.y));
        }
        g_fileDrop.EndDrag(); inside_ = false;
        return S_OK;
    }
};
bool g_oleReady = false;
ShellDropTarget* g_dropTarget = nullptr;
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
float g_windowDpiScale = 1.0f; // actual monitor DPI, retained even if an atlas request is rejected
bool g_fontsDirty = false, g_appearanceDirty = false, g_saveAppearance = false, g_scaleLimited = false;
float g_appliedUiScale = 1.0f, g_appliedDpiScale = 1.0f;
std::atomic_uint32_t g_publishedTheme{GAKU_UI_THEME_DARK};
std::atomic<float> g_publishedScale{1.0f};
std::function<void()> g_managerAction;
bool g_saveFailed = false;
bool g_editorOpen = false, g_captureKeys = false, g_editorRequested = false;
std::string g_editPlugin, g_editAction;
char g_editCombo[128]{};
bool g_captureDown[256]{};
bool g_invalidCombo = false;

GakuStatus GetAppearanceImpl(uint32_t outputSize, GakuUiAppearanceV1* output) {
    if (!output) return GAKU_E_INVALID_ARGUMENT;
    if (outputSize < sizeof(GakuUiAppearanceV1)) return GAKU_E_SIZE;
    output->apiVersion = GAKU_ABI_VERSION;
    output->structSize = sizeof(GakuUiAppearanceV1);
    output->theme = static_cast<GakuUiTheme>(g_publishedTheme.load());
    output->uiScale = g_publishedScale.load();
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
    lstrcpynW(icon.szTip, ManagerStrings::OpenControls(), ARRAYSIZE(icon.szTip));
    if (add) {
        const bool placed = Shell_NotifyIconW(g_trayAdded ? NIM_MODIFY : NIM_ADD, &icon) != FALSE;
        // The return value used to be discarded, so a shell that refused the icon
        // left a window that closes to "hidden" with no way back. One line, once.
        if (!placed) {
            static std::atomic_bool warnedFailure{false};
            if (!warnedFailure.exchange(true)) {
                LogWarn("[DesktopUI] tray icon rejected Win32=%lu; the panel keeps a taskbar entry",
                    GetLastError());
            }
        }
        g_trayAdded = placed;
    }
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
        g_fileDrop.Clear();
        g_minimized = wparam == SIZE_MINIMIZED;
        if (g_minimized) { g_captureKeys = false; Hotkeys::SetCaptureActive(false); }
        if (!g_minimized) { g_resizeWidth = LOWORD(lparam); g_resizeHeight = HIWORD(lparam); }
        return 0;
    case WM_KILLFOCUS:
        g_captureKeys = false; Hotkeys::SetCaptureActive(false);
        break;
    case WM_ACTIVATEAPP:
        if (!wparam) { g_captureKeys = false; Hotkeys::SetCaptureActive(false); }
        break;
    case WM_SHOWWINDOW:
        if (!wparam) { g_captureKeys = false; Hotkeys::SetCaptureActive(false); }
        break;
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize = {720, 480};
        return 0;
    case WM_DPICHANGED: {
        g_fileDrop.Clear();
        const auto* rectangle = reinterpret_cast<RECT*>(lparam);
        SetWindowPos(window, nullptr, rectangle->left, rectangle->top,
            rectangle->right - rectangle->left, rectangle->bottom - rectangle->top, SWP_NOZORDER | SWP_NOACTIVATE);
        g_currentDpiScale = g_windowDpiScale = static_cast<float>(LOWORD(wparam)) / 96.0f;
        g_fontsDirty = g_appearanceDirty = true;
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wparam & 0xFFF0) == SC_KEYMENU) return 0;
        break;
    case WM_CLOSE:
        g_fileDrop.Clear();
        g_captureKeys = g_editorOpen = false; Hotkeys::SetCaptureActive(false);
        // Closing the controls never sends a close request to the game.
        // If Explorer cannot accept a tray icon, keep a minimized taskbar entry.
        ShowWindow(window, g_trayAdded ? SW_HIDE : SW_MINIMIZE);
        return 0;
    case WM_DROPFILES: {
        const HDROP drop = reinterpret_cast<HDROP>(wparam);
        struct DragFinishScope { HDROP value; ~DragFinishScope() { DragFinish(value); } } finish{drop};
        POINT reported{}; POINT point{};
        // Documented contract: fNC clear means the point is already client
        // pixels, fNC set means screen pixels to convert.
        const bool reportedClient = DragQueryPoint(drop, &reported) != FALSE;
        point = reported;
        bool resolved = reportedClient;
        if (!resolved) resolved = ScreenToClient(window, &point) != FALSE;
        if (!resolved) {
            LogWarn("[DesktopUI][FileDrop] rejected reason=coordinate-conversion error=%lu", GetLastError());
            return 0;
        }
        const POINT documented = point;
        // Real Explorer also sets fNC for client-area drops, so the documented
        // conversion subtracts the client origin a second time and moves the hit
        // point about one title bar up - which is why the drop region felt shifted
        // down. The physical cursor at release time cannot be ambiguous, so trust
        // it whenever it lands inside our client area and keep the documented
        // mapping for synthetic drops (headless tests post WM_DROPFILES with no
        // pointer over the window).
        POINT cursor{};
        const bool haveCursor = GetCursorPos(&cursor) != FALSE && ScreenToClient(window, &cursor) != FALSE;
        bool usedCursor = false;
#if !defined(GAKULOADER_DESKTOP_UI_TEST)
        if (haveCursor) {
            RECT client{};
            if (GetClientRect(window, &client) && cursor.x >= client.left && cursor.x < client.right &&
                cursor.y >= client.top && cursor.y < client.bottom) {
                point = cursor;
                usedCursor = true;
            }
        }
#endif
        LogInfo("[DesktopUI][FileDrop] wm-dropfiles hwnd=%p reported-client=%d reported=%ld,%ld documented=%ld,%ld cursor-client=%ld,%ld source=%s point=%ld,%ld",
            window, reportedClient ? 1 : 0, reported.x, reported.y, documented.x, documented.y,
            haveCursor ? cursor.x : -1L, haveCursor ? cursor.y : -1L,
            usedCursor ? "cursor" : "reported", point.x, point.y);
        DropPaths paths;
        CollectDropPaths(drop, paths);
        if (paths.failure) {
            LogWarn("[DesktopUI][FileDrop] rejected reason=%s index=%u source=wm-dropfiles",
                paths.failure, paths.failureIndex);
            return 0;
        }
        FinishDrop(window, "wm-dropfiles", paths, point);
        return 0;
    }
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
            g_fileDrop.Clear();
        }
        ShowControlWindow(window); return 0;
    case kToggleManager:
        g_fileDrop.Clear();
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
            HMENU menu = CreatePopupMenu(); AppendMenuW(menu, MF_STRING, kTrayShow, ManagerStrings::OpenControls());
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
    DesktopAppearance::LoadAppearanceSettings(ConfigPath(), g_currentTheme, g_currentUiScale);
    g_currentDpiScale = g_windowDpiScale = DesktopAppearance::GetWindowDpiScale(window);
    if (!DesktopFonts::BuildFonts(g_currentUiScale, g_currentDpiScale).ready) {
        g_currentUiScale = g_currentDpiScale = 1.0f; g_scaleLimited = true;
        if (!DesktopFonts::BuildFonts(1.0f, 1.0f).ready) return false;
    }
    DesktopAppearance::ApplyAppearance(g_currentTheme, g_currentUiScale, g_currentDpiScale);
    g_appliedUiScale = g_currentUiScale; g_appliedDpiScale = g_currentDpiScale;
    g_publishedTheme = g_currentTheme; g_publishedScale = g_currentUiScale;
    g_win32Ready = ImGui_ImplWin32_Init(window);
    g_dx11Ready = g_win32Ready && ImGui_ImplDX11_Init(g_device, g_deviceContext);
    return g_dx11Ready;
}
void DestroyPresentation(HWND window, HINSTANCE instance) {
    g_fileDrop.Clear();
    if (g_dropTarget) {
        if (window) RevokeDragDrop(window);
        g_dropTarget->Release(); // our own reference; RegisterDragDrop held another
        g_dropTarget = nullptr;
    }
    if (window) DragAcceptFiles(window, FALSE);
    g_captureKeys = g_editorOpen = false; Hotkeys::SetCaptureActive(false);
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
    // Same thread that called OleInitialize owns the uninitialize, and every
    // drop target reference is already gone above.
    if (g_oleReady) { OleUninitialize(); g_oleReady = false; }
}
using Key = ManagerStrings::Key;
const char* T(Key key) { return ManagerStrings::Text(key); }
std::string Label(Key key, const char* id) { return std::string(T(key)) + "###" + id; }
void InspectUiItem(const char* key, const char* value = "") {
#if defined(GAKULOADER_DESKTOP_UI_TEST)
    if (g_testHooks.inspectUiItem) {
        const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        g_testHooks.inspectUiItem(key, value, a.x, a.y, b.x, b.y);
    }
#else
    (void)key; (void)value;
#endif
}
bool ManagerTab(Key key, const char* id) {
    const bool open = ImGui::BeginTabItem(Label(key, id).c_str());
    InspectUiItem((std::string("tab.") + id).c_str(), open ? "selected" : "");
    return open;
}
void DrawSpinner() {
    const float radius = ImGui::GetFontSize() * 0.32f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 center(p.x + radius + 2, p.y + ImGui::GetTextLineHeight() * 0.5f);
    auto* draw = ImGui::GetWindowDrawList();
    const float phase = static_cast<float>(ImGui::GetTime() * 5.0);
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_CheckMark);
    draw->PathClear();
    for (int i = 0; i <= 24; ++i) {
        const float angle = phase + static_cast<float>(i) / 24.0f * 4.8f;
        draw->PathLineTo(ImVec2(center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius));
    }
    draw->PathStroke(color, false, (std::max)(1.5f, ImGui::GetFontSize() / 10.0f));
    ImGui::Dummy(ImVec2(radius * 2 + 4, ImGui::GetTextLineHeight()));
}
void DrawPluginList(const DesktopHostSnapshot& snapshot) {
    ImGui::TextWrapped("%s", T(Key::SummaryHelp));
    ImGui::Text("%s: %zu | %s: %s | %s", T(Key::Count), snapshot.plugins.size(), T(Key::Runtime),
        T(snapshot.runtimeReady ? Key::Ready : Key::Waiting), T(snapshot.shuttingDown ? Key::Stopping : Key::Running));
    if (ImGui::BeginTable("##plugins", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX)) {
        ImGui::TableSetupColumn(T(Key::Plugin)); ImGui::TableSetupColumn(T(Key::Version));
        ImGui::TableSetupColumn(T(Key::State)); ImGui::TableSetupColumn(T(Key::Panel));
        ImGui::TableHeadersRow();
        for (const auto& plugin : snapshot.plugins) {
            ImGui::PushID(plugin.id.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(plugin.name.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", plugin.id.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(ManagerStrings::FormatVersion(plugin.version).c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(T(plugin.failed ? Key::Failed :
                plugin.runtimeReady ? Key::Ready : plugin.activated ? Key::Activated : Key::Inactive));
            if (plugin.failed || !plugin.runtimeReady) {
                ImGui::SameLine(); DrawSpinner();
                InspectUiItem(("spinner." + plugin.id).c_str());
            }
            ImGui::TableNextColumn(); ImGui::TextUnformatted(T(plugin.hasPanel ? Key::Yes : Key::No));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}
void QueueAppearanceSave() {
    g_saveAppearance = true;
}
void DrawAppearancePage() {
    if (g_scaleLimited) ImGui::TextWrapped("%s", T(Key::ScaleLimit));
    const Key themes[] = {Key::Dark, Key::Sapphire, Key::Paper, Key::AppleLight, Key::AppleDark};
    const int rawThemeIndex = static_cast<int>(g_currentTheme);
    const int themeIndex = rawThemeIndex >= 0 && rawThemeIndex < 5 ? rawThemeIndex : 0;
    const float width = 230.0f * g_currentUiScale * g_currentDpiScale;
    ImGui::TextUnformatted(T(Key::Theme)); ImGui::SetNextItemWidth(width);
    const bool themeOpen = ImGui::BeginCombo("##ThemeCombo", T(themes[themeIndex]));
    InspectUiItem("combo.Theme");
    if (themeOpen) {
        for (int i = 0; i < 5; ++i) {
            if (ImGui::Selectable(T(themes[i]), themeIndex == i)) {
                g_currentTheme = static_cast<GakuUiTheme>(i); g_appearanceDirty = true;
                QueueAppearanceSave();
            }
            InspectUiItem(("theme." + std::to_string(i)).c_str());
        }
        ImGui::EndCombo();
    }
    ImGui::TextUnformatted(T(Key::Scale)); ImGui::SetNextItemWidth(width);
    char scaleLabel[24]{}; std::snprintf(scaleLabel, sizeof(scaleLabel), "%d%%", static_cast<int>(std::round(g_currentUiScale * 100)));
    const bool scaleOpen = ImGui::BeginCombo("##ScaleCombo", scaleLabel);
    InspectUiItem("combo.Scale", scaleLabel);
    if (scaleOpen) {
        const int percentages[] = {50, 75, 90, 100, 110, 125, 150, 175, 200, 250, 300};
        for (int percent : percentages) {
            char label[24]{}; std::snprintf(label, sizeof(label), "%d%%", percent);
            if (ImGui::Selectable(label, std::fabs(g_currentUiScale * 100 - percent) < 0.5f)) {
                g_currentUiScale = percent / 100.0f; g_currentDpiScale = g_windowDpiScale;
                g_fontsDirty = g_appearanceDirty = true;
                QueueAppearanceSave();
            }
            InspectUiItem(("scale." + std::to_string(percent)).c_str());
        }
        ImGui::EndCombo();
    }
    ImGui::TextUnformatted(T(Key::Languages)); ImGui::SetNextItemWidth(width);
    const auto language = LanguageSettings::Current();
    const bool languageOpen = ImGui::BeginCombo("##LanguageCombo", LanguageSettings::kNames[language]);
    InspectUiItem("combo.Language");
    if (languageOpen) {
        for (uint32_t i = 0; i < 4; ++i) {
            if (ImGui::Selectable(LanguageSettings::kNames[i], i == language))
                g_managerAction = [i] {
                    g_saveFailed = !LanguageSettings::Save(ConfigPath(), i);
                    if (!g_saveFailed) UpdateTray(g_window.load(), true);
                };
            InspectUiItem(("language." + std::to_string(i)).c_str());
        }
        ImGui::EndCombo();
    }
    ImGui::TextWrapped("%s", T(Key::LanguageHelp));
}
void BeginHotkeyEditor(const Hotkeys::Row& row) {
    g_editPlugin = row.pluginId; g_editAction = row.actionId;
    std::snprintf(g_editCombo, sizeof(g_editCombo), "%s", row.unbound ? "" : row.combo.c_str());
    g_invalidCombo = g_captureKeys = false; g_editorOpen = true;
    Hotkeys::SetCaptureActive(true);
    g_editorRequested = true;
}
void CaptureHotkey() {
    if (!g_captureKeys) return;
    if (GetForegroundWindow() != g_window.load()) { g_captureKeys = false; return; }
    uint32_t modifiers = 0;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) modifiers |= GAKU_HOTKEY_MOD_CTRL;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) modifiers |= GAKU_HOTKEY_MOD_SHIFT;
    if (GetAsyncKeyState(VK_MENU) & 0x8000) modifiers |= GAKU_HOTKEY_MOD_ALT;
    if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) modifiers |= GAKU_HOTKEY_MOD_WIN;
    for (uint32_t key = 8; key < 256; ++key) {
        const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
        const bool edge = down && !g_captureDown[key]; g_captureDown[key] = down;
        if (!edge || key == VK_CONTROL || key == VK_SHIFT || key == VK_MENU ||
            (key >= VK_LSHIFT && key <= VK_RMENU) || key == VK_LWIN || key == VK_RWIN) continue;
        if (key == VK_ESCAPE) { g_captureKeys = false; return; }
        const auto combo = Hotkeys::ComboName(key, modifiers);
        uint32_t parsedKey = 0, parsedMods = 0;
        if (!Hotkeys::ParseCombo(std::wstring(combo.begin(), combo.end()), parsedKey, parsedMods)) continue;
        std::snprintf(g_editCombo, sizeof(g_editCombo), "%s", combo.c_str());
        g_invalidCombo = key == Hotkeys::kManagerKey; g_captureKeys = false; return;
    }
}
void DrawHotkeyEditor() {
    const auto title = Label(Key::Capture, "HotkeyEditor");
    if (g_editorRequested) { ImGui::OpenPopup(title.c_str()); g_editorRequested = false; }
    if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    if (!g_editorOpen) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
    Hotkeys::SetCaptureActive(GetForegroundWindow() == g_window.load());
    ImGui::Text("%s.%s", g_editPlugin.c_str(), g_editAction.c_str());
    ImGui::TextWrapped("%s", T(Key::CaptureHelp));
    ImGui::SetNextItemWidth(300 * g_currentUiScale * g_currentDpiScale);
    ImGui::InputText("##NewCombo", g_editCombo, sizeof(g_editCombo));
    InspectUiItem("editor.Combo", g_editCombo);
    if (ImGui::Button(T(Key::CaptureButton))) {
        g_captureKeys = true;
        for (int i = 0; i < 256; ++i) g_captureDown[i] = (GetAsyncKeyState(i) & 0x8000) != 0;
    }
    InspectUiItem("editor.Capture");
    if (g_captureKeys) { ImGui::SameLine(); DrawSpinner(); CaptureHotkey(); }
    if (g_invalidCombo) ImGui::TextWrapped("%s", T(Key::InvalidCombo));
    if (ImGui::Button(T(Key::Apply))) {
        const std::string combo(g_editCombo);
        uint32_t vk = 0, mods = 0;
        g_invalidCombo = !combo.empty() && (!Hotkeys::ParseCombo(std::wstring(combo.begin(), combo.end()), vk, mods) || vk == Hotkeys::kManagerKey);
        if (!g_invalidCombo) {
            const auto plugin = g_editPlugin, action = g_editAction;
            g_managerAction = [plugin, action, vk, mods] {
                g_saveFailed = Hotkeys::SetOverride(ConfigPath(), plugin.c_str(), action.c_str(), vk, mods) != GAKU_OK;
            };
            g_editorOpen = g_captureKeys = false; Hotkeys::SetCaptureActive(false); ImGui::CloseCurrentPopup();
        }
    }
    InspectUiItem("editor.Apply");
    ImGui::SameLine();
    if (ImGui::Button(T(Key::Cancel))) {
        g_editorOpen = g_captureKeys = false; Hotkeys::SetCaptureActive(false); ImGui::CloseCurrentPopup();
    }
    InspectUiItem("editor.Cancel");
    ImGui::EndPopup();
}
void DrawHotkeysPage() {
    ImGui::TextWrapped("%s", T(Key::HotkeyHelp));
    const auto rows = Hotkeys::Snapshot();
    if (rows.empty()) ImGui::TextWrapped("%s", T(Key::NoHotkeys));
    else if (ImGui::BeginTable("##hotkeys", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX)) {
        ImGui::TableSetupColumn(T(Key::Plugin)); ImGui::TableSetupColumn(T(Key::Name));
        ImGui::TableSetupColumn(T(Key::Combo)); ImGui::TableSetupColumn(T(Key::State)); ImGui::TableSetupColumn(T(Key::Action));
        ImGui::TableHeadersRow();
        for (const auto& row : rows) {
            ImGui::PushID(row.pluginId.c_str()); ImGui::PushID(row.actionId.c_str());
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(row.pluginId.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(row.displayName.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", row.actionId.c_str());
            ImGui::TableNextColumn();
            if (ImGui::Selectable(row.unbound ? T(Key::Unbound) : row.combo.c_str())) BeginHotkeyEditor(row);
            InspectUiItem("hotkey.Combo", (row.pluginId + "." + row.actionId).c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(T(row.unbound ? Key::Unbound : row.conflicted ? Key::Conflict : Key::Active));
            ImGui::TableNextColumn();
            if (ImGui::SmallButton(T(Key::Edit))) BeginHotkeyEditor(row);
            InspectUiItem("hotkey.Edit");
            if (ImGui::SmallButton(T(Key::Unbind))) g_managerAction = [row] {
                g_saveFailed = Hotkeys::SetOverride(ConfigPath(), row.pluginId.c_str(), row.actionId.c_str(), 0, 0) != GAKU_OK;
            };
            InspectUiItem("hotkey.Unbind");
            if (ImGui::SmallButton(T(Key::Reset))) g_managerAction = [row] {
                g_saveFailed = Hotkeys::ResetOverride(ConfigPath(), row.pluginId.c_str(), row.actionId.c_str()) != GAKU_OK;
            };
            InspectUiItem("hotkey.Reset");
            ImGui::PopID(); ImGui::PopID();
        }
        ImGui::EndTable();
    }
    DrawHotkeyEditor();
}
void DrawSettingsPage(const DesktopHostSnapshot& snapshot) {
    // Read once per session; file I/O and writes run outside the CPU callback gate.
    static bool loaded = false, console = false, trace = false;
    if (!loaded && !g_managerAction) g_managerAction = [] {
        console = ReadConfigFlag(ConfigPath(), L"host", L"console", false);
        trace = ReadConfigFlag(ConfigPath(), L"diagnostics", L"trace", false); loaded = true;
    };
    ImGui::TextWrapped("%s", T(Key::NextStart)); ImGui::Spacing();
    ImGui::BeginDisabled(!loaded);
    if (ImGui::Checkbox(T(Key::Console), &console)) g_managerAction = [] {
        g_saveFailed = !WritePrivateProfileStringW(L"host", L"console", console ? L"true" : L"false", ConfigPath().c_str());
        if (g_saveFailed) console = !console;
    };
    InspectUiItem("settings.Console", console ? "true" : "false");
    if (ImGui::Checkbox(T(Key::Trace), &trace)) g_managerAction = [] {
        g_saveFailed = !WritePrivateProfileStringW(L"diagnostics", L"trace", trace ? L"true" : L"false", ConfigPath().c_str());
        if (g_saveFailed) trace = !trace;
    };
    InspectUiItem("settings.Trace", trace ? "true" : "false");
    ImGui::EndDisabled();
    struct PluginSetting { std::wstring filename; std::string label; bool enabled; };
    static std::vector<PluginSetting> plugins;
    static std::vector<std::wstring> knownFiles;
    if (knownFiles != snapshot.pluginFiles && !g_managerAction) {
        const auto files = snapshot.pluginFiles;
        g_managerAction = [files] {
            plugins.clear();
            for (const auto& filename : files) {
                const auto utf8 = std::filesystem::path(filename).u8string();
                plugins.push_back({filename, std::string(utf8.begin(), utf8.end()),
                    ReadConfigFlag(ConfigPath(), L"plugins", filename.c_str(), true)});
            }
            knownFiles = files;
        };
    }
    ImGui::Spacing(); ImGui::Separator(); ImGui::TextUnformatted(T(Key::PluginEnable));
    ImGui::TextWrapped("%s", T(Key::PluginEnableHelp));
    for (size_t i = 0; i < plugins.size(); ++i) {
        auto& plugin = plugins[i];
        if (ImGui::Checkbox(plugin.label.c_str(), &plugin.enabled)) g_managerAction = [i] {
            auto& value = plugins[i];
            g_saveFailed = !WritePrivateProfileStringW(L"plugins", value.filename.c_str(),
                value.enabled ? L"true" : L"false", ConfigPath().c_str());
            if (g_saveFailed) value.enabled = !value.enabled;
        };
        InspectUiItem("settings.Plugin", plugin.label.c_str());
    }
}
void DrawManagerPage(const DesktopHostSnapshot& snapshot) {
    ImGui::TextUnformatted(T(Key::Manager)); ImGui::Separator();
    if (g_saveFailed) ImGui::TextWrapped("%s", T(Key::SaveFailed));
    if (ImGui::BeginTabBar("##ManagerTabs")) {
        if (ManagerTab(Key::PluginList, "PluginList")) { DrawPluginList(snapshot); ImGui::EndTabItem(); }
        if (ManagerTab(Key::Appearance, "Appearance")) { DrawAppearancePage(); ImGui::EndTabItem(); }
        if (ManagerTab(Key::Hotkeys, "Hotkeys")) { DrawHotkeysPage(); ImGui::EndTabItem(); }
        if (ManagerTab(Key::Settings, "Settings")) { DrawSettingsPage(snapshot); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
}
void DrawHostShell(bool pluginGateHeld) {
    SnapshotDesktopHost(g_hostSnapshot);
    g_wantsTextInput.store(ImGui::GetIO().WantTextInput, std::memory_order_release);
    if (g_selectedPlugin.empty()) g_selectedPlugin = kManagerPanel;
    if (g_selectedPlugin != kManagerPanel && g_editorOpen) {
        g_editorOpen = g_captureKeys = false; Hotkeys::SetCaptureActive(false);
    }
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##GakuLoaderDesktopShell", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar);
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu(Label(Key::File, "FileMenu").c_str())) {
            if (ImGui::MenuItem(T(Key::Hide))) PostMessageW(g_window, WM_CLOSE, 0, 0);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(Label(Key::View, "ViewMenu").c_str())) {
            if (ImGui::MenuItem(T(Key::Manager), "F1")) g_selectedPlugin = kManagerPanel;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(Label(Key::Plugins, "PluginsMenu").c_str())) {
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
    if (ImGui::Selectable(Label(Key::Manager, "ManagerNav").c_str(), g_selectedPlugin == kManagerPanel)) g_selectedPlugin = kManagerPanel;
    ImGui::Separator();
    ImGui::TextUnformatted(T(Key::Plugins));
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
            ImGui::TextWrapped("%s", T(Key::SelectPanel));
        } else {
            ImGui::Text("%s  %s", selected->name.c_str(), ManagerStrings::FormatVersion(selected->version).c_str()); ImGui::Separator();
            if (selected->failed) ImGui::TextWrapped("%s", T(Key::FailedHelp));
            else if (!selected->hasPanel) ImGui::TextWrapped("%s", T(Key::NoPanel));
            else {
                ImGui::PushID(selected->id.c_str());
                g_inCallback = true;
                const double panelStarted = NowMs();
                bool dispatched = true;
                if (pluginGateHeld) DispatchDesktopPanel(selected->id.c_str());
                else dispatched = TryDispatchDesktopFrame([](void* panel) {
                    DispatchDesktopPanel(static_cast<DesktopPluginPanel*>(panel)->id.c_str());
                }, &*selected);
                const double panelElapsed = NowMs() - panelStarted;
                g_inCallback = false;
                ImGui::PopID();
                if (!dispatched) {
                    DrawSpinner(); ImGui::SameLine();
                    ImGui::TextUnformatted(T(Key::Waiting));
                }
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
    ImGui::Text("GakuLoader: %zu | Gakumas: %s", g_hostSnapshot.plugins.size(),
        T(g_hostSnapshot.shuttingDown ? Key::Stopping : g_hostSnapshot.runtimeReady ? Key::Ready : Key::Waiting));
    ImGui::End();
}
bool RenderFrame() {
    // The shell paints on its own thread. A gap here means the whole window
    // stopped painting — focus handover, a device reset or a blocked UI
    // thread — which is what a user experiences as "the settings froze".
    const double frameStarted = NowMs();
    static double previousFrame = 0.0;
    static double lastGapWarn = -1.0e9;
    static double lastGapNote = -1.0e9;
    // Two bands, because one WARN cannot mean both "an alt-tab cost us three
    // frames" and "the panel is wedged". 50-200 ms is the ordinary cost of a
    // focus handover and is only interesting with [log] level=debug; >=200 ms is
    // what a user perceives as a freeze and keeps warning severity.
    const double frameGapMs = previousFrame > 0.0 ? frameStarted - previousFrame : 0.0;
    if (frameGapMs >= 200.0 && frameStarted - lastGapWarn >= 1000.0) {
        lastGapWarn = frameStarted;
        LogWarn("[DesktopUI] frame gap %.0f ms before painting the shell", frameGapMs);
    } else if (frameGapMs >= 50.0 && frameStarted - lastGapNote >= 1000.0) {
        lastGapNote = frameStarted;
        LogDebug("[DesktopUI] frame gap %.0f ms before painting the shell (below the 200 ms warn threshold)", frameGapMs);
    }
    previousFrame = frameStarted;
    {
        ContextScope scope;
        // Rebuild atlas and upload outside the host callback gate, before any
        // NewFrame/draw data can borrow fonts from the old atlas.
        if (g_fontsDirty) {
            ImGui_ImplDX11_InvalidateDeviceObjects();
            const auto fonts = DesktopFonts::BuildFonts(g_currentUiScale, g_currentDpiScale);
            g_scaleLimited = !fonts.ready;
            if (!fonts.ready) { g_currentUiScale = g_appliedUiScale; g_currentDpiScale = g_appliedDpiScale; }
            if (!ImGui_ImplDX11_CreateDeviceObjects()) return false;
            g_fontsDirty = false;
        }
        if (g_appearanceDirty) {
            DesktopAppearance::ApplyAppearance(g_currentTheme, g_currentUiScale, g_currentDpiScale);
            g_appliedUiScale = g_currentUiScale; g_appliedDpiScale = g_currentDpiScale;
            g_publishedTheme = g_currentTheme; g_publishedScale = g_currentUiScale;
            g_appearanceDirty = false;
        }
        if (g_saveAppearance) {
            g_saveFailed = !DesktopAppearance::SaveAppearanceSettings(ConfigPath(), g_currentTheme, g_currentUiScale);
            g_saveAppearance = false;
        }
        // The manager owns only copied status and UI-thread state, so its
        // spinners keep painting while initialization owns the plugin gate.
        // Selected plugin pages retain their last complete image on contention
        // instead of flashing an empty panel between short Unity callbacks.
        const auto buildFrame = [](void* gateHeld) {
#if defined(GAKULOADER_DESKTOP_UI_TEST)
            if (g_testHooks.beginCpuFrame) g_testHooks.beginCpuFrame();
#endif
            ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
            g_fileDrop.BeginFrame();
            DrawHostShell(*static_cast<bool*>(gateHeld));
            g_fileDrop.EndFrame();
            ImGui::Render();
        };
        bool pluginGateHeld = !g_selectedPlugin.empty() && g_selectedPlugin != kManagerPanel;
        if (!pluginGateHeld) buildFrame(&pluginGateHeld);
        else if (!TryDispatchDesktopFrame(buildFrame, &pluginGateHeld)) return true;
        if (g_managerAction) { auto action = std::move(g_managerAction); g_managerAction = {}; action(); }
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
    AllowShellFileDrops(window);
    DragAcceptFiles(window, TRUE);
    // Explorer stops converting file drops into WM_DROPFILES for a window as
    // soon as that window registers an IDropTarget, and UIPI refuses every
    // cross-integrity IDropTarget call. Arming OLE above Explorer's integrity
    // therefore deletes the one transport ChangeWindowMessageFilterEx keeps
    // open and leaves nothing at all: the shell shows its no-drop cursor while
    // the window receives neither OLE nor WM_DROPFILES. Only arm it when the
    // shell can actually reach it.
    const HostIntegrity integrity = ReadHostIntegrity();
    const bool shellCanReachDropTarget =
        integrity.known && !integrity.elevated && integrity.rid <= SECURITY_MANDATORY_MEDIUM_RID;
    g_fileDrop.SetHostBlocked(!shellCanReachDropTarget, integrity.elevated);
    // OLE is initialised either way: the apartment also backs the shell dialogs
    // this thread opens, and only the target registration is integrity-bound.
    if (SUCCEEDED(OleInitialize(nullptr))) {
        g_oleReady = true;
        if (!shellCanReachDropTarget) {
            LogInfo("[DesktopUI] OLE drop target skipped: the shell cannot reach it at this "
                "integrity level, and merely registering it would suppress the whitelisted "
                "WM_DROPFILES path. WM_DROPFILES stays the live transport.");
        } else {
            auto* target = new (std::nothrow) ShellDropTarget(window);
            if (target && SUCCEEDED(RegisterDragDrop(window, target))) {
                g_dropTarget = target;
                LogInfo("[DesktopUI] OLE drop target registered hwnd=%p; hover feedback transport armed", window);
            } else {
                if (target) target->Release();
                LogWarn("[DesktopUI] RegisterDragDrop failed Win32=%lu; drag hover feedback disabled", GetLastError());
            }
        }
    } else {
        LogWarn("[DesktopUI] OleInitialize failed; file drop keeps WM_DROPFILES only");
    }
    // Report which transport is actually live. Registration succeeding does not
    // mean the shell may call it, and a wrong transport is invisible at runtime.
    LogInfo("[DesktopUI] host integrity rid=0x%04x elevated=%d known=%d drag-hover=%s transport=%s",
        integrity.rid, integrity.elevated ? 1 : 0, integrity.known ? 1 : 0,
        g_fileDrop.HostBlocked() ? "blocked-by-uipi" : "available",
        g_dropTarget ? "ole" : "wm-dropfiles");
    if (integrity.elevated) {
        LogWarn("[DesktopUI] elevated host: Explorer runs at medium integrity, so UIPI blocks every "
            "IDropTarget call, blocks the shell's low-level keyboard hook (Print Screen and the "
            "Snipping Tool do nothing while this window has focus) and makes the shell show its "
            "no-drop cursor. Drops still arrive through WM_DROPFILES. Run the game at the same "
            "integrity level as Explorer to get the drag preview and the system screenshot key");
    } else if (g_fileDrop.HostBlocked()) {
        LogWarn("[DesktopUI] hover preview unavailable: host integrity is unknown, so the OLE drop "
            "target was not armed; WM_DROPFILES remains the transport");
    }
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
namespace {
// Debounced, UI-thread-local diagnostics for file-drop service UNAVAILABLE.
// A plugin may poll every frame; only a state change (first hit, new owner or
// new reason) or a >=30s steady-clock gap emits one Warn, never a per-frame log.
struct FileDropUnavailableDiag {
    std::string owner, reason;
    ULONGLONG lastWarnTick = 0;
    bool primed = false;
};
void NoteFileDropUnavailable(FileDropUnavailableDiag& state, const char* api, const char* owner) {
    const char* reason =
          g_stop                              ? "stop"
        : GetCurrentThreadId() != g_uiThread  ? "not-ui-thread"
        : !g_inCallback                      ? "outside-callback"
        : g_selectedPlugin != owner          ? "owner-mismatch"
        : nullptr;
    if (!reason) return;
    const ULONGLONG now = GetTickCount64();
    const bool changed = !state.primed || state.owner != owner || state.reason != reason;
    if (changed || now - state.lastWarnTick >= 30000) {
        LogWarn("[DesktopUI][FileDrop] unavailable api=%s owner=%s reason=%s", api, owner, reason);
        state.owner = owner;
        state.reason = reason;
        state.lastWarnTick = now;
        state.primed = true;
    }
}
} // namespace
GakuStatus SetFileDropTarget(const char* pluginId, const GakuFileDropTarget* target) {
    static thread_local FileDropUnavailableDiag diag;
    if (!pluginId || !pluginId[0]) return GAKU_E_INVALID_ARGUMENT;
    NoteFileDropUnavailable(diag, "SetFileDropTarget", pluginId);
    if (g_stop || GetCurrentThreadId() != g_uiThread || !g_inCallback || g_selectedPlugin != pluginId)
        return GAKU_E_UNAVAILABLE;
    return g_fileDrop.SetTarget(pluginId, target);
}
GakuStatus PollFileDrop(const char* pluginId, uint32_t size, GakuFileDropResult* result,
    uint32_t capacity, char* paths) {
    static thread_local FileDropUnavailableDiag diag;
    if (!pluginId || !pluginId[0]) return GAKU_E_INVALID_ARGUMENT;
    NoteFileDropUnavailable(diag, "PollFileDrop", pluginId);
    if (g_stop || GetCurrentThreadId() != g_uiThread || !g_inCallback || g_selectedPlugin != pluginId)
        return GAKU_E_UNAVAILABLE;
    const auto status = g_fileDrop.Poll(pluginId, size, result, capacity, paths);
    if (status == GAKU_OK && capacity && result->fileCount)
        LogInfo("[DesktopUI][FileDrop] consumed owner=%s files=%u bytes=%u",
            pluginId, result->fileCount, result->requiredBytes);
    return status;
}
GakuStatus PeekFileDrag(const char* pluginId, uint32_t size, GakuFileDragState* state,
    uint32_t capacity, char* paths) {
    static thread_local FileDropUnavailableDiag diag;
    if (!pluginId || !pluginId[0]) return GAKU_E_INVALID_ARGUMENT;
    NoteFileDropUnavailable(diag, "PeekFileDrag", pluginId);
    if (g_stop || GetCurrentThreadId() != g_uiThread || !g_inCallback || g_selectedPlugin != pluginId)
        return GAKU_E_UNAVAILABLE;
    return g_fileDrop.QueryDrag(pluginId, size, state, capacity, paths);
}
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
