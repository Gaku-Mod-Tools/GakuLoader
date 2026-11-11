// Built twice: a native GameAssembly fixture DLL, then an executable linking
// the actual DiagnosticBoundary.cpp, PluginHost.cpp and MinHook implementation.
#include "GakuTestCompat.hpp"
#include "GakuFileDrop.h"
#include <windows.h>
#include <stdint.h>
#include <cstring>
#include "GakuPlugin.h"

struct FixtureString { int32_t length; const wchar_t* chars; };
enum FixtureMode { Valid, MissingMethod, WrongParameter, WrongResult, InstanceMethod,
                   InvalidSecondTarget, AliasedTargets };

#if defined(GAKU_DIAGNOSTIC_FIXTURE_DLL)
#include <atomic>
namespace {
struct Method {
    void* pointer;
    const char* name;
    const char* result;
    const char* parameters[4];
    int count;
    uint32_t flags;
};
std::atomic<uint32_t> officialLogs{0}, officialExceptions{0}, stringLengths{0}, stringCharacters{0};
std::atomic<uint32_t> managedInvocations{0}, managedLookups{0}, nullFlagOutputs{0}, allocations{0}, frees{0}, shutdowns{0};
int fixtureMode = Valid;
int invalidTarget = 0;
Method logMethod{}, exceptionMethod{};
}
extern "C" __declspec(dllexport) GAKU_NOINLINE
void FixtureOfficialLog(int, int, void*, void*, void*) { officialLogs.fetch_add(1); }
extern "C" __declspec(dllexport) GAKU_NOINLINE
void FixtureOfficialException(void*, void*, void*) { officialExceptions.fetch_add(1); }
extern "C" __declspec(dllexport) void FixtureConfigure(int mode) {
    fixtureMode = mode;
    logMethod = {reinterpret_cast<void*>(&FixtureOfficialLog), "Internal_Log", "System.Void",
        {"UnityEngine.LogType", "UnityEngine.LogOption", "System.String", "UnityEngine.Object"}, 4, 0x10};
    exceptionMethod = {reinterpret_cast<void*>(&FixtureOfficialException), "Internal_LogException", "System.Void",
        {"System.Exception", "UnityEngine.Object", nullptr, nullptr}, 2, 0x10};
    if (mode == WrongParameter) logMethod.parameters[2] = "System.Object";
    if (mode == WrongResult) logMethod.result = "System.Boolean";
    if (mode == InstanceMethod) logMethod.flags = 0;
    if (mode == InvalidSecondTarget) exceptionMethod.pointer = &invalidTarget;
    if (mode == AliasedTargets) exceptionMethod.pointer = logMethod.pointer;
}
extern "C" __declspec(dllexport) void* FixtureClass() { return &fixtureMode; }
extern "C" __declspec(dllexport) uint32_t FixtureCount(uint32_t index) {
    const std::atomic<uint32_t>* values[] = {&officialLogs, &officialExceptions, &stringLengths,
        &stringCharacters, &managedInvocations, &managedLookups, &nullFlagOutputs, &allocations, &frees, &shutdowns};
    return index < 10 ? values[index]->load() : UINT32_MAX;
}
extern "C" __declspec(dllexport) void FixtureNoteShutdown() { shutdowns.fetch_add(1); }
extern "C" __declspec(dllexport) void* il2cpp_class_get_method_from_name(void* klass, const char* name, int count) {
    if (klass != FixtureClass()) return nullptr;
    if (!std::strcmp(name, "Internal_Log") && count == 4) return &logMethod;
    if (!std::strcmp(name, "Internal_LogException") && count == 2 && fixtureMode != MissingMethod) return &exceptionMethod;
    managedLookups.fetch_add(1); // includes any attempted managed Exception.ToString lookup
    return nullptr;
}
extern "C" __declspec(dllexport) const void* il2cpp_method_get_param(const void* method, uint32_t index) {
    const auto* value = static_cast<const Method*>(method);
    return index < static_cast<uint32_t>(value->count) ? value->parameters[index] : nullptr;
}
extern "C" __declspec(dllexport) const void* il2cpp_method_get_return_type(const void* method) {
    return static_cast<const Method*>(method)->result;
}
extern "C" __declspec(dllexport) uint32_t il2cpp_method_get_flags(const void* method, uint32_t* output) {
    if (output) *output = 0; else nullFlagOutputs.fetch_add(1);
    return static_cast<const Method*>(method)->flags;
}
extern "C" __declspec(dllexport) char* il2cpp_type_get_name(const void* type) {
    const auto* name = static_cast<const char*>(type);
    auto* output = static_cast<char*>(HeapAlloc(GetProcessHeap(), 0, std::strlen(name) + 1));
    if (output) { std::strcpy(output, name); allocations.fetch_add(1); }
    return output;
}
extern "C" __declspec(dllexport) void il2cpp_free(void* value) {
    if (value) { frees.fetch_add(1); HeapFree(GetProcessHeap(), 0, value); }
}
extern "C" __declspec(dllexport) int32_t il2cpp_string_length(void* value) {
    stringLengths.fetch_add(1); return static_cast<FixtureString*>(value)->length;
}
extern "C" __declspec(dllexport) const wchar_t* il2cpp_string_chars(void* value) {
    stringCharacters.fetch_add(1); return static_cast<FixtureString*>(value)->chars;
}
extern "C" __declspec(dllexport) void* il2cpp_runtime_invoke(const void*, void*, void**, void**) {
    managedInvocations.fetch_add(1); return nullptr;
}
namespace {
GakuStatus PluginActivate(const GakuHostApi*, void** state) { *state = nullptr; return GAKU_OK; }
void PluginShutdown(void*) {
    const auto module = GetModuleHandleW(L"GameAssembly.dll");
    using Log = void (*)(int, int, void*, void*, void*);
    using Exception = void (*)(void*, void*, void*);
    using Note = void (*)();
    reinterpret_cast<Log>(GetProcAddress(module, "FixtureOfficialLog"))(2, 0, nullptr, nullptr, nullptr);
    // An intentionally invalid opaque exception proves no object dereference or
    // managed ToString path is needed during teardown.
    reinterpret_cast<Exception>(GetProcAddress(module, "FixtureOfficialException"))(reinterpret_cast<void*>(1), nullptr, nullptr);
    reinterpret_cast<Note>(GetProcAddress(module, "FixtureNoteShutdown"))();
}
}
extern "C" __declspec(dllexport) GakuStatus GakuPlugin_Query(uint32_t abi, uint32_t size, GakuPluginDescriptor* output) {
    if (abi != GAKU_ABI_VERSION) return GAKU_E_VERSION;
    if (!output || size < sizeof(*output)) return GAKU_E_SIZE;
    *output = {GAKU_ABI_VERSION, sizeof(*output), "gaku.diagnostic-fixture", "Diagnostic fixture", "1", PluginActivate,
        nullptr, nullptr, nullptr, nullptr, PluginShutdown};
    return GAKU_OK;
}
#else
#include "DiagnosticBoundary.hpp"
#include "Host.hpp"
#include "DesktopUi.hpp"
#include <minhook.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
using Log = void (*)(int, int, void*, void*, void*);
using Exception = void (*)(void*, void*, void*);
using Configure = void (*)(int);
using Count = uint32_t (*)(uint32_t);
HMODULE fixture = nullptr;
uint32_t foreignCalls = 0;
Exception foreignOriginal = nullptr;
void Require(bool result, const char* message) {
    if (!result) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void ForeignException(void* error, void* context, void* method) {
    ++foreignCalls; foreignOriginal(error, context, method);
}
size_t Occurrences(const std::string& text, const char* needle) {
    size_t count = 0, position = 0;
    while ((position = text.find(needle, position)) != std::string::npos) { ++count; position += std::strlen(needle); }
    return count;
}
std::string Read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
}
// The IL2CPP export calls and native patching are real. Only class discovery
// and the unrelated D3D UI surface are replaced by deterministic fixtures.
namespace GakuLoader {
void* ResolveIl2cppClass(const char* assembly, const char* namespaze, const char* name) {
    Require(!std::strcmp(assembly, "UnityEngine.CoreModule.dll") && !std::strcmp(namespaze, "UnityEngine") &&
        !std::strcmp(name, "DebugLogHandler"), "only the exact diagnostic class may be resolved");
    return reinterpret_cast<void* (*)()>(GetProcAddress(fixture, "FixtureClass"))();
}
namespace DesktopUi {
bool Initialize() { return true; }
void Shutdown() {}
const GakuDesktopUiService* GetService() { return nullptr; }
const GakuDesktopWindowService* GetWindowService() { return nullptr; }
const GakuAppearanceService* GetAppearanceService() { return nullptr; }
GakuStatus SetFileDropTarget(const char*, const GakuFileDropTarget*) { return GAKU_E_UNAVAILABLE; }
GakuStatus PollFileDrop(const char*, uint32_t, GakuFileDropResult*, uint32_t, char*) { return GAKU_E_UNAVAILABLE; }
GakuStatus PeekFileDrag(const char*, uint32_t, GakuFileDragState*, uint32_t, char*) { return GAKU_E_UNAVAILABLE; }
GakuStatus RequestShowPanel(const char*) { return GAKU_E_UNAVAILABLE; }
void ToggleManager() {}
bool WantsTextInput() { return false; }
}
namespace DesktopDialogs {
GakuFileDialogService ServiceForOwner(void*) { return {}; }
void CancelOwner(void*) {}
}
}

int wmain(int argc, wchar_t** argv) {
    Require(argc == 3, "expected test root and scenario");
    const std::filesystem::path root(argv[1]);
    const std::wstring scenario(argv[2]);
    const auto local = root / scenario / L"GakuLoader";
    fixture = LoadLibraryW((root / L"GameAssembly.dll").c_str());
    Require(fixture != nullptr, "fixture GameAssembly loaded");
    const auto configure = reinterpret_cast<Configure>(GetProcAddress(fixture, "FixtureConfigure"));
    const auto count = reinterpret_cast<Count>(GetProcAddress(fixture, "FixtureCount"));
    const auto log = reinterpret_cast<Log>(GetProcAddress(fixture, "FixtureOfficialLog"));
    const auto exception = reinterpret_cast<Exception>(GetProcAddress(fixture, "FixtureOfficialException"));
    Require(configure && count && log && exception, "fixture exports present");
    int mode = Valid;
    if (scenario == L"missing") mode = MissingMethod;
    else if (scenario == L"parameter") mode = WrongParameter;
    else if (scenario == L"return") mode = WrongResult;
    else if (scenario == L"instance") mode = InstanceMethod;
    else if (scenario == L"rollback") mode = InvalidSecondTarget;
    else if (scenario == L"aliased") mode = AliasedTargets;
    else Require(scenario == L"success" || scenario == L"collision", "known scenario");
    configure(mode);
    const HANDLE standardOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    SetStdHandle(STD_OUTPUT_HANDLE, nullptr); // actual private file logger is still exercised
    GakuLoader::Initialize(root, local);
    Require(GakuLoader::BindMainThread(GetCurrentThreadId()), "fixture main-thread identity");
    std::vector<unsigned char> logBefore(24), exceptionBefore(24);
    std::memcpy(logBefore.data(), reinterpret_cast<void*>(log), logBefore.size());
    std::memcpy(exceptionBefore.data(), reinterpret_cast<void*>(exception), exceptionBefore.size());
    if (scenario == L"collision") {
        Require(MH_Initialize() == MH_OK, "foreign hook backend initialized");
        Require(MH_CreateHook(reinterpret_cast<void*>(exception), reinterpret_cast<void*>(&ForeignException),
            reinterpret_cast<void**>(&foreignOriginal)) == MH_OK, "foreign hook created");
        Require(MH_EnableHook(reinterpret_cast<void*>(exception)) == MH_OK, "foreign hook enabled");
        // MinHook may patch above an adjacent short function. Compare against
        // the baseline with the foreign patch present, never its original bytes.
        std::memcpy(logBefore.data(), reinterpret_cast<void*>(log), logBefore.size());
    }
    const bool installed = GakuLoader::InstallDiagnosticBoundary();
    if (scenario != L"success") {
        Require(!installed, "invalid ABI or hook conflict refuses startup");
        Require(GakuLoader::GetLoadedPlugins().empty(), "no plugin initialized after protection failure");
        log(0, 0, nullptr, nullptr, nullptr);
        exception(reinterpret_cast<void*>(1), nullptr, nullptr);
        Require(count(0) == 1 && count(1) == 1, "rollback restores native official sinks");
        Require(GakuLoader::GetDiagnosticBoundaryCounts().messages == 0 &&
            GakuLoader::GetDiagnosticBoundaryCounts().exceptions == 0, "failed boundary captures nothing");
        Require(!std::memcmp(logBefore.data(), reinterpret_cast<void*>(log), logBefore.size()), "first target bytes restored after failure");
        if (scenario == L"collision") {
            Require(foreignCalls == 1, "foreign collision remains enabled");
            GakuLoader::DisableOwnedHooksForExit();
            exception(nullptr, nullptr, nullptr);
            Require(foreignCalls == 2 && count(1) == 2, "host lease shutdown preserves foreign hook and trampoline");
        } else {
            Require(!std::memcmp(exceptionBefore.data(), reinterpret_cast<void*>(exception), exceptionBefore.size()), "second target bytes untouched after failure");
            // Startup rollback must leave the host lease reusable, with no stale
            // active flag claiming protection when neither target is hooked.
            configure(Valid);
            Require(GakuLoader::InstallDiagnosticBoundary(), "clean retry after failed startup succeeds");
            log(0, 0, nullptr, nullptr, nullptr); exception(reinterpret_cast<void*>(1), nullptr, nullptr);
            Require(count(0) == 1 && count(1) == 1, "retry intercepts both actual native calls");
        }
    } else {
        Require(installed && GakuLoader::InstallDiagnosticBoundary(), "boundary installation is idempotent");
        SYSTEM_INFO system{}; GetSystemInfo(&system);
        auto* guarded = static_cast<unsigned char*>(VirtualAlloc(nullptr, 2 * system.dwPageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        Require(guarded != nullptr, "guarded source allocation");
        DWORD previous = 0;
        Require(VirtualProtect(guarded + system.dwPageSize, system.dwPageSize, PAGE_NOACCESS, &previous) != 0, "source guard page protected");
        auto* chars = reinterpret_cast<wchar_t*>(guarded + system.dwPageSize) - 1024;
        for (size_t i = 0; i < 1024; ++i) chars[i] = L'Z';
        const wchar_t literal[] = L"literal %s %n ";
        std::memcpy(chars, literal, sizeof(literal) - sizeof(wchar_t));
        FixtureString text{1000000, chars}; // reading beyond 1024 wchar crashes at the guard page
        for (int i = 0; i < 130; ++i) log(2, 0, &text, nullptr, reinterpret_cast<void*>(0x1234));
        for (int i = 0; i < 20; ++i) exception(reinterpret_cast<void*>(1), nullptr, reinterpret_cast<void*>(0x5678));
        Require(count(0) == 0 && count(1) == 0, "official originals never called by either diagnostic detour");
        Require(count(2) == 64 && count(3) == 64, "native string reading stops at detail budget");
        Require(count(4) == 0 && count(5) == 0, "no managed invocation or ToString lookup");
        Require(count(6) == 0 && count(7) == count(8), "metadata flags output valid and type names freed");
        auto counts = GakuLoader::GetDiagnosticBoundaryCounts();
        Require(counts.messages == 130 && counts.exceptions == 20, "aggregate counts continue beyond private detail limit");
        const auto logText = Read(local / L"Logs" / L"gakuloader.log");
        Require(Occurrences(logText, "[UnityDiagnostic/local-only] type=") == 64, "exactly 64 private message records");
        Require(Occurrences(logText, "[UnityDiagnostic/local-only] exception captured;") == 8, "exactly 8 private exception records");
        Require(logText.find("literal %s %n ") != std::string::npos, "captured content remains literal text");
        std::filesystem::copy_file(root / L"GameAssembly.dll", local / L"plugins" / L"diagnostic-fixture.dll",
            std::filesystem::copy_options::overwrite_existing);
        GakuLoader::ActivatePlugins();
        GakuLoader::NotifyRuntimeReady();
        Require(GakuLoader::GetLoadedPlugins().size() == 1, "real lifecycle fixture plugin loaded");
        GakuLoader::Shutdown();
        Require(count(9) == 1, "fixture plugin shutdown completed");
        Require(count(0) == 0 && count(1) == 0, "diagnostics remain isolated throughout plugin shutdown callbacks");
        counts = GakuLoader::GetDiagnosticBoundaryCounts();
        Require(counts.messages == 131 && counts.exceptions == 21, "teardown diagnostics were captured");
        GakuLoader::DisableOwnedHooksForExit();
        log(0, 0, nullptr, nullptr, nullptr); exception(nullptr, nullptr, nullptr);
        Require(count(0) == 0 && count(1) == 0, "final host shutdown preserves process-lifetime diagnostic isolation");
        counts = GakuLoader::GetDiagnosticBoundaryCounts();
        Require(counts.messages == 132 && counts.exceptions == 22, "exit diagnostics still enter real native detours");
        VirtualFree(guarded, 0, MEM_RELEASE);
    }
    SetStdHandle(STD_OUTPUT_HANDLE, standardOutput);
    std::printf("PASS: diagnostic boundary %ls (actual native detours and host leases)\n", scenario.c_str());
    return 0;
}
#endif
