// Production standalone desktop window with a hidden WARP device. No game,
// Localify, VRify, Present detour, or GameAssembly is loaded by this test.
#include "DesktopUi.hpp"
#include "DesktopDialogs.hpp"
#include "Host.hpp"
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <d3d11.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <atomic>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::atomic_uint frames{0}, inspectedFrames{0}, gameCloses{0};
std::atomic_uint otherPanelFrames{0};
std::atomic_uint cpuFrames{0}, presentedFrames{0}, rejectedFrames{0};
std::atomic_bool rejectFrames{false};
std::atomic_bool expectPluginPixels{false};
std::atomic_bool buildingFrame{false}, requestPasteCheck{false}, requestCopy{false};
std::atomic_bool blockClipboard{false}, clipboardEntered{false}, releaseClipboard{false};
std::atomic_uint clipboardGets{0}, clipboardSets{0}, clipboardSequence{1}, pasteChecks{0};
std::atomic_bool fileDropArmed{false};
std::atomic_uint fileDropTargetFrames{0}, nativeDropBatches{0};
std::atomic_int fileDropX{0}, fileDropY{0};
std::mutex clipboardMutex;
std::string clipboardText = "initial simulated clipboard";
std::atomic_bool keySeen{false}, callbackRunning{false}, stopped{false};
std::atomic_int displayWidth{0}, displayHeight{0};
std::mutex errorMutex;
std::string error;
HMODULE consumerModule = nullptr;
using ConsumerDrawFn = int (*)(const GakuDesktopUiService*);
ConsumerDrawFn consumerDraw = nullptr;
const GakuImGuiAbi kAbi{IMGUI_VERSION_NUM, sizeof(ImGuiIO), sizeof(ImGuiStyle),
    sizeof(ImDrawVert), sizeof(ImDrawIdx), sizeof(ImWchar)};
void Check(bool success, const char* message) { if (!success) throw std::runtime_error(message); }
void Fail(const char* message) { std::lock_guard lock(errorMutex); if (error.empty()) error = message; }
void CheckWorker() { std::lock_guard lock(errorMutex); Check(error.empty(), error.c_str()); }
const char* FakeClipboardGet(void*) {
    if (buildingFrame || callbackRunning) Fail("native clipboard fetch ran inside CPU frame/callback gate");
    ++clipboardGets;
    if (blockClipboard.exchange(false)) {
        clipboardEntered = true;
        const auto deadline = GetTickCount64() + 2000;
        while (!releaseClipboard && GetTickCount64() < deadline) Sleep(1);
        if (!releaseClipboard) Fail("delayed clipboard test was not released");
    }
    thread_local std::string copied;
    std::lock_guard lock(clipboardMutex); copied = clipboardText; return copied.c_str();
}
void FakeClipboardSet(void*, const char* text) {
    if (buildingFrame || callbackRunning) Fail("native clipboard write ran inside CPU frame/callback gate");
    std::lock_guard lock(clipboardMutex); clipboardText = text; ++clipboardSequence; ++clipboardSets;
}
void ChangeClipboard(const char* text) {
    std::lock_guard lock(clipboardMutex); clipboardText = text; ++clipboardSequence;
}
const std::vector<std::wstring> kDropFiles{L"曲库/动作 🎵.vmd", L"曲库/カメラ.gkcamera", L"曲库/song.wav"};
const std::string kExpectedDropBytes = [] {
    std::string packed;
    for (const auto* path : {"曲库/动作 🎵.vmd", "曲库/カメラ.gkcamera", "曲库/song.wav"}) {
        packed.append(path); packed.push_back('\0');
    }
    return packed;
}();
void SendNativeDrop(HWND window, POINT clientPoint, bool reportedNonClient = false) {
    if (reportedNonClient) Check(ClientToScreen(window, &clientPoint) != FALSE, "screen coordinates for fNC test");
    size_t characterCount = 1;
    for (const auto& path : kDropFiles) characterCount += path.size() + 1;
    const HGLOBAL memory = GlobalAlloc(GHND, sizeof(DROPFILES) + characterCount * sizeof(wchar_t));
    Check(memory != nullptr, "native file-drop allocation");
    auto* drop = static_cast<DROPFILES*>(GlobalLock(memory));
    if (!drop) { GlobalFree(memory); Check(false, "native file-drop lock"); }
    drop->pFiles = sizeof(DROPFILES); drop->pt = clientPoint; drop->fNC = reportedNonClient ? TRUE : FALSE; drop->fWide = TRUE;
    auto* next = reinterpret_cast<wchar_t*>(reinterpret_cast<char*>(drop) + sizeof(DROPFILES));
    for (const auto& path : kDropFiles) {
        std::memcpy(next, path.c_str(), (path.size() + 1) * sizeof(wchar_t));
        next += path.size() + 1;
    }
    *next = L'\0';
    GlobalUnlock(memory);
    // Production WM_DROPFILES owns the HDROP and must DragFinish it even when
    // the hit is rejected. No real files, Explorer automation or imports occur.
    SendMessageW(window, WM_DROPFILES, reinterpret_cast<WPARAM>(memory), 0);
}
void CheckShellDropFilters(HWND desktop, HWND untouchedWindow) {
    // Repeating ALLOW reports whether production startup actually configured
    // the per-window filter. A same-process WM_DROPFILES alone bypasses UIPI,
    // so the existing native-drop test could pass without either exception.
    for (const UINT message : {static_cast<UINT>(WM_DROPFILES), 0x0049u}) {
        CHANGEFILTERSTRUCT desktopFilter{sizeof(desktopFilter)};
        Check(ChangeWindowMessageFilterEx(desktop, message, MSGFLT_ALLOW, &desktopFilter) != FALSE,
            "could not inspect desktop Shell file-drop UIPI filter");
        Check(desktopFilter.ExtStatus == MSGFLTINFO_ALREADYALLOWED_FORWND,
            "desktop startup omitted a per-window Shell file-drop UIPI exception");
        // DISALLOW distinguishes the untouched default (already disallowed)
        // from an exception allowed by this HWND or at process/global scope.
        CHANGEFILTERSTRUCT untouchedFilter{sizeof(untouchedFilter)};
        Check(ChangeWindowMessageFilterEx(untouchedWindow, message, MSGFLT_DISALLOW, &untouchedFilter) != FALSE,
            "could not inspect unrelated-window Shell file-drop UIPI scope");
        Check(untouchedFilter.ExtStatus == MSGFLTINFO_ALREADYDISALLOWED_FORWND,
            "Shell file-drop UIPI exception escaped the independent desktop HWND");
        // Exercise a first ALLOW and restore the synthetic window's default.
        CHANGEFILTERSTRUCT otherFilter{sizeof(otherFilter)};
        Check(ChangeWindowMessageFilterEx(untouchedWindow, message, MSGFLT_ALLOW, &otherFilter) != FALSE,
            "could not inspect unrelated-window Shell file-drop UIPI filter");
        Check(otherFilter.ExtStatus == MSGFLTINFO_NONE,
            "Shell file-drop UIPI exception escaped the independent desktop HWND");
        // RESET resets the complete HWND filter and requires message == 0.
        CHANGEFILTERSTRUCT resetFilter{sizeof(resetFilter)};
        Check(ChangeWindowMessageFilterEx(untouchedWindow, 0, MSGFLT_RESET, &resetFilter) != FALSE,
            "could not reset unrelated-window Shell file-drop filter probe");
        Check(resetFilter.ExtStatus == MSGFLTINFO_NONE,
            "unrelated-window Shell file-drop filter reset returned an unexpected status");
    }
}
template<class T> void Release(T*& value) { if (value) { value->Release(); value = nullptr; } }
void Pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
}
template<class Predicate> void WaitUntil(Predicate predicate, const char* message) {
    const ULONGLONG deadline = GetTickCount64() + 15000;
    while (!predicate() && GetTickCount64() < deadline) { CheckWorker(); Pump(); Sleep(5); }
    CheckWorker(); Check(predicate(), message);
}
// The host shell opens on its manager page, so a plugin panel only renders once
// the operator selects it. Synthetic events are queued from the CPU-frame hook
// (UI thread, inside the live ImGui context) instead of moving the real cursor.
std::atomic_int clickY{-1};
std::atomic_int clickStep{0};
void QueueSelectClick(int y) { clickStep = 0; clickY = y; }
void ServiceSelectionClick() {
    const int y = clickY.load();
    if (y < 0) return;
    const int step = clickStep.load();
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(60.0f, static_cast<float>(y));
    if (step == 0) io.AddMouseButtonEvent(0, true);
    else if (step == 1) io.AddMouseButtonEvent(0, false);
    else { clickY = -1; return; }
    clickStep = step + 1;
}
void SelectPluginPanel() {
    // Rows of the navigation column are ~20px tall; a 4px sweep cannot skip one.
    for (int y = 30; y <= 300 && frames.load() == 0; y += 4) {
        const auto presented = presentedFrames.load();
        QueueSelectClick(y);
        WaitUntil([&] { return presentedFrames.load() >= presented + 3; }, "synthetic selection frame");
    }
    Check(frames.load() > 0, "plugin panel could not be selected from the host navigation");
    expectPluginPixels = true;
}
LRESULT CALLBACK GameProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_CLOSE) { ++gameCloses; return 0; }
    return DefWindowProcW(window, message, wparam, lparam);
}
void InspectPixel(void* rawDevice, void* rawContext, void* rawSwapchain) {
    try {
        auto* device = static_cast<ID3D11Device*>(rawDevice);
        auto* context = static_cast<ID3D11DeviceContext*>(rawContext);
        auto* chain = static_cast<IDXGISwapChain*>(rawSwapchain);
        ID3D11Texture2D *buffer = nullptr, *staging = nullptr;
        Check(SUCCEEDED(chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&buffer))), "desktop readback buffer");
        D3D11_TEXTURE2D_DESC description{}; buffer->GetDesc(&description);
        description.Usage = D3D11_USAGE_STAGING; description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        description.BindFlags = 0; description.MiscFlags = 0;
        Check(SUCCEEDED(device->CreateTexture2D(&description, nullptr, &staging)), "desktop readback staging");
        context->CopyResource(staging, buffer);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Check(SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)), "desktop readback map");
        const auto* red = static_cast<const unsigned char*>(mapped.pData) + mapped.RowPitch * 50 + 50 * 4;
        const auto* cyan = static_cast<const unsigned char*>(mapped.pData) + mapped.RowPitch * 50 + 150 * 4;
        const bool redPresent = red[0] > 240 && red[1] < 10 && red[2] < 10;
        const bool cyanPresent = !consumerDraw || (cyan[0] < 10 && cyan[1] > 240 && cyan[2] > 240);
        context->Unmap(staging, 0); Release(staging); Release(buffer);
        if (expectPluginPixels) {
            Check(redPresent, "plugin pixels absent from standalone desktop backbuffer");
            Check(cyanPresent, "cross-DLL plugin pixels absent from desktop backbuffer");
        }
        GakuDesktopUiBinding binding{};
        Check(GakuLoader::DesktopUi::GetService()->bind(&kAbi, sizeof(binding), &binding) == GAKU_E_UNAVAILABLE,
            "binding available outside selected plugin callback");
        ++inspectedFrames;
    } catch (const std::exception& problem) { Fail(problem.what()); }
}
void CheckDialogsStopped() {
    int owner = 0;
    const auto dialogs = GakuLoader::DesktopDialogs::ServiceForOwner(&owner);
    const GakuFileFilter filter{L"All files", L"*.*"};
    const GakuFileDialogRequest request{GAKU_SERVICE_VERSION, sizeof(request), L"Stopped desktop", 1, &filter};
    uint64_t ticket = 0xA5A5;
    Check(dialogs.beginOpen(dialogs.context, &request, &ticket) == GAKU_E_SHUTTING_DOWN && ticket == 0xA5A5,
        "dialog request accepted after desktop became permanently unavailable");
}
}
namespace GakuLoader {
std::filesystem::path ConfigPath() { return ""; }
void LogInfo(const char* format, ...) { va_list args; va_start(args, format); vprintf(format, args); puts(""); va_end(args); }
void LogWarn(const char* format, ...) { va_list args; va_start(args, format); vprintf(format, args); puts(""); va_end(args); }
void LogError(const char* format, ...) { va_list args; va_start(args, format); vprintf(format, args); puts(""); va_end(args); }
// Actual mutex contention is tested with the real PluginHost+DLL fixtures by
// test-host.ps1. This adapter forces denial to test the production GPU branch.
bool TryDispatchDesktopFrame(void (*build)(void*), void* data) {
    if (rejectFrames) { ++rejectedFrames; return false; }
    buildingFrame = true;
    build(data);
    buildingFrame = false;
    return true;
}
bool SnapshotDesktopHost(DesktopHostSnapshot& output) {
    output.plugins = {{"gaku.test", "Desktop test", "1", true, true, false, true},
        {"gaku.other", "Other desktop test", "1", true, true, false, true},
        {"gaku.legacy", "Legacy test", "1", false, true, false, false},
        {"gaku.failed", "Failed test", "1", true, true, true, true},
        {"gaku.no-panel", "Headless test", "1", true, true, false, false}};
    output.runtimeReady = true; output.shuttingDown = false; output.activated = true;
    return true;
}
void DispatchDesktopPanel(const char* id) {
    callbackRunning = true;
    try {
        Check(!stopped && (std::strcmp(id, "gaku.test") == 0 || std::strcmp(id, "gaku.other") == 0),
            "wrong or post-shutdown plugin panel dispatch");
        if (std::strcmp(id, "gaku.other") == 0) {
            ++otherPanelFrames;
            callbackRunning = false;
            return;
        }
        auto* service = DesktopUi::GetService();
        GakuDesktopUiBinding binding{};
        Check(service->bind(&kAbi, sizeof(binding), &binding) == GAKU_OK, "valid UI binding refused");
        Check(binding.imguiContext == ImGui::GetCurrentContext() && binding.alloc && binding.free, "binding context/allocator");
        const auto& ids = ImGui::GetCurrentWindow()->IDStack;
        Check(ids.Size >= 2 && ids.back() == ImHashStr(id, 0, ids[ids.Size - 2]),
            "plugin panel is missing its host-owned ID namespace");
        const ImGuiID siblingSeed = ImHashStr("gaku.other-plugin", 0, ids[ids.Size - 2]);
        Check(ImGui::GetID("Shared input label") != ImHashStr("Shared input label", 0, siblingSeed),
            "same widget labels collide between plugin panel namespaces");
        if (requestPasteCheck.exchange(false)) {
            Check(std::strcmp(ImGui::GetClipboardText(), "fresh clipboard: 星南") == 0,
                "first paste returned stale clipboard text");
            ++pasteChecks;
        }
        if (requestCopy.exchange(false)) ImGui::SetClipboardText("copied from plugin panel");
        if (fileDropArmed) {
            ImGui::Dummy(ImVec2(180.0f, 60.0f));
            const ImVec2 minimum = ImGui::GetItemRectMin(), maximum = ImGui::GetItemRectMax();
            const GakuFileDropTarget target{GAKU_FILE_DROP_VERSION, sizeof(GakuFileDropTarget),
                minimum.x, minimum.y, maximum.x, maximum.y};
            Check(DesktopUi::SetFileDropTarget(id, &target) == GAKU_OK, "native file-drop target registration");
            Check(DesktopUi::SetFileDropTarget("gaku.other", &target) == GAKU_E_UNAVAILABLE,
                "another panel could set the selected owner's file-drop target");
            GakuFileDropResult drop{};
            Check(DesktopUi::PollFileDrop("gaku.other", sizeof(drop), &drop, 0, nullptr) == GAKU_E_UNAVAILABLE,
                "another panel could poll the selected owner's file-drop batch");
            Check(DesktopUi::PollFileDrop(id, sizeof(drop), &drop, 0, nullptr) == GAKU_OK,
                "native file-drop metadata poll");
            if (drop.fileCount) {
                Check(drop.fileCount == kDropFiles.size() && drop.requiredBytes == kExpectedDropBytes.size(),
                    "native file-drop count or Unicode byte size");
                std::vector<char> output(drop.requiredBytes);
                Check(DesktopUi::PollFileDrop(id, sizeof(drop), &drop,
                    static_cast<uint32_t>(output.size()), output.data()) == GAKU_OK, "native file-drop copy");
                Check(std::memcmp(output.data(), kExpectedDropBytes.data(), output.size()) == 0,
                    "native file-drop changed Unicode paths or batch order");
                ++nativeDropBatches;
            }
            fileDropX = static_cast<int>((minimum.x + maximum.x) * 0.5f);
            fileDropY = static_cast<int>((minimum.y + maximum.y) * 0.5f);
            ++fileDropTargetFrames;
        } else {
            Check(DesktopUi::SetFileDropTarget(id, nullptr) == GAKU_OK, "disable file-drop target");
        }
        void* memory = binding.alloc(16, binding.allocatorUserData); Check(memory != nullptr, "allocator");
        binding.free(memory, binding.allocatorUserData);
        GakuDesktopUiBinding untouched; std::memset(&untouched, 0xA5, sizeof(untouched)); const auto before = untouched;
        auto wrong = kAbi; ++wrong.drawIdxSize;
        Check(service->bind(&wrong, sizeof(untouched), &untouched) == GAKU_E_VERSION &&
            std::memcmp(&untouched, &before, sizeof(before)) == 0, "ABI mismatch changed output");
        Check(service->bind(&kAbi, sizeof(binding) - 1, &untouched) == GAKU_E_SIZE &&
            std::memcmp(&untouched, &before, sizeof(before)) == 0, "short binding changed output");
        ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(20, 20), ImVec2(100, 100), IM_COL32(255, 0, 0, 255));
        if (consumerDraw) Check(consumerDraw(service) == 0, "cross-DLL consumer binding failed");
        if (ImGui::IsKeyDown(ImGuiKey_A)) keySeen = true;
        displayWidth = static_cast<int>(ImGui::GetIO().DisplaySize.x);
        displayHeight = static_cast<int>(ImGui::GetIO().DisplaySize.y);
        ++frames;
    } catch (const std::exception& problem) { Fail(problem.what()); }
    callbackRunning = false;
}
}
int main(int argc, char** argv) {
    HWND game = nullptr;
    try {
        if (argc == 2 && std::strcmp(argv[1], "--startup-failure") == 0) {
            // Force a real production RegisterClassEx failure, with a request
            // already queued. Failure must cancel it and reject future Browse.
            WNDCLASSW conflict{}; conflict.hInstance = GetModuleHandleW(nullptr);
            conflict.lpfnWndProc = DefWindowProcW; conflict.lpszClassName = L"GakuModLoader.DesktopControl";
            Check(RegisterClassW(&conflict) != 0, "startup-failure class fixture");
            int owner = 0;
            const auto dialogs = GakuLoader::DesktopDialogs::ServiceForOwner(&owner);
            const GakuFileFilter filter{L"All files", L"*.*"};
            const GakuFileDialogRequest request{GAKU_SERVICE_VERSION, sizeof(request), L"Queued before failure", 1, &filter};
            uint64_t ticket = 0;
            Check(dialogs.beginOpen(dialogs.context, &request, &ticket) == GAKU_OK, "queue before startup failure");
            Check(GakuLoader::DesktopUi::Initialize(), "startup failure worker creation");
            WaitUntil([] { const auto* service = GakuLoader::DesktopUi::GetWindowService();
                return service->requestShow(service->context) == GAKU_E_SHUTTING_DOWN; },
                "failed presentation did not enter terminal state");
            GakuLoader::DesktopUi::Shutdown();
            GakuFileDialogResult result{};
            Check(dialogs.poll(dialogs.context, ticket, sizeof(result), &result, 0, nullptr) == GAKU_OK &&
                result.state == GAKU_FILE_DIALOG_CANCELLED, "startup failure left queued dialog pending");
            Check(dialogs.release(dialogs.context, ticket) == GAKU_OK, "release cancelled startup ticket");
            CheckDialogsStopped();
            UnregisterClassW(conflict.lpszClassName, conflict.hInstance);
            puts("PASS: real window registration failure cancels queued dialogs and rejects later requests");
            return 0;
        }
        for (int index = 1; index < argc; ++index) {
            if (std::strcmp(argv[index], "--consumer") == 0 && index + 1 < argc) {
                consumerModule = LoadLibraryA(argv[++index]); Check(consumerModule != nullptr, "load cross-DLL consumer");
                consumerDraw = reinterpret_cast<ConsumerDrawFn>(GetProcAddress(consumerModule, "TestDesktopUiConsumerDraw"));
                Check(consumerDraw != nullptr, "consumer draw export");
            }
        }
        WNDCLASSW cls{}; cls.hInstance = GetModuleHandleW(nullptr); cls.lpfnWndProc = GameProc; cls.lpszClassName = L"UnityWndClass";
        Check(RegisterClassW(&cls) != 0, "synthetic game class");
        game = CreateWindowW(cls.lpszClassName, L"Synthetic game (hidden)", WS_OVERLAPPEDWINDOW,
            0, 0, 450, 800, nullptr, nullptr, cls.hInstance, nullptr);
        Check(game != nullptr, "synthetic game window");
        GakuDesktopUiBinding binding{};
        Check(GakuLoader::DesktopUi::GetService()->bind(&kAbi, sizeof(binding), &binding) == GAKU_E_UNAVAILABLE,
            "binding accepted outside callback");
        const auto* windowService = GakuLoader::DesktopUi::GetWindowService();
        Check(windowService->getWindow() == nullptr && windowService->presentationKind == GAKU_DESKTOP_PRESENTATION_PLUGIN_PANEL,
            "desktop window service contract");
        GakuLoader::DesktopUi::TestHooks hooks{true, InspectPixel,
            [] { ++cpuFrames; ServiceSelectionClick(); }, [] { ++presentedFrames; }};
        hooks.clipboardGet = FakeClipboardGet; hooks.clipboardSet = FakeClipboardSet;
        hooks.clipboardSequence = [] { return clipboardSequence.load(); };
        GakuLoader::DesktopUi::ConfigureTest(hooks);
        Check(GakuLoader::DesktopUi::Initialize(), "desktop worker startup");
        // F1 and the manager page are the new default view; a plugin panel is
        // only composited after it is selected in the navigation column.
        SelectPluginPanel();
        WaitUntil([] { return inspectedFrames.load() >= 3; }, "standalone WARP frames not observed");
        const HWND desktop = static_cast<HWND>(windowService->getWindow());
        Check(desktop && desktop != game && IsWindow(desktop), "desktop owner is not a separate HWND");
        DWORD process = 0;
        Check(GetWindowThreadProcessId(desktop, &process) != GetCurrentThreadId() && process == GetCurrentProcessId(),
            "desktop window is not owned by its dedicated UI thread");
        Check(GetWindow(desktop, GW_OWNER) == nullptr, "desktop unexpectedly owned by game window");
        Check(reinterpret_cast<WNDPROC>(GetWindowLongPtrW(game, GWLP_WNDPROC)) == GameProc, "game window subclassed");
        Check(!IsWindowVisible(desktop), "test desktop should start hidden");
        puts("PASS: independent top-level desktop HWND/device/UI thread, plugin pixels, context/allocator ABI, game WndProc untouched");
        if (consumerDraw) puts("PASS: MinGW-built ImGui consumer renders pixels in the MSVC host");

        CheckShellDropFilters(desktop, game);
        puts("PASS: both Shell file-drop UIPI exceptions are configured on the desktop HWND only");

        Check(GakuLoader::DesktopUi::SetFileDropTarget("gaku.test", nullptr) == GAKU_E_UNAVAILABLE,
            "file-drop target accepted outside the UI callback");
        GakuFileDropResult outsideDrop{};
        Check(GakuLoader::DesktopUi::PollFileDrop("gaku.test", sizeof(outsideDrop), &outsideDrop, 0, nullptr) == GAKU_E_UNAVAILABLE,
            "file-drop poll accepted outside the UI callback");
        SetWindowPos(desktop, nullptr, 310, 180, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        fileDropArmed = true;
        WaitUntil([] { return fileDropTargetFrames >= 2; }, "native file-drop target was not rendered");
        POINT screenOrigin{};
        Check(ClientToScreen(desktop, &screenOrigin) && screenOrigin.x != 0 && screenOrigin.y != 0,
            "native file-drop fixture needs a nonzero window origin");
        SendNativeDrop(desktop, POINT{5, 5});
        const auto outsideDropFrame = presentedFrames.load();
        WaitUntil([&] { return presentedFrames >= outsideDropFrame + 3; }, "outside file-drop observation frames");
        Check(nativeDropBatches == 0, "native file-drop outside the registered content was accepted");
        SendNativeDrop(desktop, POINT{fileDropX.load(), fileDropY.load()});
        WaitUntil([] { return nativeDropBatches == 1; }, "native file-drop inside the content did not arrive");
        const auto deliveredDropFrame = presentedFrames.load();
        WaitUntil([&] { return presentedFrames >= deliveredDropFrame + 3; }, "exactly-once file-drop observation frames");
        Check(nativeDropBatches == 1, "native file-drop batch was delivered more than once");
        SendNativeDrop(desktop, POINT{-5, -5}, true);
        const auto outsideNcFrame = presentedFrames.load();
        WaitUntil([&] { return presentedFrames >= outsideNcFrame + 3; }, "outside non-client drop observation frames");
        Check(nativeDropBatches == 1, "outside screen-coordinate drop was accepted");
        SendNativeDrop(desktop, POINT{fileDropX.load(), fileDropY.load()}, true);
        WaitUntil([] { return nativeDropBatches == 2; }, "screen-coordinate drop inside target did not arrive");
        const auto deliveredNcFrame = presentedFrames.load();
        WaitUntil([&] { return presentedFrames >= deliveredNcFrame + 3; }, "non-client exactly-once observation frames");
        Check(nativeDropBatches == 2, "screen-coordinate batch was delivered more than once");
        fileDropArmed = false;
        puts("PASS: production WM_DROPFILES routes client and screen coordinates once; outside client/target refused");

        expectPluginPixels = false;
        rejectFrames = true;
        WaitUntil([] { return rejectedFrames >= 1; }, "busy-frame denial not exercised");
        const auto cpuBefore = cpuFrames.load(), drawBefore = inspectedFrames.load(), presentBefore = presentedFrames.load();
        const auto pluginBefore = frames.load();
        const auto rejectedBefore = rejectedFrames.load();
        WaitUntil([&] { return rejectedFrames >= rejectedBefore + 12; }, "repeated busy frames not observed");
        Check(cpuFrames == cpuBefore && inspectedFrames == drawBefore && presentedFrames == presentBefore,
            "busy plugin page replaced its last complete image");
        Check(frames == pluginBefore, "busy plugin gate dispatched a plugin callback");
        Check(windowService->requestShow(windowService->context) == GAKU_OK, "busy manager show request refused");
        WaitUntil([&] { return presentedFrames >= presentBefore + 12 && cpuFrames >= cpuBefore + 12; },
            "manager stopped drawing and presenting while plugin callbacks were busy");
        Check(frames == pluginBefore, "busy manager dispatched a plugin callback");
        const auto resumePresent = presentedFrames.load();
        rejectFrames = false;
        Check(GakuLoader::DesktopUi::RequestShowPanel("gaku.test") == GAKU_OK, "plugin restore after busy manager refused");
        WaitUntil([&] { return frames >= pluginBefore + 3 && presentedFrames >= resumePresent + 3; },
            "plugin drawing did not resume after callback gate opened");
        CheckWorker();
        puts("PASS: busy plugin page retains complete image; Manager paints 12+ frames; plugin drawing resumes when gate opens");

        rejectFrames = true;
        const auto deniedBeforeClipboard = rejectedFrames.load();
        WaitUntil([&] { return rejectedFrames > deniedBeforeClipboard; }, "clipboard test frame barrier");
        const auto fetchBefore = clipboardGets.load();
        blockClipboard = true; ChangeClipboard("fresh clipboard: 星南"); requestPasteCheck = true;
        rejectFrames = false;
        WaitUntil([] { return clipboardEntered.load(); }, "delayed clipboard backend not entered");
        Check(!buildingFrame && !callbackRunning, "clipboard owner wait still owns CPU/plugin frame");
        releaseClipboard = true;
        WaitUntil([] { return pasteChecks >= 1; }, "fresh clipboard was not available on first paste");
        Check(clipboardGets == fetchBefore + 1, "unchanged sequence caused redundant clipboard OS fetches");
        requestCopy = true;
        WaitUntil([] { return clipboardSets >= 1; }, "clipboard copy did not flush outside the frame gate");
        expectPluginPixels = true;
        puts("PASS: delayed clipboard backend runs outside CPU gate; first paste is fresh and copy flushes without touching system clipboard");

        SendMessageW(desktop, WM_SETFOCUS, 0, 0);
        SendMessageW(desktop, WM_KEYDOWN, 'A', 1 | (MapVirtualKeyW('A', MAPVK_VK_TO_VSC) << 16));
        WaitUntil([] { return keySeen.load(); }, "desktop Win32 input did not reach ImGui");
        SendMessageW(desktop, WM_KEYUP, 'A', 1 | (MapVirtualKeyW('A', MAPVK_VK_TO_VSC) << 16) | (1u << 30) | (1u << 31));
        SetWindowPos(desktop, nullptr, 0, 0, 900, 650, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        RECT rectangle{}; GetClientRect(desktop, &rectangle);
        WaitUntil([&] { return displayWidth == rectangle.right && displayHeight == rectangle.bottom; }, "resize display dimensions stale");
        puts("PASS: independent desktop resize/recreated backbuffer pixel readback and Win32 keyboard input");

        expectPluginPixels = false;
        Check(windowService->requestShow(windowService->context) == GAKU_OK, "asynchronous show request refused");
        WaitUntil([&] { return IsWindowVisible(desktop) != FALSE; }, "show request did not restore desktop");
        SendMessageW(desktop, WM_CLOSE, 0, 0);
        WaitUntil([&] { return !IsWindowVisible(desktop) || IsIconic(desktop); }, "desktop close neither hid nor minimized controls");
        Check(gameCloses == 0 && IsWindow(game), "desktop close reached the game window");
        Check(windowService->requestShow(windowService->context) == GAKU_OK, "reopen request refused");
        WaitUntil([&] { return IsWindowVisible(desktop) && !IsIconic(desktop); }, "desktop could not reopen");

        // The plugin's owner-bound request must choose that exact panel from
        // Manager and must also replace a different explicit selection.
        const auto managerPresent = presentedFrames.load();
        WaitUntil([&] { return presentedFrames >= managerPresent + 3; }, "manager show frames");
        const auto managerFirst = frames.load(), managerOther = otherPanelFrames.load();
        const auto managerStable = presentedFrames.load();
        WaitUntil([&] { return presentedFrames >= managerStable + 3; }, "manager stable frames");
        Check(frames == managerFirst && otherPanelFrames == managerOther, "host show did not select Manager");
        Check(GakuLoader::DesktopUi::RequestShowPanel("missing") == GAKU_E_UNAVAILABLE &&
            GakuLoader::DesktopUi::RequestShowPanel("gaku.legacy") == GAKU_E_UNAVAILABLE &&
            GakuLoader::DesktopUi::RequestShowPanel("gaku.failed") == GAKU_E_UNAVAILABLE &&
            GakuLoader::DesktopUi::RequestShowPanel("gaku.no-panel") == GAKU_E_UNAVAILABLE,
            "invalid owner panel request accepted");
        Check(GakuLoader::DesktopUi::RequestShowPanel("gaku.other") == GAKU_OK, "second plugin show refused");
        WaitUntil([&] { return otherPanelFrames > managerOther; }, "Manager request selected the wrong plugin");
        const auto firstBeforeRequest = frames.load();
        Check(GakuLoader::DesktopUi::RequestShowPanel("gaku.test") == GAKU_OK, "first plugin show refused");
        WaitUntil([&] { return frames > firstBeforeRequest; }, "plugin request preserved another plugin selection");
        const auto otherBeforeRequest = otherPanelFrames.load();
        Check(GakuLoader::DesktopUi::RequestShowPanel("gaku.other") == GAKU_OK, "second plugin reopen refused");
        WaitUntil([&] { return otherPanelFrames > otherBeforeRequest; }, "second plugin request selected the first panel");
        puts("PASS: owner-specific show selects the requested panel from Manager and another plugin; invalid panels refused");
        ShowWindow(desktop, SW_HIDE);
        puts("PASS: controls close/hide and asynchronous reopen preserve the game window");

        GakuLoader::DesktopUi::Shutdown(); stopped = true;
        CheckDialogsStopped();
        const auto before = frames.load(); Sleep(50); CheckWorker();
        Check(frames == before && !IsWindow(desktop) && windowService->getWindow() == nullptr, "desktop worker/window survived shutdown");
        Check(windowService->requestShow(windowService->context) == GAKU_E_SHUTTING_DOWN, "show accepted after shutdown");
        Check(GakuLoader::DesktopUi::GetService()->bind(&kAbi, sizeof(binding), &binding) == GAKU_E_UNAVAILABLE,
            "binding available after shutdown");
        Check(reinterpret_cast<WNDPROC>(GetWindowLongPtrW(game, GWLP_WNDPROC)) == GameProc, "game WndProc changed on desktop shutdown");
        DestroyWindow(game); game = nullptr;
        if (consumerModule) FreeLibrary(consumerModule);
        puts("PASS: UI-thread presentation teardown; game remains independent");
        puts("Standalone desktop UI WARP smoke PASS");
        return 0;
    } catch (const std::exception& problem) {
        std::fprintf(stderr, "Standalone desktop UI WARP smoke FAIL: %s\n", problem.what());
        GakuLoader::DesktopUi::Shutdown();
        if (game) DestroyWindow(game);
        return 1;
    }
}
