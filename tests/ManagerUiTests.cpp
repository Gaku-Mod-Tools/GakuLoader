// Drive the production manager through real ImGui mouse/key events on a hidden
// WARP desktop. Hooks only observe widget rectangles; no manager state setters.
#include "DesktopUi.hpp"
#include "Host.hpp"
#include "Hotkeys.hpp"
#include "LanguageSettings.hpp"
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Item { std::string value; float left, top, right, bottom; unsigned frame; };
std::filesystem::path configPath, evidenceRoot;
std::mutex itemMutex, inputMutex, screenshotMutex, errorMutex;
std::map<std::string, Item> items;
std::deque<std::function<void()>> inputs;
std::filesystem::path screenshot;
std::string workerError;
std::atomic_uint cpuFrames{0}, presentedFrames{0}, completedInput{0}, screenshots{0};
std::atomic<float> rasterSize{0}, bitmapScale{0}, background{0};
std::atomic_bool callbackGateBusy{false};
std::atomic<double> spinnerTime{0};
unsigned nextInput = 0;
int hotkeyOwner = 0;
void Check(bool okay, const char* message) { if (!okay) throw std::runtime_error(message); }
void Fail(const char* message) { std::lock_guard lock(errorMutex); if (workerError.empty()) workerError = message; }
void CheckWorker() { std::lock_guard lock(errorMutex); Check(workerError.empty(), workerError.c_str()); }
template<class Predicate> void Wait(Predicate predicate, const std::string& message) {
    const auto deadline = GetTickCount64() + 20000;
    while (!predicate() && GetTickCount64() < deadline) { CheckWorker(); Sleep(2); }
    CheckWorker(); Check(predicate(), message.c_str());
}
void BeginFrame() {
    ++cpuFrames;
    std::function<void()> input;
    { std::lock_guard lock(inputMutex); if (!inputs.empty()) { input = std::move(inputs.front()); inputs.pop_front(); } }
    if (input) input();
}
void InspectItem(const char* key, const char* value, float left, float top, float right, float bottom) {
    if (std::string(key) == "spinner.test.waiting") spinnerTime = ImGui::GetTime();
    { std::lock_guard lock(itemMutex); items[key] = {value, left, top, right, bottom, cpuFrames.load()}; }
    rasterSize = ImGui::GetIO().FontDefault->FontSize;
    bitmapScale = ImGui::GetIO().FontGlobalScale;
    background = ImGui::GetStyle().Colors[ImGuiCol_WindowBg].x;
}
bool HasFresh(const std::string& key, const char* value = nullptr) {
    std::lock_guard lock(itemMutex);
    const auto found = items.find(key);
    return found != items.end() && found->second.frame + 2 >= cpuFrames.load() &&
        (!value || found->second.value == value);
}
void RunInput(std::vector<std::function<void()>> steps) {
    const unsigned sequence = ++nextInput, before = presentedFrames.load();
    {
        std::lock_guard lock(inputMutex);
        for (auto& step : steps) inputs.push_back(std::move(step));
        inputs.push_back([sequence] { completedInput = sequence; });
    }
    Wait([&] { return completedInput >= sequence && presentedFrames >= before + steps.size() + 3; }, "synthetic input did not finish");
}
void Click(const std::string& key) {
    Wait([&] { return HasFresh(key); }, "widget unavailable: " + key);
    Item item;
    { std::lock_guard lock(itemMutex); item = items.at(key); }
    const float x = (item.left + item.right) * 0.5f, y = (item.top + item.bottom) * 0.5f;
    RunInput({[=] { ImGui::GetIO().AddMousePosEvent(x, y); },
        [=] { ImGui::GetIO().AddMousePosEvent(x, y); ImGui::GetIO().AddMouseButtonEvent(0, true); },
        [=] { ImGui::GetIO().AddMousePosEvent(x, y); ImGui::GetIO().AddMouseButtonEvent(0, false); }});
}
void SelectTab(const char* name) {
    const std::string key = std::string("tab.") + name;
    Click(key);
    Wait([&] { return HasFresh(key, "selected"); }, "tab failed to select: " + key);
    const auto frame = cpuFrames.load();
    Wait([&] { return cpuFrames >= frame + 3; }, "tab failed to remain selected");
    Check(HasFresh(key, "selected"), "selected manager tab changed without input");
}
std::wstring Config(const wchar_t* section, const wchar_t* key, const wchar_t* fallback = L"") {
    wchar_t value[256]{};
    GetPrivateProfileStringW(section, key, fallback, value, ARRAYSIZE(value), configPath.c_str());
    return value;
}
void Screenshot(const char* name) {
    const auto before = screenshots.load();
    { std::lock_guard lock(screenshotMutex); screenshot = evidenceRoot / name; }
    Wait([&] { return screenshots > before; }, "WARP screenshot did not finish");
}
template<class T> void Release(T*& object) { if (object) { object->Release(); object = nullptr; } }
void InspectFrame(void* rawDevice, void* rawContext, void* rawSwapchain) {
    std::filesystem::path destination;
    { std::lock_guard lock(screenshotMutex); destination = std::move(screenshot); screenshot.clear(); }
    if (destination.empty()) return;
    ID3D11Texture2D *buffer = nullptr, *staging = nullptr;
    auto* device = static_cast<ID3D11Device*>(rawDevice);
    auto* context = static_cast<ID3D11DeviceContext*>(rawContext);
    try {
        auto* chain = static_cast<IDXGISwapChain*>(rawSwapchain);
        Check(SUCCEEDED(chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&buffer))), "WARP screenshot backbuffer");
        D3D11_TEXTURE2D_DESC description{}; buffer->GetDesc(&description);
        description.Usage = D3D11_USAGE_STAGING; description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        description.BindFlags = description.MiscFlags = 0;
        Check(SUCCEEDED(device->CreateTexture2D(&description, nullptr, &staging)), "WARP screenshot staging");
        context->CopyResource(staging, buffer);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Check(SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)), "WARP screenshot map");
        const DWORD stride = (description.Width * 3 + 3) & ~3u;
        std::vector<unsigned char> pixels(static_cast<size_t>(stride) * description.Height);
        for (UINT y = 0; y < description.Height; ++y) {
            const auto* source = static_cast<const unsigned char*>(mapped.pData) + mapped.RowPitch * y;
            auto* row = pixels.data() + stride * y;
            for (UINT x = 0; x < description.Width; ++x) {
                row[x * 3] = source[x * 4 + 2]; row[x * 3 + 1] = source[x * 4 + 1]; row[x * 3 + 2] = source[x * 4];
            }
        }
        context->Unmap(staging, 0);
        BITMAPFILEHEADER header{}; header.bfType = 0x4D42;
        header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
        header.bfSize = header.bfOffBits + static_cast<DWORD>(pixels.size());
        BITMAPINFOHEADER info{}; info.biSize = sizeof(info); info.biWidth = description.Width;
        info.biHeight = -static_cast<LONG>(description.Height); info.biPlanes = 1; info.biBitCount = 24; info.biCompression = BI_RGB;
        std::ofstream file(destination, std::ios::binary);
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        file.write(reinterpret_cast<const char*>(&info), sizeof(info));
        file.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
        Check(file.good(), "WARP screenshot file write");
        ++screenshots;
    } catch (const std::exception& problem) { Fail(problem.what()); }
    Release(staging); Release(buffer);
}
void ReplaceComboText(const char* text) {
    Click("editor.Combo");
    RunInput({[] { auto& io = ImGui::GetIO(); io.AddKeyEvent(ImGuiMod_Ctrl, true); io.AddKeyEvent(ImGuiKey_A, true); },
        [] { auto& io = ImGui::GetIO(); io.AddKeyEvent(ImGuiKey_A, false); io.AddKeyEvent(ImGuiMod_Ctrl, false); },
        [value = std::string(text)] { ImGui::GetIO().AddInputCharactersUTF8(value.c_str()); }});
    Wait([&] { return HasFresh("editor.Combo", text); }, "hotkey text input did not replace combo");
}
}

namespace GakuLoader {
std::filesystem::path ConfigPath() { return configPath; }
void LogInfo(const char* format, ...) { va_list args; va_start(args, format); vprintf(format, args); puts(""); va_end(args); }
void LogWarn(const char* format, ...) { va_list args; va_start(args, format); vprintf(format, args); puts(""); va_end(args); }
void LogError(const char* format, ...) { va_list args; va_start(args, format); vprintf(format, args); puts(""); va_end(args); }
void LogDebug(const char*, ...) {}
bool TryDispatchDesktopFrame(void (*build)(void*), void* data) {
    if (callbackGateBusy) return false;
    build(data); return true;
}
bool SnapshotDesktopHost(DesktopHostSnapshot& output) {
    if (callbackGateBusy) return false;
    output.plugins = {{"test.waiting", "Waiting plugin", "1.0", true, false, false, false},
        {"test.failed", "Failed plugin", "1.0", true, true, true, false},
        {"test.ready", "Ready plugin", "1.0", true, true, false, false}};
    output.pluginFiles = {L"test-ready.dll"};
    output.runtimeReady = false; output.shuttingDown = false; output.activated = true;
    return true;
}
void DispatchDesktopPanel(const char*) { Fail("manager test unexpectedly dispatched a plugin panel"); }
}

int main(int argc, char** argv) {
    using namespace GakuLoader;
    try {
        evidenceRoot = argc == 2 ? std::filesystem::path(argv[1]) : std::filesystem::path("build") / "manager-ui-tests";
        evidenceRoot = std::filesystem::absolute(evidenceRoot) / std::to_wstring(GetCurrentProcessId());
        std::filesystem::create_directories(evidenceRoot);
        configPath = evidenceRoot / "gakuloader.ini";
        Check(WritePrivateProfileStringW(L"appearance", L"theme", L"Dark", configPath.c_str()), "initial theme config");
        Check(WritePrivateProfileStringW(L"appearance", L"scale", L"100", configPath.c_str()), "initial scale config");
        Check(WritePrivateProfileStringW(L"appearance", L"language", L"en", configPath.c_str()), "initial language config");
        LanguageSettings::Load(configPath);
        Hotkeys::LoadOverrides(configPath);
        const GakuHotkeyBinding hotkey{GAKU_SERVICE_VERSION, sizeof(GakuHotkeyBinding), "toggle", "Toggle fixture", 'M', GAKU_HOTKEY_MOD_CTRL, [](void*) {}, nullptr};
        Check(Hotkeys::Register(&hotkeyOwner, "test.ready", &hotkey) == GAKU_OK, "register manager fixture hotkey");
        DesktopUi::TestHooks hooks{true, InspectFrame, BeginFrame, [] { ++presentedFrames; }};
        hooks.inspectUiItem = InspectItem;
        DesktopUi::ConfigureTest(hooks);
        Check(DesktopUi::Initialize(), "manager UI worker creation");
        Wait([] { return presentedFrames >= 3 && HasFresh("tab.PluginList", "selected"); }, "Plugin List is not the default manager tab");
        const auto initial = cpuFrames.load();
        Wait([&] { return cpuFrames >= initial + 4; }, "default manager selection frames");
        Check(HasFresh("tab.PluginList", "selected"), "default Plugin List selection was unstable");
        Check(HasFresh("spinner.test.waiting") && HasFresh("spinner.test.failed") && !HasFresh("spinner.test.ready"),
            "spinner must follow the displayed state, including failed after runtime ready");
        Check(rasterSize == 18 && bitmapScale == 1.0f, "initial font rasterization");
        Screenshot("plugin-list-dark.bmp");

        // Initialization owns the plugin callback gate. The manager must still
        // build and present new frames using the last copied status, so the
        // actual ImGui clock driving DrawSpinner keeps advancing.
        callbackGateBusy = true;
        const auto busyFrame = presentedFrames.load();
        const auto previousSpinnerTime = spinnerTime.load();
        Wait([&] { return presentedFrames >= busyFrame + 12; }, "manager stopped painting while plugin callbacks were busy");
        Check(HasFresh("spinner.test.waiting") && spinnerTime > previousSpinnerTime + 0.05,
            "waiting spinner stopped animating while plugin callbacks were busy");
        callbackGateBusy = false;
        puts("PASS: manager presents 12+ frames and advances spinner animation while plugin callback gate is busy");

        SelectTab("Appearance");
        for (const auto& [index, tag] : {std::pair{1, L"Sapphire"}, std::pair{3, L"AppleLight"}, std::pair{4, L"AppleDark"}}) {
            Click("combo.Theme"); Click("theme." + std::to_string(index));
            Wait([&] { return Config(L"appearance", L"theme") == tag; }, "merged theme did not apply and save");
            GakuUiAppearanceV1 published{};
            Check(DesktopUi::GetAppearanceService()->getAppearance(sizeof(published), &published) == GAKU_OK &&
                static_cast<int>(published.theme) == index, "merged theme was not published to plugins");
        }
        Click("combo.Theme"); Click("theme.2");
        Wait([] { return background > 0.8f && Config(L"appearance", L"theme") == L"PaperAndInk"; }, "Paper and Ink theme was not applied and saved");
        Click("combo.Scale"); Click("scale.150");
        Wait([] { return rasterSize == 27 && bitmapScale == 1.0f && Config(L"appearance", L"scale") == L"150"; }, "150% scale did not rebuild native-size font pixels");
        // A synthetic monitor transition exceeds the atlas cap. Retain the
        // working raster, but remember actual DPI so a smaller UI scale can
        // recover at the new monitor density instead of using stale DPI.
        const auto window = static_cast<HWND>(DesktopUi::GetWindowService()->getWindow());
        RECT bounds{}; GetWindowRect(window, &bounds);
        auto dpiFrame = cpuFrames.load();
        SendMessageW(window, WM_DPICHANGED, MAKEWPARAM(384, 384), reinterpret_cast<LPARAM>(&bounds));
        Wait([&] { return cpuFrames >= dpiFrame + 4; }, "rejected DPI transition did not paint");
        Check(rasterSize == 27 && Config(L"appearance", L"scale") == L"150", "rejected DPI transition changed the working raster");
        Click("combo.Scale"); Click("scale.75");
        Wait([] { return rasterSize == 54 && Config(L"appearance", L"scale") == L"75"; }, "smaller UI scale did not recover using actual monitor DPI");
        dpiFrame = cpuFrames.load();
        SendMessageW(window, WM_DPICHANGED, MAKEWPARAM(96, 96), reinterpret_cast<LPARAM>(&bounds));
        Wait([&] { return cpuFrames >= dpiFrame + 4 && rasterSize == 14; }, "DPI restore did not rerasterize");
        Click("combo.Scale"); Click("scale.150");
        Wait([] { return rasterSize == 27 && Config(L"appearance", L"scale") == L"150"; }, "UI scale restore failed");
        GakuUiAppearanceV1 appearance{};
        Check(DesktopUi::GetAppearanceService()->getAppearance(sizeof(appearance), &appearance) == GAKU_OK &&
            appearance.theme == GAKU_UI_THEME_PAPER_AND_INK && std::fabs(appearance.uiScale - 1.5f) < 0.001f, "published plugin appearance differs from manager");
        for (uint32_t language : {1u, 2u, 3u, 0u}) {
            Click("combo.Language"); Click("language." + std::to_string(language));
            Wait([&] { return LanguageSettings::Current() == language && Config(L"appearance", L"language") == LanguageSettings::kTags[language]; }, "language did not apply and persist");
            GakuUiLanguageV1 published{};
            Check(LanguageSettings::kService.getLanguage(sizeof(published), &published) == GAKU_OK && published.language == language,
                "published plugin language differs from manager");
            const auto frame = cpuFrames.load();
            Wait([&] { return cpuFrames >= frame + 2; }, "translated UI did not redraw");
            const std::string image = "appearance-language-" + std::to_string(language) + "-150.bmp";
            Screenshot(image.c_str());
        }
        Screenshot("appearance-light-150.bmp");

        SelectTab("Settings");
        Click("settings.Console");
        Wait([] { return Config(L"host", L"console") == L"true"; }, "Console checkbox did not save next-start configuration");
        Click("settings.Trace");
        Wait([] { return Config(L"diagnostics", L"trace") == L"true"; }, "trace checkbox did not save configuration");
        Click("settings.Plugin");
        Wait([] { return Config(L"plugins", L"test-ready.dll") == L"false"; }, "plugin checkbox did not save next-start enable state");
        Screenshot("settings-light-150.bmp");

        SelectTab("Hotkeys");
        Click("hotkey.Combo");
        Wait([] { return HasFresh("editor.Combo", "Ctrl+M"); }, "hotkey combo did not open its editor");
        Click("editor.Cancel");
        Check(Hotkeys::Snapshot().at(0).combo == "Ctrl+M", "cancel changed hotkey binding");
        Click("hotkey.Edit"); ReplaceComboText("Ctrl+K"); Click("editor.Apply");
        Wait([] { return Hotkeys::Snapshot().at(0).combo == "Ctrl+K" && Config(L"hotkeys", L"v2:746573742e7265616479:746f67676c65") == L"Ctrl+K"; }, "hotkey editor did not apply and save combo");
        Click("hotkey.Unbind");
        Wait([] { return Hotkeys::Snapshot().at(0).unbound && Config(L"hotkeys", L"v2:746573742e7265616479:746f67676c65", L"missing").empty(); }, "hotkey unbind did not save empty override");
        Click("hotkey.Reset");
        Wait([] { const auto row = Hotkeys::Snapshot().at(0); return row.combo == "Ctrl+M" && !row.overridden &&
            Config(L"hotkeys", L"v2:746573742e7265616479:746f67676c65", L"missing") == L"<default>"; }, "hotkey reset did not persist the plugin-default marker");
        Screenshot("hotkeys-light-150.bmp");
        SelectTab("PluginList"); Screenshot("plugin-list-light-150.bmp");
        DesktopUi::Shutdown();
        Hotkeys::RetireOwner(&hotkeyOwner);
        CheckWorker();
        std::printf("Manager UI WARP PASS: tabs, light/dark screenshots, 150%% raster, four languages, Console/trace/plugin persistence and hotkey edit/cancel/unbind/reset.\nEvidence: %ls\n", evidenceRoot.c_str());
        return 0;
    } catch (const std::exception& problem) {
        std::fprintf(stderr, "Manager UI WARP FAIL: %s\n", problem.what());
        DesktopUi::Shutdown(); Hotkeys::RetireOwner(&hotkeyOwner); return 1;
    }
}
