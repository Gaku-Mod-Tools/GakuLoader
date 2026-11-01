// Real Windows Shell dialogs. This fixture owns every window and input action;
// it does not load the game, game hooks, ImGui, or plugin modules.
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include "DesktopDialogs.hpp"
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
std::atomic<HWND> host{nullptr};
std::atomic<DWORD> uiThread{0};
std::atomic<bool> stopping{false}, finished{false};
std::atomic<int> mode{0}, observedMode{-1}; // 0=normal, 1=invalid HWND, 2=no Pump
std::atomic<unsigned long long> updates{0}, pumps{0};
struct Evidence { std::string name, detail; };
std::vector<Evidence> passed;
int owners[24]{};

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Predicate> void Wait(Predicate predicate, const char* message, unsigned timeout=12000) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout);
    while (!predicate() && Clock::now() < deadline) Sleep(10);
    Check(predicate(), message);
}
void Pass(const char* name, std::string detail) {
    passed.push_back({name, detail});
    printf("PASS %s: %s\n", name, detail.c_str()); fflush(stdout);
}
std::string Utf8(const std::wstring& text) {
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    Check(count > 0, "UTF-8 conversion failed");
    std::vector<char> value(static_cast<size_t>(count));
    Check(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.c_str(), -1, value.data(), count, nullptr, nullptr) > 0,
          "UTF-8 conversion failed");
    return {value.data()};
}
std::string Json(const std::string& text) {
    std::string result = "\"";
    for (unsigned char c : text) {
        if (c == '\\' || c == '"') { result += '\\'; result += static_cast<char>(c); }
        else if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else if (c < 32) result += '?';
        else result += static_cast<char>(c);
    }
    return result + '"';
}
void Report(const char* path, const std::string& failure) {
    std::ofstream report(path, std::ios::binary);
    report << "{\n  \"passed\":" << (failure.empty() ? "true" : "false")
           << ",\n  \"scope\":\"Real IFileOpenDialog service, fixture-owned desktop HWND, no game runtime\",\n"
           << "  \"cases\":[";
    for (size_t i=0; i<passed.size(); ++i) {
        if (i) report << ',';
        report << "\n    {\"name\":" << Json(passed[i].name) << ",\"evidence\":" << Json(passed[i].detail) << '}';
    }
    report << "\n  ],\n  \"simulatedUpdateCount\":" << updates.load()
           << ",\n  \"completedPumpCount\":" << pumps.load()
           << ",\n  \"failure\":" << Json(failure) << "\n}\n";
}
LRESULT CALLBACK HostProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    // A close only hides this test window. It never targets a game window.
    if (message == WM_CLOSE) { ShowWindow(window, SW_HIDE); return 0; }
    return DefWindowProcW(window, message, wp, lp);
}
void UiMain() {
    uiThread = GetCurrentThreadId();
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = HostProc;
    windowClass.lpszClassName = L"GakuDesktopDialogNativeTest";
    windowClass.hInstance = GetModuleHandleW(nullptr);
    RegisterClassW(&windowClass);
    HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"GakuLoader native file dialog test owner",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 480, 220, nullptr, nullptr, windowClass.hInstance, nullptr);
    host = window;
    if (!window) return;
    ShowWindow(window, SW_SHOWNOACTIVATE);
    while (!stopping) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        const int currentMode = mode;
        observedMode = currentMode;
        if (currentMode != 2) {
            GakuLoader::DesktopDialogs::Pump(currentMode == 1 ? nullptr : window);
            ++pumps;
        }
        Sleep(5);
    }
    DestroyWindow(window); host = nullptr;
    UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
}
void SetMode(int value) {
    mode = value;
    Wait([value] { return observedMode == value; }, "UI thread did not acknowledge Pump mode");
}
struct FindDialogState { std::wstring title; HWND result{}; };
BOOL CALLBACK FindDialog(HWND window, LPARAM data) {
    auto& state = *reinterpret_cast<FindDialogState*>(data);
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId()) return TRUE;
    wchar_t title[512]{}, name[128]{};
    GetWindowTextW(window, title, 512); GetClassNameW(window, name, 128);
    if (state.title == title && wcscmp(name, L"#32770") == 0) { state.result = window; return FALSE; }
    return TRUE;
}
HWND LocateDialog(const std::wstring& title) {
    FindDialogState state{title}; EnumWindows(FindDialog, reinterpret_cast<LPARAM>(&state)); return state.result;
}
HWND AwaitDialog(const std::wstring& title) {
    HWND window = nullptr;
    Wait([&] { window = LocateDialog(title); return window != nullptr; }, "real Shell dialog did not appear");
    Check(GetWindow(window, GW_OWNER) == host.load(), "Shell dialog owner differs from desktop host HWND");
    Check(GetWindowThreadProcessId(window, nullptr) == uiThread.load(), "Shell dialog runs on wrong UI thread");
    printf("DIALOG title=%s hwnd=%p owner=%p thread=%lu\n", Utf8(title).c_str(), window, GetWindow(window, GW_OWNER), uiThread.load());
    fflush(stdout); return window;
}
struct EditSearch { HWND result{}; };
BOOL CALLBACK FindFilenameEdit(HWND window, LPARAM data) {
    wchar_t name[64]{}, parent[64]{};
    GetClassNameW(window, name, 64); GetClassNameW(GetParent(window), parent, 64);
    // Shell filename combo owns its Edit; this excludes the search box.
    if (wcscmp(name, L"Edit") == 0 &&
        (wcscmp(parent, L"ComboBox") == 0 || wcscmp(parent, L"ComboBoxEx32") == 0)) {
        reinterpret_cast<EditSearch*>(data)->result = window; return FALSE;
    }
    return TRUE;
}
void Accept(HWND dialog, const std::wstring& path) {
    Sleep(500); // Allow the Shell namespace view to finish its asynchronous initialization.
    EditSearch search;
    HWND button=nullptr;
    Wait([&] {
        search.result=nullptr;
        EnumChildWindows(dialog, FindFilenameEdit, reinterpret_cast<LPARAM>(&search));
        button=GetDlgItem(dialog, IDOK);
        return search.result && IsWindowVisible(search.result) && IsWindowEnabled(search.result) &&
               button && IsWindowVisible(button) && IsWindowEnabled(button);
    }, "real Shell filename/Open controls did not become ready");
    DWORD_PTR result = 0;
    Check(SendMessageTimeoutW(search.result, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(path.c_str()),
          SMTO_ABORTIFHUNG, 2000, &result) && result, "setting Shell filename failed");
    // The actual Shell Open command resolves/validates the filesystem path.
    Check(PostMessageW(button, BM_CLICK, 0, 0),
          "posting Shell Open failed");
}
void Cancel(HWND dialog) {
    Check(PostMessageW(dialog, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(dialog, IDCANCEL))),
          "posting Shell Cancel failed");
}
GakuFileDialogResult Poll(const GakuFileDialogService& service, uint64_t ticket) {
    GakuFileDialogResult result{};
    Check(service.poll(service.context, ticket, sizeof(result), &result, 0, nullptr) == GAKU_OK, "metadata poll failed");
    return result;
}
GakuFileDialogResult AwaitResult(const GakuFileDialogService& service, uint64_t ticket) {
    GakuFileDialogResult result{};
    Wait([&] { result = Poll(service, ticket); return result.state != GAKU_FILE_DIALOG_PENDING; }, "dialog result timeout");
    return result;
}
uint64_t Begin(const GakuFileDialogService& service, const std::wstring& title, const wchar_t* pattern=L"*.*") {
    const GakuFileFilter filter{L"Fixture file", pattern};
    const GakuFileDialogRequest request{GAKU_SERVICE_VERSION, sizeof(GakuFileDialogRequest), title.c_str(), 1, &filter};
    uint64_t ticket=0;
    Check(service.beginOpen(service.context, &request, &ticket) == GAKU_OK && ticket, "beginOpen failed");
    return ticket;
}
void CheckUnchangedCancel(const GakuFileDialogService& service, uint64_t ticket) {
    GakuFileDialogResult result{};
    char path[64] = "previous selection unchanged";
    Check(service.poll(service.context, ticket, sizeof(result), &result, sizeof(path), path) == GAKU_OK,
          "cancel result copy failed");
    Check(result.state == GAKU_FILE_DIALOG_CANCELLED && result.requiredBytes == 0,
          "cancel did not yield CANCELLED with zero required bytes");
    Check(strcmp(path, "previous selection unchanged") == 0, "cancel modified caller path");
}
void Run(const std::filesystem::path& fixtureRoot) {
    Wait([] { return host.load() != nullptr; }, "desktop host HWND creation failed");
    auto service = GakuLoader::DesktopDialogs::ServiceForOwner(&owners[0]);
    auto other = GakuLoader::DesktopDialogs::ServiceForOwner(&owners[1]);
    Check(service.abiVersion == GAKU_SERVICE_VERSION && service.structSize == sizeof(service), "service ABI header");
    const auto directory = fixtureRoot / L"\u4e2d\u6587 \u76ee\u5f55";
    std::filesystem::create_directories(directory);
    const wchar_t* extensions[]{L"vmd", L"wav", L"gkcamera"};
    for (unsigned i=0; i<3; ++i) {
        const auto file = directory / (std::wstring(L"\u9009\u62e9 \u6587\u4ef6.") + extensions[i]);
        std::ofstream(file, std::ios::binary) << "native chooser fixture only";
        const std::wstring title = L"GakuLoader real Shell selection " + std::to_wstring(i);
        const std::wstring pattern = i == 2 ? L"*.vmd;*.gkcamera" : std::wstring(L"*.") + extensions[i];
        uint64_t ticket = Begin(service, title, pattern.c_str());
        HWND dialog = AwaitDialog(title);
        const auto countBefore = updates.load();
        Sleep(150);
        // Windows' default timer quantum may round Sleep(2) to ~15.6 ms.
        Check(updates.load() > countBefore + 3, "simulated main update stalled while real dialog was modal");
        if (i == 0) {
            GakuFileDialogResult sentinel; memset(&sentinel, 0xA5, sizeof(sentinel));
            const auto original = sentinel;
            Check(other.poll(other.context, ticket, sizeof(sentinel), &sentinel, 0, nullptr) == GAKU_E_INVALID_ARGUMENT &&
                  memcmp(&sentinel, &original, sizeof(sentinel)) == 0, "cross-owner poll changed output or accepted ticket");
            Check(other.release(other.context, ticket) == GAKU_E_INVALID_ARGUMENT, "cross-owner release accepted");
            Check(service.poll(service.context, ticket, sizeof(sentinel)-1, &sentinel, 0, nullptr) == GAKU_E_SIZE &&
                  memcmp(&sentinel, &original, sizeof(sentinel)) == 0, "undersized result modified output");
            uint64_t untouchedTicket=0x12345678;
            const GakuFileFilter filter{L"Any", L"*.*"};
            const GakuFileDialogRequest duplicate{GAKU_SERVICE_VERSION, sizeof(duplicate), L"duplicate", 1, &filter};
            Check(service.beginOpen(service.context, &duplicate, &untouchedTicket) == GAKU_E_CONFLICT && untouchedTicket==0x12345678,
                  "same-owner duplicate ticket accepted or failure output changed");
            Pass("ticket_ownership_and_output_guards", "cross-owner poll/release rejected; duplicate rejected; undersized result untouched");
        }
        Accept(dialog, file.wstring());
        auto result = AwaitResult(service, ticket);
        Check(result.state == GAKU_FILE_DIALOG_ACCEPTED && result.osError == S_OK, "Shell Open did not accept fixture");
        const auto expected = Utf8(file.wstring());
        Check(result.requiredBytes == expected.size()+1, "UTF-8 byte count incorrect");
        GakuFileDialogResult untouched; memset(&untouched, 0xA5, sizeof(untouched));
        const auto initial = untouched;
        std::vector<char> shortPath(result.requiredBytes, 'X');
        Check(service.poll(service.context, ticket, sizeof(untouched), &untouched, result.requiredBytes-1, shortPath.data()) == GAKU_E_SIZE &&
              memcmp(&untouched, &initial, sizeof(untouched)) == 0 && shortPath == std::vector<char>(result.requiredBytes, 'X'),
              "undersized UTF-8 buffer modified outputs");
        std::vector<char> path(result.requiredBytes);
        Check(service.poll(service.context, ticket, sizeof(result), &result, static_cast<uint32_t>(path.size()), path.data()) == GAKU_OK &&
              expected == path.data(), "Shell UTF-8 selected path mismatch");
        Check(service.release(service.context, ticket) == GAKU_OK, "release accepted ticket");
        Wait([&] { return !IsWindow(dialog); }, "accepted Shell window remained");
        Pass((std::string("real_select_")+Utf8(extensions[i])).c_str(), expected + "; owner HWND verified; simulated updates continued");
    }
    for (int i=0; i<2; ++i) {
        const std::wstring title = L"GakuLoader real Shell cancel " + std::to_wstring(i);
        const auto ticket = Begin(service, title);
        HWND dialog=AwaitDialog(title); Cancel(dialog);
        Check(AwaitResult(service, ticket).state == GAKU_FILE_DIALOG_CANCELLED, "real cancel state");
        CheckUnchangedCancel(service, ticket);
        Check(service.release(service.context, ticket)==GAKU_OK, "release cancel ticket");
        Wait([&] { return !IsWindow(dialog); }, "cancelled Shell window remained");
    }
    Pass("real_cancel_and_reopen", "two real modal cancels; selected path unchanged; same owner reopened");
    {
        const auto ticket=Begin(service, L"GakuLoader release active modal");
        HWND dialog=AwaitDialog(L"GakuLoader release active modal");
        Check(service.release(service.context, ticket)==GAKU_OK, "release active request");
        Wait([&] { return !IsWindow(dialog); }, "release failed to close real modal");
        GakuFileDialogResult output{};
        Check(service.poll(service.context, ticket, sizeof(output), &output, 0, nullptr)==GAKU_E_INVALID_ARGUMENT, "released ticket remained visible");
        Pass("release_cancels_real_modal", "ticket removed and STA cancellation timer closed Shell window");
    }
    {
        auto retiring=GakuLoader::DesktopDialogs::ServiceForOwner(&owners[2]);
        const auto ticket=Begin(retiring, L"GakuLoader retire active owner");
        HWND dialog=AwaitDialog(L"GakuLoader retire active owner");
        GakuLoader::DesktopDialogs::CancelOwner(retiring.context);
        Wait([&] { return !IsWindow(dialog); }, "CancelOwner failed to close real modal");
        GakuFileDialogResult output{};
        Check(retiring.poll(retiring.context, ticket, sizeof(output), &output, 0, nullptr)==GAKU_E_INVALID_ARGUMENT, "retired owner retained ticket");
        const GakuFileFilter filter{L"Any",L"*.*"};
        const GakuFileDialogRequest input{GAKU_SERVICE_VERSION,sizeof(input),L"retired",1,&filter};
        uint64_t next=0;
        Check(retiring.beginOpen(retiring.context,&input,&next)==GAKU_E_SHUTTING_DOWN, "retired owner accepted request");
        Pass("retire_owner_cancels_real_modal", "owner permanently rejected after real Shell window closed");
    }
    {
        SetMode(1);
        const auto ticket=Begin(service,L"GakuLoader invalid HWND");
        const auto result=AwaitResult(service,ticket);
        Check(result.state==GAKU_FILE_DIALOG_FAILED && result.osError==static_cast<int32_t>(HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE)),
              "invalid owner HWND HRESULT mismatch");
        Check(service.release(service.context,ticket)==GAKU_OK,"release invalid HWND result");
        SetMode(0);
        Pass("invalid_hwnd_error", "FAILED HRESULT=0x80070578; no dialog shown");
    }
    {
        SetMode(2);
        const auto ticket=Begin(service,L"GakuLoader MTA mismatch");
        HRESULT apartment=E_FAIL;
        std::thread mta([&] {
            apartment=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
            if (SUCCEEDED(apartment)) { GakuLoader::DesktopDialogs::Pump(host.load()); CoUninitialize(); }
        });
        mta.join();
        Check(SUCCEEDED(apartment),"MTA test setup failed");
        const auto result=AwaitResult(service,ticket);
        Check(result.state==GAKU_FILE_DIALOG_FAILED && result.osError==static_cast<int32_t>(RPC_E_CHANGED_MODE),"MTA mismatch HRESULT");
        Check(service.release(service.context,ticket)==GAKU_OK,"release MTA result");
        SetMode(0);
        Pass("non_sta_error", "preinitialized MTA thread returns RPC_E_CHANGED_MODE=0x80010106");
    }
    {
        const auto ticket=Begin(service,L"GakuLoader shutdown active modal");
        HWND dialog=AwaitDialog(L"GakuLoader shutdown active modal");
        GakuLoader::DesktopDialogs::Shutdown();
        CheckUnchangedCancel(service,ticket);
        Wait([&] { return !IsWindow(dialog); },"Shutdown failed to close real modal");
        const GakuFileFilter filter{L"Any",L"*.*"};
        const GakuFileDialogRequest input{GAKU_SERVICE_VERSION,sizeof(input),L"after shutdown",1,&filter};
        uint64_t next=0;
        Check(other.beginOpen(other.context,&input,&next)==GAKU_E_SHUTTING_DOWN,"shutdown accepted new request");
        Check(service.release(service.context,ticket)==GAKU_OK,"release shutdown ticket");
        Pass("shutdown_cancels_real_modal", "real Shell modal closed; path unchanged; new requests rejected");
    }
}
}
namespace GakuLoader {
void LogError(const char* format, ...) {
    va_list arguments; va_start(arguments,format); vprintf(format,arguments); va_end(arguments); puts(""); fflush(stdout);
}
}
int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    const char* report=argc>1?argv[1]:"desktop-dialogs-results.json";
    const auto root=std::filesystem::absolute(argc>2?argv[2]:"desktop-dialog-fixtures");
    // A fixture-only watchdog guarantees a hung Shell modal cannot hang CI.
    std::thread watchdog([] {
        for (int i=0;i<1800 && !finished;++i) Sleep(100);
        if (!finished) { fputs("FAIL fixture watchdog timeout\n",stderr); fflush(stderr); ExitProcess(124); }
    });
    std::thread ui(UiMain);
    std::thread update([] { while (!stopping) { ++updates; Sleep(2); } });
    std::string failure;
    try { Run(root); }
    catch (const std::exception& error) { failure=error.what(); fprintf(stderr,"FAIL %s\n",error.what()); }
    GakuLoader::DesktopDialogs::Shutdown();
    stopping=true;
    // Join retains the live request until its own STA modal cancellation returns.
    ui.join(); update.join();
    Report(report,failure);
    finished=true; watchdog.join();
    printf("RESULT %s cases=%zu simulated_updates=%llu completed_pumps=%llu\n",
           failure.empty()?"PASS":"FAIL",passed.size(),updates.load(),pumps.load());
    return failure.empty()?0:1;
}
