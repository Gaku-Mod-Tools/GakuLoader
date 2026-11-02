#include "Host.hpp"
#include "DesktopUi.hpp"
#include "GakuPaths.h"
#include "GakuPaths.hpp"
#include "PluginPaths.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

// Production PluginHost is linked, but no game, windows or dialogs are started.
namespace GakuLoader::DesktopUi {
bool Initialize() { return true; }
void Shutdown() {}
const GakuDesktopUiService* GetService() { return nullptr; }
const GakuDesktopWindowService* GetWindowService() { return nullptr; }
const GakuAppearanceService* GetAppearanceService() { return nullptr; }
GakuStatus RequestShowPanel(const char*) { return GAKU_E_UNAVAILABLE; }
void ToggleManager() {}
bool WantsTextInput() { return false; }
}
namespace GakuLoader::DesktopDialogs {
GakuFileDialogService ServiceForOwner(void*) { return {}; }
void CancelOwner(void*) {}
}

namespace {
constexpr std::array<GakuPathKind, 6> kKinds{GAKU_PATH_USER_DATA, GAKU_PATH_CONFIGS,
    GAKU_PATH_PLUGIN_DATA, GAKU_PATH_CACHE, GAKU_PATH_LOGS, GAKU_PATH_TEMP};
constexpr std::array<const wchar_t*, 6> kCategories{
    L"UserData", L"Configs", L"PluginData", L"Cache", L"Logs", L"Temp"};
constexpr wchar_t kSentinel = L'\x7b7b';

template<class Getter>
std::filesystem::path CheckGet(Getter get, GakuPathKind kind) {
    uint32_t required = 0;
    assert(get(kind, nullptr, 0, &required) == GAKU_E_SIZE);
    assert(required > 1);
    std::vector<wchar_t> output(required + 4, kSentinel);
    uint32_t shortRequired = 0;
    assert(get(kind, output.data(), required - 1, &shortRequired) == GAKU_E_SIZE);
    assert(shortRequired == required);
    assert(std::all_of(output.begin(), output.end(), [](wchar_t c) { return c == kSentinel; }));
    assert(get(kind, output.data(), 0, &shortRequired) == GAKU_E_SIZE);
    assert(std::all_of(output.begin(), output.end(), [](wchar_t c) { return c == kSentinel; }));
    assert(get(kind, output.data(), required, &shortRequired) == GAKU_OK);
    assert(shortRequired == required && output[required - 1] == L'\0');
    assert(std::all_of(output.begin() + required, output.end(), [](wchar_t c) { return c == kSentinel; }));
    const std::filesystem::path path(output.data());
    assert(path.is_absolute());
    return path;
}

template<class Getter>
void CheckUnchanged(Getter get, GakuPathKind kind, GakuStatus expected) {
    wchar_t output[8];
    std::fill(std::begin(output), std::end(output), kSentinel);
    uint32_t required = 0x12345678u;
    assert(get(kind, output, 8, &required) == expected);
    assert(required == 0x12345678u);
    assert(std::all_of(std::begin(output), std::end(output), [](wchar_t c) { return c == kSentinel; }));
}

void StateChecks(const std::filesystem::path& root) {
    const auto local = root / L"state unicode \u5b66\u5712 \U0001f3b5";
    GakuLoader::PluginPaths::State state;
    state.Configure(local, "unit.valid");
    assert(state.Status() == GAKU_OK);
    auto get = [&state](GakuPathKind kind, wchar_t* out, uint32_t cap, uint32_t* needed) {
        return state.GetRoot(kind, out, cap, needed);
    };
    for (size_t i = 0; i < kKinds.size(); ++i) {
        assert(CheckGet(get, kKinds[i]) == local / kCategories[i] / L"unit.valid");
    }
    assert(!std::filesystem::exists(local)); // querying never creates directories
    CheckUnchanged(get, 0, GAKU_E_INVALID_ARGUMENT);
    CheckUnchanged(get, 7, GAKU_E_INVALID_ARGUMENT);
    uint32_t required = 0x12345678u;
    assert(get(GAKU_PATH_CONFIGS, nullptr, 2, &required) == GAKU_E_INVALID_ARGUMENT);
    assert(required == 0x12345678u);
    wchar_t output[8]; std::fill(std::begin(output), std::end(output), kSentinel);
    assert(get(GAKU_PATH_CONFIGS, output, 8, nullptr) == GAKU_E_INVALID_ARGUMENT);
    assert(std::all_of(std::begin(output), std::end(output), [](wchar_t c) { return c == kSentinel; }));
    std::error_code error;
    assert(state.EnsureRoot(GAKU_PATH_CONFIGS, error) == GAKU_OK);
    assert(!error && std::filesystem::is_directory(local / L"Configs" / L"unit.valid"));
    assert(state.EnsureRoot(GAKU_PATH_CONFIGS, error) == GAKU_OK); // idempotent
    for (size_t i = 0; i < kKinds.size(); ++i) {
        if (kKinds[i] != GAKU_PATH_CONFIGS) assert(!std::filesystem::exists(local / kCategories[i]));
    }

    // A regular file cannot become a category directory. This deterministic
    // filesystem failure works without changing real ACLs or elevated access.
    { std::ofstream blocked(local / L"Logs", std::ios::binary); blocked << "synthetic blocker"; }
    assert(state.EnsureRoot(GAKU_PATH_LOGS, error) == GAKU_E_INTERNAL);
    assert(error && std::filesystem::is_regular_file(local / L"Logs"));
    assert(state.EnsureRoot(0, error) == GAKU_E_INVALID_ARGUMENT);
    state.Retire();
    CheckUnchanged(get, GAKU_PATH_CONFIGS, GAKU_E_SHUTTING_DOWN);
    assert(state.EnsureRoot(GAKU_PATH_USER_DATA, error) == GAKU_E_SHUTTING_DOWN);
    assert(!std::filesystem::exists(local / L"UserData"));

    const std::vector<std::string> invalid{
        "", ".", "..", ".alpha", "alpha.", "alpha..beta", "alpha/beta", "alpha\\beta",
        "alpha:beta", "alpha beta", "alpha ", "Paths.Alpha", "con", "con.txt", "prn",
        "aux", "nul", "com1", "com9.log", "lpt1", "lpt9.ext", "\xc3\xa9", std::string(65, 'a')};
    for (const auto& id : invalid) {
        GakuLoader::PluginPaths::State rejected;
        rejected.Configure(root / L"invalid", id);
        assert(rejected.Status() == GAKU_E_INVALID_ARGUMENT);
        auto badGet = [&rejected](GakuPathKind kind, wchar_t* out, uint32_t cap, uint32_t* needed) {
            return rejected.GetRoot(kind, out, cap, needed);
        };
        CheckUnchanged(badGet, GAKU_PATH_CONFIGS, GAKU_E_INVALID_ARGUMENT);
        assert(rejected.EnsureRoot(GAKU_PATH_CONFIGS, error) == GAKU_E_INVALID_ARGUMENT);
    }
    assert(!std::filesystem::exists(root / L"invalid"));
}

GakuStatus MissingService(void*, const char*, uint32_t, uint32_t, void*) {
    return GAKU_E_UNAVAILABLE;
}

void SdkChecks(const std::filesystem::path& root) {
    GakuPathsV1 untouched;
    std::memset(&untouched, 0x7b, sizeof(untouched));
    const auto original = untouched;
    GakuHostApi oldHost{GAKU_ABI_VERSION, sizeof(GakuHostApi), nullptr, MissingService, nullptr};
    assert(GakuPaths::Query(&oldHost, untouched) == GAKU_E_UNAVAILABLE);
    assert(std::memcmp(&untouched, &original, sizeof(untouched)) == 0);
    assert(GakuPaths::Query(nullptr, untouched) == GAKU_E_INVALID_ARGUMENT);
    assert(std::memcmp(&untouched, &original, sizeof(untouched)) == 0);
    // Only the 8-byte header is readable. The absent function pointer is on an
    // inaccessible page: SDK negotiation must reject size before accessing it.
    SYSTEM_INFO systemInfo{}; GetSystemInfo(&systemInfo);
    auto* allocation = static_cast<unsigned char*>(VirtualAlloc(nullptr,
        static_cast<SIZE_T>(systemInfo.dwPageSize) * 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    assert(allocation);
    DWORD oldProtection = 0;
    assert(VirtualProtect(allocation + systemInfo.dwPageSize, systemInfo.dwPageSize,
        PAGE_NOACCESS, &oldProtection));
    auto* shortHost = reinterpret_cast<GakuHostApi*>(allocation + systemInfo.dwPageSize - sizeof(GakuServiceHeader));
    shortHost->abiVersion = GAKU_ABI_VERSION;
    shortHost->structSize = sizeof(GakuServiceHeader);
    assert(GakuPaths::Query(shortHost, untouched) == GAKU_E_SIZE);
    assert(std::memcmp(&untouched, &original, sizeof(untouched)) == 0);
    assert(VirtualFree(allocation, 0, MEM_RELEASE));

    const std::string utf8 = "music \xe5\xad\xa6\xe5\x9c\x92 \xf0\x9f\x8e\xb5";
    const std::wstring wide = L"music \u5b66\u5712 \U0001f3b5";
    std::wstring decoded;
    std::string encoded;
    assert(GakuPaths::FromUtf8(utf8, decoded) == GAKU_OK && decoded == wide);
    assert(GakuPaths::ToUtf8(wide, encoded) == GAKU_OK && encoded == utf8);
    const std::vector<std::string> badUtf8{
        "\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe5", std::string("a\0b", 3)};
    for (const auto& invalid : badUtf8) {
        decoded = L"unchanged";
        assert(GakuPaths::FromUtf8(invalid, decoded) == GAKU_E_INVALID_ARGUMENT);
        assert(decoded == L"unchanged");
    }
    const std::vector<std::wstring> badUtf16{
        std::wstring(1, static_cast<wchar_t>(0xd800)), std::wstring(1, static_cast<wchar_t>(0xdc00)),
        std::wstring(L"a\0b", 3)};
    for (const auto& invalid : badUtf16) {
        encoded = "unchanged";
        assert(GakuPaths::ToUtf8(invalid, encoded) == GAKU_E_INVALID_ARGUMENT);
        assert(encoded == "unchanged");
    }
    assert(GakuPaths::FromUtf8("", decoded) == GAKU_OK && decoded.empty());
    assert(GakuPaths::ToUtf8(L"", encoded) == GAKU_OK && encoded.empty());

    std::filesystem::path joined = root / L"unchanged";
    const auto sentinel = joined;
    const std::vector<std::wstring> invalidRelative{
        L"", L".", L"..", L"a/../b", L"a/./b", L"/absolute", L"\\absolute", L"//server/share",
        L"C:relative", L"C:/absolute", L"a:b", L"a\\\\b", L"a/", L"a.", L"a ", L"file?.json",
        L"CON", L"aux.txt", L"COM1.txt", L"lpt9.ext", L"COM\u00b9.txt", std::wstring(L"a\0b", 3)};
    for (const auto& relative : invalidRelative) {
        assert(GakuPaths::JoinRelative(root, relative, joined) == GAKU_E_INVALID_ARGUMENT);
        assert(joined == sentinel);
    }
    assert(GakuPaths::JoinRelative(L"relative-root", L"file.json", joined) == GAKU_E_INVALID_ARGUMENT);
    assert(joined == sentinel);
    assert(GakuPaths::JoinRelative(root, L"\u5b66\u5712 music/\U0001f3b5.json", joined) == GAKU_OK);
    assert(joined == root / L"\u5b66\u5712 music" / L"\U0001f3b5.json");
    assert(!std::filesystem::exists(joined)); // helper never performs I/O
}

const GakuLoader::LoadedPlugin& Find(const char* id) {
    const auto& plugins = GakuLoader::GetLoadedPlugins();
    const auto found = std::find_if(plugins.begin(), plugins.end(), [id](const auto& plugin) { return plugin.id == id; });
    assert(found != plugins.end());
    return *found;
}

void HostChecks(const std::filesystem::path& root) {
    const auto local = root / L"gakumas-local";
    GakuLoader::DisableHotkeyPollerForTest();
    GakuLoader::Initialize(root, local);
    GakuLoader::ActivatePlugins();
    assert(GakuLoader::GetLoadedPlugins().size() == 3);
    const auto& alpha = Find("paths.alpha");
    const auto& beta = Find("paths.beta");
    const auto& uppercase = Find("Paths.Alpha");
    for (const auto& plugin : GakuLoader::GetLoadedPlugins()) assert(plugin.activated && !plugin.failed);

    struct Buffer { GakuPathsV1 table; uint64_t guard; } output;
    std::memset(&output, 0x7b, sizeof(output));
    const auto sentinel = output;
    auto query = [&alpha](uint32_t version, uint32_t size, void* out) {
        return alpha.host.queryService(alpha.host.hostContext, GAKU_SERVICE_PATHS, version, size, out);
    };
    assert(query(2, sizeof(output), &output) == GAKU_E_VERSION);
    assert(std::memcmp(&output, &sentinel, sizeof(output)) == 0);
    assert(query(1, sizeof(GakuPathsV1) - 1, &output) == GAKU_E_SIZE);
    assert(std::memcmp(&output, &sentinel, sizeof(output)) == 0);
    assert(query(1, sizeof(output), &output) == GAKU_OK);
    assert(output.guard == sentinel.guard);
    assert(output.table.abiVersion == 1 && output.table.structSize == sizeof(GakuPathsV1));
    auto pathsA = output.table;
    assert(pathsA.context && pathsA.getRoot && pathsA.ensureRoot);
    GakuPathsV1 pathsB{};
    assert(beta.host.queryService(beta.host.hostContext, GAKU_SERVICE_PATHS, 1, sizeof(pathsB), &pathsB) == GAKU_OK);
    assert(pathsA.context != pathsB.context);
    GakuPathsV1 sdkPaths{};
    assert(GakuPaths::Query(&alpha.host, sdkPaths) == GAKU_OK);
    std::filesystem::path sdkRoot;
    assert(GakuPaths::GetRoot(sdkPaths, GAKU_PATH_CONFIGS, sdkRoot) == GAKU_OK);
    assert(sdkRoot == local / L"Configs" / L"paths.alpha");
    output = sentinel;
    assert(uppercase.host.queryService(uppercase.host.hostContext, GAKU_SERVICE_PATHS, 1,
        sizeof(output), &output) == GAKU_E_INVALID_ARGUMENT);
    assert(std::memcmp(&output, &sentinel, sizeof(output)) == 0);

    auto getA = [&pathsA](GakuPathKind kind, wchar_t* out, uint32_t cap, uint32_t* needed) {
        return pathsA.getRoot(pathsA.context, kind, out, cap, needed);
    };
    auto getB = [&pathsB](GakuPathKind kind, wchar_t* out, uint32_t cap, uint32_t* needed) {
        return pathsB.getRoot(pathsB.context, kind, out, cap, needed);
    };
    for (size_t i = 0; i < kKinds.size(); ++i) {
        assert(CheckGet(getA, kKinds[i]) == local / kCategories[i] / L"paths.alpha");
        assert(CheckGet(getB, kKinds[i]) == local / kCategories[i] / L"paths.beta");
        assert(!std::filesystem::exists(local / kCategories[i]));
    }
    CheckUnchanged(getA, 7, GAKU_E_INVALID_ARGUMENT);
    uint32_t needed = 0x12345678u;
    wchar_t untouched[8]; std::fill(std::begin(untouched), std::end(untouched), kSentinel);
    assert(pathsA.getRoot(nullptr, GAKU_PATH_CONFIGS, untouched, 8, &needed) == GAKU_E_INVALID_ARGUMENT);
    assert(needed == 0x12345678u);
    assert(std::all_of(std::begin(untouched), std::end(untouched), [](wchar_t c) { return c == kSentinel; }));
    assert(pathsA.ensureRoot(nullptr, GAKU_PATH_CONFIGS) == GAKU_E_INVALID_ARGUMENT);
    assert(pathsA.ensureRoot(pathsA.context, GAKU_PATH_CONFIGS) == GAKU_OK);
    assert(std::filesystem::is_directory(local / L"Configs" / L"paths.alpha"));
    assert(!std::filesystem::exists(local / L"Configs" / L"paths.beta"));
    assert(pathsB.ensureRoot(pathsB.context, GAKU_PATH_USER_DATA) == GAKU_OK);
    assert(std::filesystem::is_directory(local / L"UserData" / L"paths.beta"));
    assert(!std::filesystem::exists(local / L"UserData" / L"paths.alpha"));
    { std::ofstream blocked(local / L"Cache", std::ios::binary); blocked << "synthetic blocker"; }
    assert(pathsA.ensureRoot(pathsA.context, GAKU_PATH_CACHE) == GAKU_E_INTERNAL);
    assert(std::filesystem::is_regular_file(local / L"Cache"));

    // Original runtime ABI still provides the same roots and no new fields.
    GakuRuntimeService runtime{};
    assert(alpha.host.queryService(alpha.host.hostContext, GAKU_SERVICE_RUNTIME, 1,
        sizeof(runtime), &runtime) == GAKU_OK);
    assert(std::filesystem::path(runtime.gameRoot) == root);
    assert(std::filesystem::path(runtime.localRoot) == local);
    assert(std::filesystem::path(runtime.pluginsRoot) == local / L"plugins");

    GakuLoader::RetireOwnerForTest(alpha.owner);
    CheckUnchanged(getA, GAKU_PATH_CONFIGS, GAKU_E_SHUTTING_DOWN);
    assert(pathsA.ensureRoot(pathsA.context, GAKU_PATH_TEMP) == GAKU_E_SHUTTING_DOWN);
    output = sentinel;
    assert(query(1, sizeof(output), &output) == GAKU_E_SHUTTING_DOWN);
    assert(std::memcmp(&output, &sentinel, sizeof(output)) == 0);
    assert(!std::filesystem::exists(local / L"Temp"));
    assert(pathsB.ensureRoot(pathsB.context, GAKU_PATH_LOGS) == GAKU_OK); // other owner unaffected
    assert(GakuLoader::BindMainThread(GetCurrentThreadId()));
    GakuLoader::Shutdown();
    CheckUnchanged(getB, GAKU_PATH_USER_DATA, GAKU_E_SHUTTING_DOWN);
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) { std::fputs("usage: paths-tests <synthetic absolute root>\n", stderr); return 2; }
    const std::filesystem::path root(argv[1]);
    assert(root.is_absolute());
    const auto originalCwd = std::filesystem::current_path();
    const auto foreignCwd = root / L"foreign cwd";
    std::filesystem::create_directories(foreignCwd);
    std::filesystem::current_path(foreignCwd);
    SdkChecks(root);
    StateChecks(root);
    HostChecks(root);
    assert(std::filesystem::is_empty(foreignCwd));
    std::filesystem::current_path(originalCwd);
    std::puts("PASS: optional paths service; original ABI fixtures still activate; owner isolation; "
        "UTF-16/Unicode/space roots independent of cwd; pure queries; exact/short/large buffers; "
        "invalid namespace and arguments preserve outputs; lazy/idempotent creation; filesystem "
        "failure; owner retirement and shutdown");
    return 0;
}
