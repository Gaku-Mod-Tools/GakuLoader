// Actual PluginHost callback gate + actual Windows Shell modal dialog.
// This fixture creates every native window itself and never opens the game.
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include "Host.hpp"
#include "DesktopDialogs.hpp"
#include "DesktopUi.hpp"
#include "GakuFixtureContract.hpp"
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// Deliberately omit desktop rendering. Link the real host and real file service.
namespace GakuLoader::DesktopUi {
bool Initialize() { return true; }
void Shutdown() {}
const GakuDesktopUiService* GetService() { return nullptr; }
const GakuDesktopWindowService* GetWindowService() { return nullptr; }
const GakuAppearanceService* GetAppearanceService() { return nullptr; }
GakuStatus SetFileDropTarget(const char*, const GakuFileDropTarget*) { return GAKU_E_UNAVAILABLE; }
GakuStatus PollFileDrop(const char*, uint32_t, GakuFileDropResult*, uint32_t, char*) { return GAKU_E_UNAVAILABLE; }
GakuStatus RequestShowPanel(const char*) { return GAKU_E_UNAVAILABLE; }
void ToggleManager() {}
bool WantsTextInput() { return false; }
}
namespace {
std::atomic<HWND> ownerWindow{nullptr};
std::atomic<DWORD> uiThreadId{0};
std::atomic<bool> stopUi{false}, finished{false}, abortTests{false};
std::atomic<unsigned long long> completedPumps{0};
std::mutex errorMutex;
std::string workerFailure;
struct CaseResult { std::string name; unsigned frames, actorTicks, actorA, actorB; unsigned long long elapsedMs; };
std::vector<CaseResult> results;

void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void WorkerFail(const char* message) {
    std::lock_guard lock(errorMutex); if (workerFailure.empty()) workerFailure=message;
}
void CheckWorker() {
    std::lock_guard lock(errorMutex); Check(workerFailure.empty(),workerFailure.c_str());
}
template<class Predicate> void Wait(Predicate predicate,const char* message,unsigned timeout=15000) {
    const auto deadline=GetTickCount64()+timeout;
    while (!predicate() && GetTickCount64()<deadline) {
        CheckWorker();
        MSG item{};
        while (PeekMessageW(&item,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&item); DispatchMessageW(&item); }
        Sleep(5);
    }
    CheckWorker(); Check(predicate(),message);
}
LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    return DefWindowProcW(window,message,wp,lp);
}
void UiThread() {
    // Match the production desktop worker: the service establishes its STA for
    // Show, pairs CoUninitialize, and retains all modal COM work on this thread.
    uiThreadId=GetCurrentThreadId();
    WNDCLASSW type{}; type.lpfnWndProc=WindowProc;
    type.lpszClassName=L"GakuHostDialogConcurrencyOwner"; type.hInstance=GetModuleHandleW(nullptr);
    RegisterClassW(&type);
    HWND window=CreateWindowExW(0,type.lpszClassName,L"GakuLoader host + COM concurrency test",
        WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,480,220,nullptr,nullptr,type.hInstance,nullptr);
    if (!window) { WorkerFail("test host HWND creation failed"); return; }
    ownerWindow=window; ShowWindow(window,SW_SHOWNOACTIVATE);
    while (!stopUi) {
        MSG message{};
        while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        GakuLoader::DesktopDialogs::Pump(window);
        ++completedPumps;
        Sleep(5);
    }
    DestroyWindow(window); ownerWindow=nullptr;
    UnregisterClassW(type.lpszClassName,type.hInstance);
}
struct Search { const std::wstring* title; HWND result{}; };
BOOL CALLBACK LocateWindow(HWND window,LPARAM argument) {
    auto& search=*reinterpret_cast<Search*>(argument);
    DWORD pid=0; GetWindowThreadProcessId(window,&pid);
    if (pid!=GetCurrentProcessId()) return TRUE;
    wchar_t title[256]{}, type[64]{};
    GetWindowTextW(window,title,256); GetClassNameW(window,type,64);
    if (*search.title==title && wcscmp(type,L"#32770")==0) { search.result=window; return FALSE; }
    return TRUE;
}
HWND FindDialog(const std::wstring& title) {
    Search search{&title}; EnumWindows(LocateWindow,reinterpret_cast<LPARAM>(&search)); return search.result;
}
enum class CloseMethod { Release, RetireOwner, Shutdown };
void TestModal(const char* name,const std::wstring& title,const GakuFileDialogService& service,
               CloseMethod close,FixtureSnapshotFn snapshot) {
    const GakuFileFilter filter{L"Motion or camera",L"*.vmd;*.gkcamera"};
    const GakuFileDialogRequest input{GAKU_SERVICE_VERSION,sizeof(input),title.c_str(),1,&filter};
    uint64_t ticket=0;
    Check(service.beginOpen(service.context,&input,&ticket)==GAKU_OK && ticket,"begin actual file service request failed");
    std::atomic<bool> observed{false}, updatesDone{false}, closed{false};
    std::atomic<HWND> activeDialog{nullptr};
    std::thread controller([&] {
        try {
            HWND dialog=nullptr;
            Wait([&] { dialog=FindDialog(title); return dialog!=nullptr || abortTests; },"real IFileOpenDialog did not appear");
            if (abortTests) return;
            Check(GetWindow(dialog,GW_OWNER)==ownerWindow.load(),"real dialog GW_OWNER differs from test desktop HWND");
            Check(GetWindowThreadProcessId(dialog,nullptr)==uiThreadId.load(),"real dialog runs on wrong UI STA thread");
            printf("DIALOG %s hwnd=%p owner=%p thread=%lu\n",name,dialog,GetWindow(dialog,GW_OWNER),uiThreadId.load()); fflush(stdout);
            const auto pumpBeforeClose=completedPumps.load();
            activeDialog=dialog; observed=true;
            Wait([&] { return updatesDone.load() || abortTests; },"actual host dispatches did not finish before modal timeout",20000);
            printf("CANCEL %s begin\n",name); fflush(stdout);
            if (close==CloseMethod::Release) {
                const auto status=service.release(service.context,ticket);
                GakuFileDialogResult result{};
                const auto poll=service.poll(service.context,ticket,sizeof(result),&result,0,nullptr);
                printf("RELEASE status=%ld subsequent_poll=%ld visible=%u owner_enabled=%u completed_pumps=%llu\n",
                       static_cast<long>(status),static_cast<long>(poll),IsWindowVisible(dialog),IsWindowEnabled(ownerWindow.load()),completedPumps.load()); fflush(stdout);
                Check(status==GAKU_OK && poll==GAKU_E_INVALID_ARGUMENT,"release did not cancel/remove actual ticket");
            }
            else if (close==CloseMethod::RetireOwner) GakuLoader::DesktopDialogs::CancelOwner(service.context);
            else GakuLoader::DesktopDialogs::Shutdown();
            Wait([&] { return !IsWindowVisible(dialog) && completedPumps.load()>pumpBeforeClose; },
                 "real modal remained visible or service Pump did not return after cancellation");
            printf("CLOSE state hwnd_alive=%u visible=%u owner_enabled=%u completed_pumps=%llu\n",
                   IsWindow(dialog),IsWindowVisible(dialog),IsWindowEnabled(ownerWindow.load()),completedPumps.load()); fflush(stdout);
            printf("CANCEL %s closed\n",name); fflush(stdout);
            closed=true;
        } catch (const std::exception& problem) {
            fprintf(stderr,"CONTROLLER FAIL %s\n",problem.what()); fflush(stderr);
            WorkerFail(problem.what()); GakuLoader::DesktopDialogs::Shutdown();
        }
    });
    const auto before=snapshot();
    unsigned long long elapsed=0;
    try {
        Wait([&] { return observed.load(); },"controller did not confirm real modal");
        printf("DISPATCH %s begin\n",name); fflush(stdout);
        const auto start=GetTickCount64();
        // Both actors must receive every main-thread rewrite while Show is modal,
        // and the frame pump must keep running with a real OS modal on screen.
        for (unsigned round=0;round<100;++round) {
            // Do not enumerate/GetWindowText during callbacks: same-process
            // WM_GETTEXT is a cross-thread synchronous send. IsWindow only
            // validates the HWND the controller already identified above.
            Check(IsWindow(activeDialog.load()),"real modal disappeared before actor dispatch");
            GakuLoader::TickFrame(.016f);
            GakuLoader::TickActor(reinterpret_cast<void*>(uintptr_t{1}),.016f);
            GakuLoader::TickActor(reinterpret_cast<void*>(uintptr_t{2}),.016f);
            MSG item{};
            while (PeekMessageW(&item,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&item); DispatchMessageW(&item); }
        }
        elapsed=GetTickCount64()-start;
        printf("DISPATCH %s completed elapsed_ms=%llu\n",name,elapsed); fflush(stdout);
        Check(IsWindow(activeDialog.load()),"real modal disappeared during actor dispatches");
        const auto after=snapshot();
        Check(after.actorTickCount-before.actorTickCount==200,"actual host dropped an actor update while Shell modal was open");
        Check(after.tickCount-before.tickCount==100,"actual host dropped a frame pump while Shell modal was open");
        Check(after.actorACount-before.actorACount==100 && after.actorBCount-before.actorBCount==100,
              "actual host A/B actor update count mismatch");
        Check(after.overlapCount==0 && after.contractFailures==0,"actual fixture callback overlap or contract failure");
        Check(after.desktopCount==0,"desktop rendering unexpectedly enabled in modal gate test");
        Check(elapsed<15000,"actual main gate stalled behind modal COM call");
        updatesDone=true;
        Wait([&] { return closed.load(); },"controller did not confirm modal cancellation");
    } catch (...) {
        fputs("DISPATCH exception; cancel and join controller\n",stderr); fflush(stderr);
        abortTests=true; updatesDone=true; GakuLoader::DesktopDialogs::Shutdown();
        controller.join(); throw;
    }
    controller.join(); CheckWorker();
    if (close==CloseMethod::Shutdown) {
        GakuFileDialogResult result{};
        Check(service.poll(service.context,ticket,sizeof(result),&result,0,nullptr)==GAKU_OK &&
              result.state==GAKU_FILE_DIALOG_CANCELLED,"Shutdown ticket was not CANCELLED");
        Check(service.release(service.context,ticket)==GAKU_OK,"release shutdown result failed");
    }
    results.push_back({name,100,200,100,100,elapsed});
    printf("PASS %s actual_frames=100 actor_ticks=200 actor_A=100 actor_B=100 overlaps=0 elapsed_ms=%llu\n",name,elapsed); fflush(stdout);
}
void Report(const std::filesystem::path& path,const std::string& failure) {
    std::ofstream output(path,std::ios::binary);
    output << "{\n  \"passed\":" << (failure.empty()?"true":"false")
           << ",\n  \"scope\":\"Actual GakuLoader::TickFrame/TickActor + fixture DLL + real IFileOpenDialog; no game runtime/rendering\",\n  \"cases\":[";
    for (size_t index=0;index<results.size();++index) {
        if (index) output << ',';
        const auto& result=results[index];
        output << "\n    {\"name\":\"" << result.name << "\",\"actualFrameDelta\":" << result.frames
               << ",\"actualActorTickDelta\":" << result.actorTicks
               << ",\"actorADelta\":" << result.actorA << ",\"actorBDelta\":" << result.actorB
               << ",\"overlapCount\":0,\"elapsedMs\":" << result.elapsedMs << '}';
    }
    // Error messages above are fixed ASCII strings, with no path/user input.
    output << "\n  ],\n  \"failure\":\"" << failure << "\"\n}\n";
}
}
int wmain(int argc,wchar_t** argv) {
    if (argc!=3) { fputs("usage: host-dialog-concurrency.exe fixtureRoot report.json\n",stderr); return 2; }
    std::thread watchdog([] {
        for (unsigned count=0;count<900 && !finished;++count) Sleep(100);
        if (!finished) { fputs("FAIL test-only watchdog timeout\n",stderr); fflush(stderr); ExitProcess(124); }
    });
    std::thread ui(UiThread);
    std::string failure;
    bool initialized=false;
    try {
        const std::filesystem::path root(argv[1]);
        GakuLoader::Initialize(root,root/L"gakumas-local"); initialized=true;
        GakuLoader::ActivatePlugins();
        const auto& plugins=GakuLoader::GetLoadedPlugins();
        Check(plugins.size()==1 && plugins[0].activated,"isolated fixture activation must load exactly one plugin");
        const auto snapshot=reinterpret_cast<FixtureSnapshotFn>(GetProcAddress(plugins[0].handle,"GakuFixture_Snapshot"));
        Check(snapshot!=nullptr,"fixture snapshot export unavailable");
        GakuLoader::BindMainThread(GetCurrentThreadId());
        GakuLoader::NotifyRuntimeReady();
        // The fixture explicitly supplies the confirmed runtime/main thread.
        GakuLoader::TickFrame(.016f);
        GakuLoader::TickActor(nullptr,.016f);
        const auto first=snapshot();
        Check(first.activateCount==1 && first.readyCount==1 && first.tickCount==1 && first.actorTickCount==1 &&
              first.activateThread==GetCurrentThreadId() && first.readyThread==GetCurrentThreadId() &&
              first.tickThread==GetCurrentThreadId(),"fixture did not activate/ready/tick on actual host main thread");
        GakuFileDialogService service{};
        Check(plugins[0].host.queryService(plugins[0].host.hostContext,GAKU_SERVICE_FILE_DIALOG,
              GAKU_SERVICE_VERSION,sizeof(service),&service)==GAKU_OK,"actual host file service query failed");
        Wait([] { return ownerWindow.load()!=nullptr; },"test desktop HWND not ready");
        TestModal("release_modal",L"GakuLoader actual host release modal",service,CloseMethod::Release,snapshot);
        // Separate owner models another plugin retiring without retiring the live
        // fixture's host token before the final process-wide Shutdown case.
        int retiringOwner=0;
        const auto retiring=GakuLoader::DesktopDialogs::ServiceForOwner(&retiringOwner);
        TestModal("retire_owner_modal",L"GakuLoader actual host retire modal",retiring,CloseMethod::RetireOwner,snapshot);
        TestModal("shutdown_modal",L"GakuLoader actual host shutdown modal",service,CloseMethod::Shutdown,snapshot);
        const auto final=snapshot();
        Check(final.tickCount==301 && final.actorTickCount==601 && final.actorACount==300 &&
              final.actorBCount==300 && final.otherActorCount==1,
              "final actual host dispatch totals mismatch");
        Check(final.overlapCount==0 && final.contractFailures==0 && final.desktopCount==0,"final fixture contract failure");
        printf("TOTAL actual_frames=%u actor_ticks=%u actor_A=%u actor_B=%u other_actor=%u overlaps=%u desktop_callbacks=%u\n",
               final.tickCount,final.actorTickCount,final.actorACount,final.actorBCount,final.otherActorCount,
               final.overlapCount,final.desktopCount);
    } catch (const std::exception& problem) { failure=problem.what(); fprintf(stderr,"FAIL %s\n",problem.what()); fflush(stderr); }
    abortTests=true; GakuLoader::DesktopDialogs::Shutdown(); stopUi=true; ui.join();
    if (initialized) GakuLoader::Shutdown();
    Report(argv[2],failure);
    finished=true; watchdog.join();
    printf("RESULT %s cases=%zu\n",failure.empty()?"PASS":"FAIL",results.size());
    return failure.empty()?0:1;
}
