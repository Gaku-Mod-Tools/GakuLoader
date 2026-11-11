#define NOMINMAX
#include "GakuTestCompat.hpp"
#include "Host.hpp"
#include "GakuLifecycle.h"
#include "HostContracts.hpp"
#include "HostConfig.hpp"
#include "Hotkeys.hpp"
#include "ActorEntry.hpp"
#include "DesktopUi.hpp"
#include "GakuFixtureContract.hpp"
#include "GakuProcessLifetimeHooks.h"
#include <cassert>
#include <atomic>
#include <thread>
#include <mutex>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>
#include <algorithm>

// Desktop binding is covered separately by the real runtime; these lifecycle
// tests intentionally need no game, HWND, D3D device or private VRify modules.
namespace GakuLoader::DesktopUi {
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
namespace GakuLoader::DesktopDialogs {
GakuFileDialogService ServiceForOwner(void*) { return {}; }
void CancelOwner(void*) {}
}

namespace {
using TestTargetFn = int (*)(int);
TestTargetFn persistentOriginal = nullptr;
GAKU_NOINLINE int PersistentTarget(int value) { volatile int result = value + 1; return result; }
int PersistentDetour(int value) { return persistentOriginal(value) + 20; }
TestTargetFn batchOriginalA = nullptr, batchOriginalB = nullptr;
GAKU_NOINLINE int BatchTargetA(int value) { volatile int result = value + 2; return result; }
GAKU_NOINLINE int BatchTargetB(int value) { volatile int result = value + 3; return result; }
int BatchDetourA(int value) {
    assert(batchOriginalA && batchOriginalB);
    return batchOriginalA(value) + batchOriginalB(value) + 100;
}
int BatchDetourB(int value) { return batchOriginalB(value) + 200; }
TestTargetFn retainedOriginalA = nullptr, retainedOriginalB = nullptr, unpromotedOriginal = nullptr;
GAKU_NOINLINE int RetainedTargetA(int value) { volatile int result = value + 11; return result; }
GAKU_NOINLINE int RetainedTargetB(int value) { volatile int result = value + 12; return result; }
GAKU_NOINLINE int UnpromotedTarget(int value) { volatile int result = value + 13; return result; }
int RetainedDetourA(int value) { return retainedOriginalA(value) + 300; }
int RetainedDetourB(int value) { return retainedOriginalB(value) + 400; }
int UnpromotedDetour(int value) { return unpromotedOriginal(value) + 500; }
void FirstEnabled(uint32_t count) {
    if (count == 1) {
        assert(batchOriginalA && batchOriginalB);
        assert(BatchTargetA(4) == 113); // first real detour can call second original
        assert(BatchTargetB(4) == 7);   // second target is not enabled yet
    }
}

std::mutex g_keyMutex;
std::vector<uint32_t> g_keysHeld;
bool g_foreground = true, g_textInput = false;
bool KeyDownImpl(uint32_t key) {
    std::lock_guard<std::mutex> lock(g_keyMutex);
    return std::find(g_keysHeld.begin(), g_keysHeld.end(), key) != g_keysHeld.end();
}
bool KeyDown(uint32_t key) { return KeyDownImpl(key); }
void Hold(uint32_t key) {
    std::lock_guard<std::mutex> lock(g_keyMutex);
    if (std::find(g_keysHeld.begin(), g_keysHeld.end(), key) == g_keysHeld.end()) g_keysHeld.push_back(key);
}
void Release(uint32_t key) {
    std::lock_guard<std::mutex> lock(g_keyMutex);
    g_keysHeld.erase(std::remove(g_keysHeld.begin(), g_keysHeld.end(), key), g_keysHeld.end());
}
GakuLoader::Hotkeys::PollContext Context() {
    GakuLoader::Hotkeys::PollContext context;
    context.foregroundOwnedByThisProcess = g_foreground;
    context.hostWindowWantsTextInput = g_textInput;
    context.isKeyDown = KeyDown;
    return context;
}
bool Pump() { return GakuLoader::PumpHotkeysForTest(Context()); }

GakuFixtureSnapshot ReadFixture(HMODULE module) {
    const auto snapshot = reinterpret_cast<FixtureSnapshotFn>(GetProcAddress(module, "GakuFixture_Snapshot"));
    assert(snapshot);
    return snapshot();
}
void DeleteMarker(const std::filesystem::path& pluginDir, const wchar_t* name) {
    DeleteFileW((pluginDir / name).c_str());
}
size_t ConflictCount() {
    const auto rows = GakuLoader::Hotkeys::Snapshot();
    return static_cast<size_t>(std::count_if(rows.begin(), rows.end(), [](const auto& row) { return row.conflicted; }));
}

GakuLoader::Hotkeys::Row HotkeyRow(const char* pluginId, const char* actionId) {
    const auto rows = GakuLoader::Hotkeys::Snapshot();
    const auto row = std::find_if(rows.begin(), rows.end(), [&](const auto& item) {
        return item.pluginId == pluginId && item.actionId == actionId;
    });
    assert(row != rows.end());
    return *row;
}

void TestHotkeyIdentities(const std::filesystem::path& root) {
    using namespace GakuLoader::Hotkeys;
    const auto legacyFile = root / L"hotkey-legacy-identities.ini";
    assert(WritePrivateProfileStringW(L"hotkeys", nullptr, nullptr, legacyFile.c_str()));
    assert(WritePrivateProfileStringW(L"hotkeys", L"a.b.c", L"Ctrl+F20", legacyFile.c_str()));
    int ownerA = 0, ownerB = 0;
    GakuHotkeyBinding a{1, sizeof(a), "c", "A", VK_F13, 0, [](void*) {}, nullptr};
    GakuHotkeyBinding b{1, sizeof(b), "b.c", "B", VK_F14, 0, [](void*) {}, nullptr};
    LoadOverrides(legacyFile);
    assert(Register(&ownerA, "a.b", &a) == GAKU_OK);
    assert(HotkeyRow("a.b", "c").virtualKey == VK_F20); // unique old alias remains compatible
    assert(Register(&ownerB, "a", &b) == GAKU_OK);
    assert(HotkeyRow("a.b", "c").virtualKey == VK_F13 && !HotkeyRow("a.b", "c").overridden);
    assert(HotkeyRow("a", "b.c").virtualKey == VK_F14 && !HotkeyRow("a", "b.c").overridden);
    RetireOwner(&ownerB);
    assert(HotkeyRow("a.b", "c").virtualKey == VK_F20); // uniqueness is restored
    RetireOwner(&ownerA);
    LoadOverrides(legacyFile);
    assert(Register(&ownerB, "a", &b) == GAKU_OK); // reverse order has the same final result
    assert(HotkeyRow("a", "b.c").virtualKey == VK_F20);
    assert(Register(&ownerA, "a.b", &a) == GAKU_OK);
    assert(HotkeyRow("a.b", "c").virtualKey == VK_F13);
    assert(HotkeyRow("a", "b.c").virtualKey == VK_F14);
    assert(GakuLoader::ReadConfigValue(legacyFile, L"hotkeys", L"a.b.c") == L"Ctrl+F20");

    const auto identityFile = root / L"hotkey-exact-identities.ini";
    assert(WritePrivateProfileStringW(L"hotkeys", nullptr, nullptr, identityFile.c_str()));
    assert(WritePrivateProfileStringW(L"hotkeys", L"a.b.c", L"Ctrl+F20", identityFile.c_str()));
    LoadOverrides(identityFile);
    assert(SetOverride(identityFile, "a.b", "c", VK_F15, 0) == GAKU_OK);
    assert(HotkeyRow("a.b", "c").virtualKey == VK_F15);
    assert(HotkeyRow("a", "b.c").virtualKey == VK_F14);
    assert(SetOverride(identityFile, "a", "b.c", VK_F16, GAKU_HOTKEY_MOD_ALT) == GAKU_OK);
    // Assert the public persistence format with literals, not a copy of its encoder.
    assert(GakuLoader::ReadConfigValue(identityFile, L"hotkeys", L"v2:612e62:63") == L"F15");
    assert(GakuLoader::ReadConfigValue(identityFile, L"hotkeys", L"v2:61:622e63") == L"Alt+F16");
    assert(GakuLoader::ReadConfigValue(identityFile, L"hotkeys", L"a.b.c") == L"Ctrl+F20");
    PollResult polled;
    Poll(Context(), polled); // released sample after manager apply
    Hold(VK_F15); Poll(Context(), polled);
    assert(polled.actions.size() == 1 && polled.actions[0].owner == &ownerA);
    Release(VK_F15); Poll(Context(), polled);
    const auto blocked = root / L"hotkey-identities-directory";
    std::filesystem::create_directories(blocked);
    assert(SetOverride(blocked, "a.b", "c", VK_F17, 0) == GAKU_E_INTERNAL);
    assert(ResetOverride(blocked, "a.b", "c") == GAKU_E_INTERNAL);
    a.virtualKey = VK_F18;
    assert(Register(&ownerA, "a.b", &a) == GAKU_OK);
    assert(HotkeyRow("a.b", "c").virtualKey == VK_F15); // failed writes did not change cache
    RetireOwner(&ownerA); RetireOwner(&ownerB);
    LoadOverrides(identityFile);
    assert(Register(&ownerB, "a", &b) == GAKU_OK);
    assert(Register(&ownerA, "a.b", &a) == GAKU_OK);
    assert(HotkeyRow("a.b", "c").virtualKey == VK_F15);
    assert(HotkeyRow("a", "b.c").virtualKey == VK_F16 &&
        HotkeyRow("a", "b.c").modifiers == GAKU_HOTKEY_MOD_ALT);
    assert(ResetOverride(identityFile, "a.b", "c") == GAKU_OK);
    assert(HotkeyRow("a.b", "c").virtualKey == VK_F18 && !HotkeyRow("a.b", "c").overridden);
    assert(HotkeyRow("a", "b.c").virtualKey == VK_F16);
    assert(GakuLoader::ReadConfigValue(identityFile, L"hotkeys", L"v2:612e62:63") == L"<default>");
    assert(GakuLoader::ReadConfigValue(identityFile, L"hotkeys", L"a.b.c") == L"Ctrl+F20");
    RetireOwner(&ownerA); RetireOwner(&ownerB);
    LoadOverrides(identityFile);
    a.virtualKey = VK_F19;
    assert(Register(&ownerA, "a.b", &a) == GAKU_OK);
    assert(HotkeyRow("a.b", "c").virtualKey == VK_F19 && !HotkeyRow("a.b", "c").overridden);
    // The legacy alias is now unique, but the exact default marker still wins.
    assert(Register(&ownerB, "a", &b) == GAKU_OK);
    assert(HotkeyRow("a", "b.c").virtualKey == VK_F16);
    RetireOwner(&ownerA); RetireOwner(&ownerB);

    const auto caseFile = root / L"hotkey-case-identities.ini";
    assert(WritePrivateProfileStringW(L"hotkeys", nullptr, nullptr, caseFile.c_str()));
    assert(WritePrivateProfileStringW(L"hotkeys", L"case.plugin.go", L"F20", caseFile.c_str()));
    assert(WritePrivateProfileStringW(L"hotkeys", L"case.action.go", L"F21", caseFile.c_str()));
    int upperOwner = 0, lowerOwner = 0, actionOwner = 0;
    GakuHotkeyBinding upper{1, sizeof(upper), "go", "Upper plugin", VK_F13, 0, [](void*) {}, nullptr};
    GakuHotkeyBinding lower{1, sizeof(lower), "go", "Lower plugin", VK_F14, 0, [](void*) {}, nullptr};
    GakuHotkeyBinding actionUpper{1, sizeof(actionUpper), "Go", "Upper action", VK_F15, 0, [](void*) {}, nullptr};
    GakuHotkeyBinding actionLower{1, sizeof(actionLower), "go", "Lower action", VK_F16, 0, [](void*) {}, nullptr};
    LoadOverrides(caseFile);
    assert(Register(&upperOwner, "Case.Plugin", &upper) == GAKU_OK);
    assert(HotkeyRow("Case.Plugin", "go").virtualKey == VK_F20);
    assert(Register(&lowerOwner, "case.plugin", &lower) == GAKU_OK);
    assert(HotkeyRow("Case.Plugin", "go").virtualKey == VK_F13);
    assert(HotkeyRow("case.plugin", "go").virtualKey == VK_F14);
    assert(Register(&actionOwner, "case.action", &actionUpper) == GAKU_OK);
    assert(HotkeyRow("case.action", "Go").virtualKey == VK_F21);
    assert(Register(&actionOwner, "case.action", &actionLower) == GAKU_OK);
    assert(HotkeyRow("case.action", "Go").virtualKey == VK_F15);
    assert(HotkeyRow("case.action", "go").virtualKey == VK_F16);
    assert(SetOverride(caseFile, "Case.Plugin", "go", VK_F17, 0) == GAKU_OK);
    assert(SetOverride(caseFile, "case.plugin", "go", VK_F18, 0) == GAKU_OK);
    assert(SetOverride(caseFile, "case.action", "Go", VK_F19, 0) == GAKU_OK);
    assert(SetOverride(caseFile, "case.action", "go", VK_F22, 0) == GAKU_OK);
    assert(GakuLoader::ReadConfigValue(caseFile, L"hotkeys", L"v2:436173652e506c7567696e:676f") == L"F17");
    assert(GakuLoader::ReadConfigValue(caseFile, L"hotkeys", L"v2:636173652e706c7567696e:676f") == L"F18");
    assert(GakuLoader::ReadConfigValue(caseFile, L"hotkeys", L"v2:636173652e616374696f6e:476f") == L"F19");
    assert(GakuLoader::ReadConfigValue(caseFile, L"hotkeys", L"v2:636173652e616374696f6e:676f") == L"F22");
    assert(SetOverride(caseFile, "CASE.PLUGIN", "go", VK_F23, 0) == GAKU_E_UNAVAILABLE);
    assert(ResetOverride(caseFile, "case.action", "GO") == GAKU_E_UNAVAILABLE);
    assert(ResetOverride(caseFile, "case.action", "Go") == GAKU_OK);
    assert(HotkeyRow("case.action", "Go").virtualKey == VK_F15);
    assert(HotkeyRow("case.action", "go").virtualKey == VK_F22);
    RetireOwner(&upperOwner); RetireOwner(&lowerOwner); RetireOwner(&actionOwner);
    LoadOverrides(caseFile);
    assert(Register(&lowerOwner, "case.plugin", &lower) == GAKU_OK);
    assert(Register(&upperOwner, "Case.Plugin", &upper) == GAKU_OK);
    assert(Register(&actionOwner, "case.action", &actionLower) == GAKU_OK);
    actionUpper.virtualKey = VK_F23;
    assert(Register(&actionOwner, "case.action", &actionUpper) == GAKU_OK);
    assert(HotkeyRow("Case.Plugin", "go").virtualKey == VK_F17);
    assert(HotkeyRow("case.plugin", "go").virtualKey == VK_F18);
    assert(HotkeyRow("case.action", "go").virtualKey == VK_F22);
    assert(HotkeyRow("case.action", "Go").virtualKey == VK_F23 && !HotkeyRow("case.action", "Go").overridden);
    assert(GakuLoader::ReadConfigValue(caseFile, L"hotkeys", L"case.plugin.go") == L"F20");
    assert(GakuLoader::ReadConfigValue(caseFile, L"hotkeys", L"case.action.go") == L"F21");
    RetireOwner(&upperOwner); RetireOwner(&lowerOwner); RetireOwner(&actionOwner);

    // Hex keys can exceed the old 8192-character section buffer in normal use.
    const auto longFile = root / L"hotkey-long-identities.ini";
    assert(WritePrivateProfileStringW(L"hotkeys", nullptr, nullptr, longFile.c_str()));
    int longOwner = 0, watchOwner = 0;
    std::vector<std::string> actions;
    for (int i = 0; i < 45; ++i) actions.push_back(std::string(96, 'x') + std::to_string(i));
    LoadOverrides(longFile);
    for (size_t i = 0; i < actions.size(); ++i) {
        GakuHotkeyBinding item{1, sizeof(item), actions[i].c_str(), "Long identity", VK_F13, 0, [](void*) {}, nullptr};
        assert(Register(&longOwner, "long.owner", &item) == GAKU_OK);
        assert(SetOverride(longFile, "long.owner", item.actionId, VK_F14 + static_cast<uint32_t>(i % 11), GAKU_HOTKEY_MOD_ALT) == GAKU_OK);
    }
    std::vector<wchar_t> oldBuffer(8192);
    assert(GetPrivateProfileSectionW(L"hotkeys", oldBuffer.data(), static_cast<DWORD>(oldBuffer.size()), longFile.c_str()) == oldBuffer.size() - 2);
    RetireOwner(&longOwner);
    LoadOverrides(longFile);
    for (size_t i = 0; i < actions.size(); ++i) {
        GakuHotkeyBinding item{1, sizeof(item), actions[i].c_str(), "Long identity", VK_F13, 0, [](void*) {}, nullptr};
        assert(Register(&longOwner, "long.owner", &item) == GAKU_OK);
        const auto row = HotkeyRow("long.owner", item.actionId);
        assert(row.virtualKey == VK_F14 + i % 11 && row.modifiers == GAKU_HOTKEY_MOD_ALT && row.overridden);
    }
    GakuHotkeyBinding watch{1, sizeof(watch), "watch", "Truncation watch", VK_F13, 0, [](void*) {}, nullptr};
    assert(Register(&watchOwner, "truncation", &watch) == GAKU_OK);
    assert(SetOverride(longFile, "truncation", "watch", VK_F15, 0) == GAKU_OK);
    // Overflow must reject the entire section, including its valid first entries.
    // Use a new path so this native file write never relies on INI cache flushing.
    const auto overflowFile = root / (L"hotkey-overflow-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    FILE* overflow = _wfopen(overflowFile.c_str(), L"w");
    assert(overflow);
    std::fprintf(overflow, "[hotkeys]\nv2:7472756e636174696f6e:7761746368=F16\n");
    const std::string padding(160, 'q');
    for (int i = 0; i < 200; ++i) std::fprintf(overflow, "padding%d.%s=F20\n", i, padding.c_str());
    std::fclose(overflow);
    LoadOverrides(overflowFile);
    assert(HotkeyRow("truncation", "watch").virtualKey == VK_F15);
    watch.virtualKey = VK_F14;
    assert(Register(&watchOwner, "truncation", &watch) == GAKU_OK);
    assert(HotkeyRow("truncation", "watch").virtualKey == VK_F15); // rejected load kept the old cache too
    RetireOwner(&longOwner); RetireOwner(&watchOwner);
}

// --- Shared actor entry ------------------------------------------------------
// The loader owns the only detour on the game's actor LateUpdate. Participants
// registered through the ABI must bracket the original exactly once per call.
std::vector<std::string> g_actorEntryEvents;
int g_originalCalls = 0;
void RecordBeforeA(void*, void*) { g_actorEntryEvents.emplace_back("before.a"); }
void RecordBeforeB(void*, void*) { g_actorEntryEvents.emplace_back("before.b"); }
void RecordAfterA(void*, void*) { g_actorEntryEvents.emplace_back("after.a"); }
void RecordAfterThrows(void*, void*) { g_actorEntryEvents.emplace_back("before.throws"); throw 7; }
void CountOriginal(void*, void*) { ++g_originalCalls; g_actorEntryEvents.emplace_back("original"); }
GakuActorEntryBinding Binding(uint32_t phase, const char* name, void (*callback)(void*, void*), void* user = nullptr) {
    GakuActorEntryBinding binding{GAKU_SERVICE_VERSION, sizeof(GakuActorEntryBinding), phase, name, callback, user};
    return binding;
}

// --- Snapshot immutability ---------------------------------------------------
// A participant that re-registers its own owner's phase from inside a callback
// must not change the version the running invocation already captured.
GakuActorEntryService* g_registeringEntryService = nullptr;
void RecordAfterOld(void*, void*) { g_actorEntryEvents.emplace_back("after.old"); }
void RecordAfterNew(void*, void*) { g_actorEntryEvents.emplace_back("after.new"); }
void ReplacingBefore(void*, void*) {
    g_actorEntryEvents.emplace_back("before.replacing");
    auto replacement = Binding(GAKU_ACTOR_ENTRY_AFTER, "a.after", RecordAfterNew);
    assert(g_registeringEntryService->registerEntry(g_registeringEntryService->context, &replacement) == GAKU_OK);
}

// --- Registration/retirement interleave --------------------------------------
HANDLE g_registerPaused = nullptr, g_registerResume = nullptr;
void PauseRegistration() {
    SetEvent(g_registerPaused);
    WaitForSingleObject(g_registerResume, 5000);
}

// --- Actor participants vs the desktop callback gate -------------------------
// Participants read plugin state that the desktop UI writes (free camera,
// subtitle offsets). They must therefore run inside the same gate that
// serializes desktop frames, while the game original stays outside it.
HANDLE g_desktopGateHeld = nullptr, g_desktopGateRelease = nullptr;
std::atomic_bool g_desktopFrameActive{false};
std::atomic_bool g_participantSawDesktopFrame{false};
std::atomic_int g_gateParticipantCalls{0};
int g_gateOriginalCalls = 0;
void* g_gateLastActor = nullptr;
void HoldDesktopGate(void*) {
    g_desktopFrameActive.store(true, std::memory_order_release);
    SetEvent(g_desktopGateHeld);
    WaitForSingleObject(g_desktopGateRelease, 5000);
    g_desktopFrameActive.store(false, std::memory_order_release);
}
void GateParticipant(void*, void* actor) {
    g_participantSawDesktopFrame.store(g_desktopFrameActive.load(std::memory_order_acquire),
        std::memory_order_release);
    g_gateLastActor = actor;
    g_gateParticipantCalls.fetch_add(1, std::memory_order_relaxed);
}
void GateOriginal(void*, void*) { ++g_gateOriginalCalls; }
}

int wmain(int argc, wchar_t** argv) {
    assert(argc == 2 || argc == 3);
    assert(GakuLoader::ParseConfigFlag(L" \t FaLsE ", true) == false);
    assert(GakuLoader::ParseConfigFlag(L"0", true) == false);
    assert(GakuLoader::ParseConfigFlag(L"NO", true) == false);
    assert(GakuLoader::ParseConfigFlag(L" true ", false) == true);
    assert(GakuLoader::ParseConfigFlag(L"1", false) == true);
    assert(GakuLoader::ParseConfigFlag(L"YES", false) == true);
    assert(GakuLoader::ParseConfigFlag(L"typo", true) == true);
    assert(GakuLoader::ParseConfigFlag(L"typo", false) == false);

    const std::filesystem::path root(argv[1]);
    const auto pluginDir = root / L"GakuLoader" / L"plugins";
    DeleteMarker(pluginDir, L"legacy-dllmain.txt");
    DeleteMarker(pluginDir, L"legacy-query.txt");
    DeleteMarker(pluginDir, L"legacy-init.txt");
    g_keysHeld.clear();
    g_foreground = true; g_textInput = false;

    GakuLoader::DisableHotkeyPollerForTest();
    GakuLoader::Initialize(root, root / "GakuLoader");
    // Native activation is a separate phase: nothing is loaded by Initialize.
    assert(GakuLoader::GetLoadedPlugins().empty());
    assert(!GakuLoader::IsRuntimeReady());
    GakuLoader::ActivatePlugins();

    const auto& plugins = GakuLoader::GetLoadedPlugins();
    if (argc == 3 && std::wcscmp(argv[2], L"filtered") == 0) {
        // A config flag must skip LoadLibrary entirely, so the legacy DllMain
        // never runs and no pre-reset module is even resident.
        assert(plugins.size() == 2);
        assert(GetModuleHandleW(L"legacy-fixture.dll") == nullptr);
        assert(!std::filesystem::exists(pluginDir / L"legacy-dllmain.txt"));
        assert(GakuLoader::BindMainThread(GetCurrentThreadId()));
        GakuLoader::Shutdown();
        puts("PASS: per-filename config skips a pre-reset DLL before LoadLibrary");
        return 0;
    }
    assert(plugins.size() == 2); // the legacy DLL is probed, refused and released
    assert(!plugins[0].failed && !plugins[1].failed);
    assert(plugins[0].id == "fixture.a" && plugins[1].id == "fixture.b");
    assert(plugins[0].activated && !plugins[0].runtimeReady);
    assert(std::filesystem::exists(pluginDir / L"legacy-dllmain.txt"));
    assert(!std::filesystem::exists(pluginDir / L"legacy-query.txt")); // old entry never called
    assert(!std::filesystem::exists(pluginDir / L"legacy-init.txt"));

    const HMODULE moduleA = plugins[0].handle;
    const HMODULE moduleB = plugins[1].handle;
    const auto enqueue = reinterpret_cast<FixtureEnqueueFn>(GetProcAddress(moduleA, "GakuFixture_Enqueue"));
    const auto hook = reinterpret_cast<FixtureHookFn>(GetProcAddress(moduleA, "GakuFixture_Hook"));
    const auto blockDesktop = reinterpret_cast<FixtureBlockFn>(GetProcAddress(moduleA, "GakuFixture_BlockDesktop"));
    const auto blockTick = reinterpret_cast<FixtureBlockFn>(GetProcAddress(moduleA, "GakuFixture_BlockTick"));
    const auto unregisterB = reinterpret_cast<FixtureVoidFn>(GetProcAddress(moduleB, "GakuFixture_UnregisterHotkeys"));
    assert(enqueue && hook && blockDesktop && blockTick && unregisterB);

    auto a = ReadFixture(moduleA);
    auto b = ReadFixture(moduleB);
    assert(a.activateCount == 1 && a.contractFailures == 0);
    assert(b.activateCount == 1 && b.contractFailures == 0);
    assert(a.activateThread == GetCurrentThreadId());
    assert(a.readyCount == 0 && a.tickCount == 0 && a.actorTickCount == 0);
    assert(a.hotkeyRegistrations == 2 && b.hotkeyRegistrations == 2);
    assert(a.hotkeyStatus == GAKU_OK);

    GakuLoader::DesktopHostSnapshot desktopSnapshot;
    assert(GakuLoader::SnapshotDesktopHost(desktopSnapshot));
    assert(desktopSnapshot.plugins.size() == 2 && desktopSnapshot.activated && !desktopSnapshot.runtimeReady);
    assert(desktopSnapshot.plugins[0].hasPanel && desktopSnapshot.plugins[0].activated);
    assert(desktopSnapshot.plugins[1].hasPanel && desktopSnapshot.plugins[1].id == "fixture.b");
    GakuLifecycleServiceV1 lifecycleA{}, lifecycleB{}, lifecycleSentinel;
    std::memset(&lifecycleSentinel, 0x5a, sizeof(lifecycleSentinel));
    const auto unchangedLifecycle = lifecycleSentinel;
    assert(plugins[0].host.queryService(plugins[0].host.hostContext, GAKU_SERVICE_LIFECYCLE,
        2, sizeof(lifecycleSentinel), &lifecycleSentinel) == GAKU_E_VERSION);
    assert(std::memcmp(&lifecycleSentinel, &unchangedLifecycle, sizeof(lifecycleSentinel)) == 0);
    assert(plugins[0].host.queryService(plugins[0].host.hostContext, GAKU_SERVICE_LIFECYCLE,
        1, sizeof(lifecycleSentinel) - 1, &lifecycleSentinel) == GAKU_E_SIZE);
    assert(std::memcmp(&lifecycleSentinel, &unchangedLifecycle, sizeof(lifecycleSentinel)) == 0);
    assert(plugins[0].host.queryService(plugins[0].host.hostContext, GAKU_SERVICE_LIFECYCLE,
        1, sizeof(lifecycleA), &lifecycleA) == GAKU_OK);
    assert(plugins[1].host.queryService(plugins[1].host.hostContext, GAKU_SERVICE_LIFECYCLE,
        1, sizeof(lifecycleB), &lifecycleB) == GAKU_OK);
    assert(!lifecycleA.isStopping(lifecycleA.context) && !lifecycleB.isStopping(lifecycleB.context));

    if (argc == 3 && std::wcscmp(argv[2], L"cancel-runtime") == 0) {
        const auto waitForStop = reinterpret_cast<FixtureBlockFn>(GetProcAddress(moduleA, "GakuFixture_WaitRuntimeForStop"));
        assert(waitForStop);
        waitForStop(0);
        std::thread initializing([] { GakuLoader::NotifyRuntimeReady(); });
        const auto deadline = GetTickCount64() + 2000;
        while (!waitForStop(1) && GetTickCount64() < deadline) Sleep(1);
        assert(waitForStop(1));
        assert(GakuLoader::SnapshotDesktopHost(desktopSnapshot) && desktopSnapshot.runtimeReady);
        assert(!desktopSnapshot.plugins[0].runtimeReady && !desktopSnapshot.plugins[1].runtimeReady);
        const auto cancelStarted = GetTickCount64();
        GakuLoader::RequestShutdown();
        initializing.join();
        assert(GetTickCount64() - cancelStarted < 2000);
        a = ReadFixture(moduleA); b = ReadFixture(moduleB);
        assert(a.readyCount == 1 && a.contractFailures == 0 && b.readyCount == 0);
        assert(lifecycleA.isStopping(lifecycleA.context));
        assert(GakuLoader::BindMainThread(GetCurrentThreadId()));
        GakuLoader::Shutdown();
        puts("PASS: runtime callback reads status/cancellation while holding gate; shutdown cancels wait and skips later runtime callbacks");
        return 0;
    }

    // Runtime ready is delivered from a loader worker thread, not the main thread.
    const DWORD mainThread = GetCurrentThreadId();
    // Arbitrary workers must not claim main-thread identity by ticking first.
    std::thread earlyForeign([] { GakuLoader::TickFrame(0.016f); });
    earlyForeign.join();
    assert(ReadFixture(moduleA).tickCount == 0);
    assert(GakuLoader::BindMainThread(mainThread));
    std::thread runtime([] { GakuLoader::NotifyRuntimeReady(); });
    runtime.join();
    a = ReadFixture(moduleA);
    assert(a.readyCount == 1 && a.readyThread != mainThread);
    assert(GakuLoader::IsRuntimeReady());

    // The frame pump runs with no actor at all and drains the queue first.
    GakuLoader::TickFrame(0.016f);
    a = ReadFixture(moduleA);
    assert(a.tickCount == 1 && a.tickThread == mainThread);
    assert(a.taskCount == 1 && a.nestedTaskCount == 0);
    assert(GakuLoader::SnapshotDesktopHost(desktopSnapshot) && desktopSnapshot.runtimeReady);
    GakuLoader::TickFrame(0.016f);
    assert(ReadFixture(moduleA).nestedTaskCount == 1); // enqueued by a task, runs next frame
    GakuLoader::TickActor(reinterpret_cast<void*>(uintptr_t{1}), 0.016f);
    GakuLoader::TickActor(reinterpret_cast<void*>(uintptr_t{2}), 0.016f);
    GakuLoader::TickActor(nullptr, 0.016f);
    a = ReadFixture(moduleA);
    assert(a.actorTickCount == 3 && a.actorACount == 1 && a.actorBCount == 1 && a.otherActorCount == 1);
    assert(ReadFixture(moduleB).tickCount == 2 && ReadFixture(moduleB).actorTickCount == 3);

    // Wrong-thread dispatch is rejected without running plugin callbacks.
    std::thread foreign([] { GakuLoader::TickFrame(0.016f); });
    foreign.join();
    assert(ReadFixture(moduleA).tickCount == 2);

    // --- Hotkey registry ---------------------------------------------------
    // A single physical press dispatches exactly once, and only after the main
    // thread drains the queue.
    Hold(0x78); // F9 -> fixture.a beta
    assert(!Pump());
    assert(ReadFixture(moduleA).hotkeyCount == 0);
    GakuLoader::TickFrame(0.016f);
    a = ReadFixture(moduleA);
    assert(a.hotkeyCount == 1 && a.hotkeyThread == mainThread);
    Pump(); GakuLoader::TickFrame(0.016f); // still held: no auto-repeat
    assert(ReadFixture(moduleA).hotkeyCount == 1);
    // A dropped KeyUp must not leave the binding latched. Clearing the simulated
    // key state (as if WM_KEYUP never arrived) and letting one poll observe the
    // release must re-arm the edge, so the next physical press fires exactly once
    // (net hotkeyCount stays 2 for the assertions below).
    { std::lock_guard<std::mutex> lock(g_keyMutex); g_keysHeld.clear(); } // lose the KeyUp
    Pump(); GakuLoader::TickFrame(0.016f);        // poll observes the release, no dispatch
    assert(ReadFixture(moduleA).hotkeyCount == 1); // no replay from the lost KeyUp
    Hold(0x78); Pump(); GakuLoader::TickFrame(0.016f); // next press re-arms the edge
    assert(ReadFixture(moduleA).hotkeyCount == 2);     // exactly +1, not stuck
    Release(0x78); Pump();

    // Ctrl+M is claimed by both fixtures: both bindings are already conflicted
    // from registration, neither fires, and neither plugin is rejected.
    assert(ConflictCount() == 2);
    Hold(0x11); Hold('M');
    Pump(); GakuLoader::TickFrame(0.016f);
    assert(ReadFixture(moduleA).hotkeyCount == 2 && ReadFixture(moduleB).hotkeyCount == 0);
    assert(ConflictCount() == 2);
    // The unaffected binding of the conflicting owner still works.
    Hold(0x10); Hold(0x79); // Ctrl+Shift+F10 -> fixture.b gamma
    Pump(); GakuLoader::TickFrame(0.016f);
    assert(ReadFixture(moduleB).hotkeyCount == 1);
    Release(0x79); Release(0x10); Release('M'); Release(0x11); Pump();
    // Resolving the conflict re-enables the remaining binding.
    unregisterB();
    assert(ConflictCount() == 0);
    Hold(0x11); Hold('M'); Pump(); GakuLoader::TickFrame(0.016f);
    assert(ReadFixture(moduleA).hotkeyCount == 3);
    Release('M'); Release(0x11); Pump();

    // F1 is the manager toggle, fires once per press, and needs no game frame.
    Hold(0x70);
    assert(Pump());
    assert(!Pump()); // held
    Release(0x70); Pump();
    Hold(0x70);
    assert(Pump());
    Release(0x70); Pump();
    assert(ReadFixture(moduleA).hotkeyCount == 3); // F1 is not a plugin action

    // Focus loss and text input never dispatch and never replay later.
    const uint32_t before = ReadFixture(moduleA).hotkeyCount;
    g_foreground = false;
    Release(0x78); Pump(); Hold(0x78); Pump(); GakuLoader::TickFrame(0.016f);
    assert(ReadFixture(moduleA).hotkeyCount == before);
    g_foreground = true;
    Pump(); GakuLoader::TickFrame(0.016f); // still held on return: primed, no replay
    assert(ReadFixture(moduleA).hotkeyCount == before);
    Release(0x78); Pump();
    g_textInput = true;
    Hold(0x78); Pump(); GakuLoader::TickFrame(0.016f);
    assert(ReadFixture(moduleA).hotkeyCount == before);
    g_textInput = false; Release(0x78); Pump();
    Hold(0x78); Pump(); GakuLoader::TickFrame(0.016f);
    assert(ReadFixture(moduleA).hotkeyCount == before + 1);
    Release(0x78); Pump();

    // The documented ini key must bind the real Motion owner-local action,
    // without accidentally requiring "gakumotion.gakumotion.show-gui".
    const auto overrideFile = root / L"hotkey-override-test.ini";
    assert(WritePrivateProfileStringW(L"hotkeys", nullptr, nullptr, overrideFile.c_str()));
    assert(WritePrivateProfileStringW(L"hotkeys", L"gakumotion.show-gui", L"Ctrl+Shift+M", overrideFile.c_str()));
    GakuLoader::Hotkeys::LoadOverrides(overrideFile);
    int temporaryOwner = 0;
    GakuHotkeyBinding overrideBinding{1, sizeof(GakuHotkeyBinding), "show-gui", "Motion GUI", 'M',
        GAKU_HOTKEY_MOD_CTRL, [](void*) {}, nullptr};
    assert(GakuLoader::Hotkeys::Register(&temporaryOwner, "gakumotion", &overrideBinding) == GAKU_OK);
    const auto overridden = GakuLoader::Hotkeys::Snapshot();
    const auto motionBinding = std::find_if(overridden.begin(), overridden.end(), [](const auto& row) {
        return row.pluginId == "gakumotion" && row.actionId == "show-gui";
    });
    assert(motionBinding != overridden.end() && motionBinding->modifiers ==
        (GAKU_HOTKEY_MOD_CTRL | GAKU_HOTKEY_MOD_SHIFT) && !motionBinding->conflicted);

    // Manager edits write through to the ini and the running registry. A held
    // capture key cannot become an action press when the binding is changed.
    const auto editedRow = [&] {
        const auto rows = GakuLoader::Hotkeys::Snapshot();
        const auto row = std::find_if(rows.begin(), rows.end(), [](const auto& item) {
            return item.pluginId == "gakumotion" && item.actionId == "show-gui";
        });
        assert(row != rows.end());
        return *row;
    };
    assert(GakuLoader::Hotkeys::SetOverride(overrideFile, "GakuMotion", "Show-Gui", 'K', GAKU_HOTKEY_MOD_CTRL) == GAKU_E_UNAVAILABLE);
    assert(GakuLoader::Hotkeys::SetOverride(overrideFile, "gakumotion", "show-gui", 'K', GAKU_HOTKEY_MOD_CTRL) == GAKU_OK);
    assert(editedRow().virtualKey == 'K' && editedRow().overridden);
    assert(GakuLoader::ReadConfigValue(overrideFile, L"hotkeys", L"v2:67616b756d6f74696f6e:73686f772d677569") == L"Ctrl+K");
    assert(GakuLoader::ReadConfigValue(overrideFile, L"hotkeys", L"gakumotion.show-gui") == L"Ctrl+Shift+M");
    GakuLoader::Hotkeys::PollResult editedPoll;
    Hold(VK_CONTROL); Hold('K');
    GakuLoader::Hotkeys::Poll(Context(), editedPoll);
    assert(editedPoll.actions.empty());
    Release('K'); GakuLoader::Hotkeys::Poll(Context(), editedPoll);
    Hold('K'); GakuLoader::Hotkeys::Poll(Context(), editedPoll);
    assert(editedPoll.actions.size() == 1 && editedPoll.actions[0].owner == &temporaryOwner);
    GakuLoader::Hotkeys::Poll(Context(), editedPoll);
    assert(editedPoll.actions.empty());
    GakuLoader::Hotkeys::SetCaptureActive(true);
    Release('K'); GakuLoader::Hotkeys::Poll(Context(), editedPoll);
    Hold('K'); Hold(VK_F1); GakuLoader::Hotkeys::Poll(Context(), editedPoll);
    assert(editedPoll.actions.empty() && !editedPoll.managerToggle);
    GakuLoader::Hotkeys::SetCaptureActive(false);
    GakuLoader::Hotkeys::Poll(Context(), editedPoll);
    assert(editedPoll.actions.empty() && !editedPoll.managerToggle);
    Release('K'); Release(VK_CONTROL); Release(VK_F1); GakuLoader::Hotkeys::Poll(Context(), editedPoll);
    Hold(VK_F1); GakuLoader::Hotkeys::Poll(Context(), editedPoll);
    assert(editedPoll.managerToggle);
    Release(VK_F1); GakuLoader::Hotkeys::Poll(Context(), editedPoll);

    assert(GakuLoader::Hotkeys::SetOverride(overrideFile, "gakumotion", "show-gui", VK_F1, 0) == GAKU_E_CONFLICT);
    assert(GakuLoader::Hotkeys::SetOverride(overrideFile, "gakumotion", "show-gui", 300, 0) == GAKU_E_INVALID_ARGUMENT);
    assert(GakuLoader::Hotkeys::SetOverride(overrideFile, "gakumotion", "show-gui", 'K', 0x80000000) == GAKU_E_INVALID_ARGUMENT);
    assert(GakuLoader::Hotkeys::SetOverride(overrideFile, "gakumotion", "show-gui", 0, GAKU_HOTKEY_MOD_CTRL) == GAKU_E_INVALID_ARGUMENT);
    assert(GakuLoader::Hotkeys::SetOverride(overrideFile, "gakumotion", "show-gui", VK_CONTROL, 0) == GAKU_E_INVALID_ARGUMENT);
    assert(GakuLoader::Hotkeys::SetOverride(overrideFile, "gakumotion", "", 'K', 0) == GAKU_E_INVALID_ARGUMENT);
    assert(GakuLoader::Hotkeys::SetOverride(overrideFile, "gakumotion", "bad\nkey", 'K', 0) == GAKU_E_UNAVAILABLE);
    assert(GakuLoader::Hotkeys::SetOverride(overrideFile, "gakumotion", "missing", 'K', 0) == GAKU_E_UNAVAILABLE);
    assert(editedRow().virtualKey == 'K' && editedRow().modifiers == GAKU_HOTKEY_MOD_CTRL);

    const auto blockedOverride = root / L"hotkey-ini-directory";
    std::filesystem::create_directories(blockedOverride);
    assert(GakuLoader::Hotkeys::SetOverride(blockedOverride, "gakumotion", "show-gui", 'P', 0) == GAKU_E_INTERNAL);
    assert(GakuLoader::Hotkeys::ResetOverride(blockedOverride, "gakumotion", "show-gui") == GAKU_E_INTERNAL);
    assert(editedRow().virtualKey == 'K' && editedRow().modifiers == GAKU_HOTKEY_MOD_CTRL);
    // A plugin can update its declared default while the user's override stays
    // active; reset follows the latest declaration without deleting old aliases.
    overrideBinding.virtualKey = 'J'; overrideBinding.modifiers = 0;
    assert(GakuLoader::Hotkeys::Register(&temporaryOwner, "gakumotion", &overrideBinding) == GAKU_OK);
    assert(editedRow().virtualKey == 'K'); // failed write never changed the cache
    assert(GakuLoader::Hotkeys::ResetOverride(overrideFile, "gakumotion", "show-gui") == GAKU_OK);
    assert(editedRow().virtualKey == 'J' && !editedRow().overridden);
    assert(GakuLoader::ReadConfigValue(overrideFile, L"hotkeys", L"v2:67616b756d6f74696f6e:73686f772d677569") == L"<default>");
    assert(GakuLoader::ReadConfigValue(overrideFile, L"hotkeys", L"gakumotion.show-gui") == L"Ctrl+Shift+M");
    assert(GakuLoader::Hotkeys::SetOverride(overrideFile, "gakumotion", "show-gui", 0, 0) == GAKU_OK);
    assert(editedRow().unbound && editedRow().overridden);
    GakuLoader::Hotkeys::LoadOverrides(overrideFile);
    assert(GakuLoader::Hotkeys::Register(&temporaryOwner, "gakumotion", &overrideBinding) == GAKU_OK);
    assert(editedRow().unbound);
    assert(GakuLoader::Hotkeys::ResetOverride(overrideFile, "gakumotion", "show-gui") == GAKU_OK);

    int conflictingOwner = 0;
    GakuHotkeyBinding conflictingBinding{1, sizeof(GakuHotkeyBinding), "same-key", "Same key", 'J', 0, [](void*) {}, nullptr};
    assert(GakuLoader::Hotkeys::Register(&conflictingOwner, "edit-test", &conflictingBinding) == GAKU_OK);
    assert(editedRow().conflicted && ConflictCount() == 2);
    assert(GakuLoader::Hotkeys::SetOverride(overrideFile, "gakumotion", "show-gui", 'K', 0) == GAKU_OK);
    assert(!editedRow().conflicted && ConflictCount() == 0);
    assert(GakuLoader::Hotkeys::ResetOverride(overrideFile, "gakumotion", "show-gui") == GAKU_OK);
    assert(editedRow().conflicted && ConflictCount() == 2);
    GakuLoader::Hotkeys::RetireOwner(&conflictingOwner);
    assert(!editedRow().conflicted);

    // A captured key without a common short name round-trips through restart.
    assert(GakuLoader::Hotkeys::SetOverride(overrideFile, "gakumotion", "show-gui", VK_NUMPAD4, GAKU_HOTKEY_MOD_ALT) == GAKU_OK);
    GakuLoader::Hotkeys::RetireOwner(&temporaryOwner);

    TestHotkeyIdentities(root);
    GakuLoader::Hotkeys::LoadOverrides(overrideFile);
    assert(GakuLoader::Hotkeys::Register(&temporaryOwner, "gakumotion", &overrideBinding) == GAKU_OK);
    assert(editedRow().virtualKey == VK_NUMPAD4 && editedRow().modifiers == GAKU_HOTKEY_MOD_ALT);
    assert(GakuLoader::Hotkeys::ResetOverride(overrideFile, "gakumotion", "show-gui") == GAKU_OK);
    GakuLoader::Hotkeys::RetireOwner(&temporaryOwner);

    // --- Desktop panel dispatch --------------------------------------------
    GakuLoader::DispatchDesktopPanel("unknown.plugin");
    assert(ReadFixture(moduleA).desktopCount == 0 && ReadFixture(moduleB).desktopCount == 0);
    GakuLoader::DispatchDesktopPanel("fixture.b");
    assert(ReadFixture(moduleB).desktopCount == 1);
    GakuLoader::DispatchDesktopPanel("fixture.a");
    assert(ReadFixture(moduleA).desktopCount == 1);
    assert(GakuLoader::SnapshotDesktopHost(desktopSnapshot) && desktopSnapshot.plugins[0].activated);

    // --- Gate contention ---------------------------------------------------
    // A plugin callback on the main thread must reject the whole desktop frame.
    blockTick(0);
    std::atomic_uint rejectedBuildCalls{0};
    std::thread rejectedDesktop([&] {
        const auto deadline = GetTickCount64() + 2000;
        while (!blockTick(1) && GetTickCount64() < deadline) Sleep(1);
        assert(blockTick(1));
        assert(!GakuLoader::TryDispatchDesktopFrame([](void* value) { ++*static_cast<std::atomic_uint*>(value); }, &rejectedBuildCalls));
        GakuLoader::DesktopHostSnapshot whileBusy;
        assert(GakuLoader::SnapshotDesktopHost(whileBusy) && whileBusy.plugins.size() == 2);
        assert(whileBusy.plugins[0].runtimeReady && !whileBusy.shuttingDown);
        assert(!lifecycleA.isStopping(lifecycleA.context));
        blockTick(2);
    });
    GakuLoader::TickFrame(0.016f);
    rejectedDesktop.join();
    assert(rejectedBuildCalls == 0);

    // A held desktop frame must not starve actor updates.
    blockDesktop(0);
    std::thread holder([&] {
        assert(GakuLoader::TryDispatchDesktopFrame([](void*) {
            GakuLoader::DispatchDesktopPanel("fixture.a");
        }, nullptr));
    });
    const auto deadline = GetTickCount64() + 2000;
    while (!blockDesktop(1) && GetTickCount64() < deadline) Sleep(1);
    assert(blockDesktop(1));
    const auto waitingBegin = GetTickCount64();
    GakuLoader::TickActor(reinterpret_cast<void*>(uintptr_t{1}), 0.016f);
    const auto waitingElapsed = GetTickCount64() - waitingBegin;
    blockDesktop(2);
    holder.join();
    assert(waitingElapsed >= 10 && waitingElapsed < 2000);
    a = ReadFixture(moduleA);
    assert(a.actorACount == 2);
    assert(a.overlapCount == 0);

    // --- Owned hooks -------------------------------------------------------
    assert(hook(2) == 5);
    assert(hook(0) == GAKU_OK && hook(2) == 15);
    assert(hook(0) == GAKU_E_CONFLICT); // duplicate acquisition cannot steal an owner
    assert(hook(1) == GAKU_OK && hook(2) == 5);
    assert(hook(0) == GAKU_OK && hook(2) == 15); // retained trampoline supports same-owner reenable
    assert(GakuLoader::AcquireProcessLifetimeHostHook(reinterpret_cast<void*>(&PersistentTarget),
        reinterpret_cast<void*>(&PersistentDetour), reinterpret_cast<void**>(&persistentOriginal)) == GAKU_OK);
    assert(PersistentTarget(4) == 25);

    GakuHookService batchHooks{}, otherHooks{};
    assert(plugins[0].host.queryService(plugins[0].host.hostContext, GAKU_SERVICE_HOOKS,
        1, sizeof(batchHooks), &batchHooks) == GAKU_OK);
    assert(plugins[1].host.queryService(plugins[1].host.hostContext, GAKU_SERVICE_HOOKS,
        1, sizeof(otherHooks), &otherHooks) == GAKU_OK);
    GakuHookRequest batch[] = {
        {reinterpret_cast<void*>(&BatchTargetA), reinterpret_cast<void*>(&BatchDetourA), reinterpret_cast<void**>(&batchOriginalA)},
        {reinterpret_cast<void*>(&BatchTargetB), reinterpret_cast<void*>(&BatchDetourB), reinterpret_cast<void**>(&batchOriginalB)}
    };
    const auto goodSecond = batch[1];
    batch[1].target = reinterpret_cast<void*>(uintptr_t{1}); // create fails after A was prepared
    assert(batchHooks.acquireBatch(batchHooks.context, 2, batch) == GAKU_E_INTERNAL);
    assert(!batchOriginalA && !batchOriginalB && BatchTargetA(4) == 6);
    batch[1] = batch[0];
    assert(batchHooks.acquireBatch(batchHooks.context, 2, batch) == GAKU_E_INVALID_ARGUMENT);
    assert(!batchOriginalA && !batchOriginalB);
    batch[1] = goodSecond;
    batch[1].original = batch[0].original;
    assert(batchHooks.acquireBatch(batchHooks.context, 2, batch) == GAKU_E_INVALID_ARGUMENT);
    batch[1] = goodSecond;
    GakuLoader::SetHookEnableObserverForTest(FirstEnabled);
    assert(batchHooks.acquireBatch(batchHooks.context, 2, batch) == GAKU_OK);
    GakuLoader::SetHookEnableObserverForTest(nullptr);
    assert(BatchTargetA(4) == 113 && BatchTargetB(4) == 207);
    assert(batchHooks.release(batchHooks.context, batch[0].target) == GAKU_OK);
    assert(batchHooks.release(batchHooks.context, batch[1].target) == GAKU_OK);
    const auto retainedA = batchOriginalA;
    assert(otherHooks.acquireBatch(otherHooks.context, 2, batch) == GAKU_E_CONFLICT);
    assert(batchOriginalA == retainedA && batchOriginalB(4) == 7);
    GakuLoader::FailHookEnableForTest(2);
    assert(batchHooks.acquireBatch(batchHooks.context, 2, batch) == GAKU_E_INTERNAL);
    GakuLoader::FailHookEnableForTest(0);
    assert(BatchTargetA(4) == 6 && BatchTargetB(4) == 7);
    assert(batchOriginalA == retainedA && batchOriginalA(4) == 6 && batchOriginalB(4) == 7);
    assert(batchHooks.acquireBatch(batchHooks.context, 2, batch) == GAKU_OK);

    // --- Optional process-lifetime protection leases -----------------------
    GakuProcessLifetimeHooksV1 retainA{}, retainB{};
    memset(&retainA, 0x7b, sizeof(retainA)); const auto untouchedRetain = retainA;
    assert(plugins[0].host.queryService(plugins[0].host.hostContext, GAKU_SERVICE_PROCESS_LIFETIME_HOOKS,
        2, sizeof(retainA), &retainA) == GAKU_E_VERSION);
    assert(memcmp(&retainA, &untouchedRetain, sizeof(retainA)) == 0);
    assert(plugins[0].host.queryService(plugins[0].host.hostContext, GAKU_SERVICE_PROCESS_LIFETIME_HOOKS,
        1, sizeof(retainA) - 1, &retainA) == GAKU_E_SIZE);
    assert(memcmp(&retainA, &untouchedRetain, sizeof(retainA)) == 0);
    assert(plugins[0].host.queryService(plugins[0].host.hostContext, GAKU_SERVICE_PROCESS_LIFETIME_HOOKS,
        1, sizeof(retainA), &retainA) == GAKU_OK);
    assert(plugins[1].host.queryService(plugins[1].host.hostContext, GAKU_SERVICE_PROCESS_LIFETIME_HOOKS,
        1, sizeof(retainB), &retainB) == GAKU_OK);
    assert(retainB.context == otherHooks.context && retainB.retainOwnedHooks);
    GakuHookRequest retainBatch[] = {
        {reinterpret_cast<void*>(&RetainedTargetA), reinterpret_cast<void*>(&RetainedDetourA), reinterpret_cast<void**>(&retainedOriginalA)},
        {reinterpret_cast<void*>(&RetainedTargetB), reinterpret_cast<void*>(&RetainedDetourB), reinterpret_cast<void**>(&retainedOriginalB)}
    };
    assert(otherHooks.acquireBatch(otherHooks.context, 2, retainBatch) == GAKU_OK);
    void* retainedTargets[]{retainBatch[0].target, retainBatch[1].target};
    void* badRetain[]{retainedTargets[0], nullptr};
    assert(retainB.retainOwnedHooks(retainB.context, 2, badRetain) == GAKU_E_INVALID_ARGUMENT);
    assert(otherHooks.release(otherHooks.context, retainedTargets[0]) == GAKU_OK); // no partial promotion
    assert(otherHooks.acquire(otherHooks.context, retainBatch[0].target, retainBatch[0].detour, retainBatch[0].original) == GAKU_OK);
    badRetain[1] = retainedTargets[0];
    assert(retainB.retainOwnedHooks(retainB.context, 2, badRetain) == GAKU_E_INVALID_ARGUMENT);
    assert(otherHooks.release(otherHooks.context, retainedTargets[0]) == GAKU_OK);
    assert(otherHooks.acquire(otherHooks.context, retainBatch[0].target, retainBatch[0].detour, retainBatch[0].original) == GAKU_OK);
    badRetain[1] = reinterpret_cast<void*>(uintptr_t{1});
    assert(retainB.retainOwnedHooks(retainB.context, 2, badRetain) == GAKU_E_UNAVAILABLE);
    assert(otherHooks.release(otherHooks.context, retainedTargets[0]) == GAKU_OK);
    assert(otherHooks.acquire(otherHooks.context, retainBatch[0].target, retainBatch[0].detour, retainBatch[0].original) == GAKU_OK);
    badRetain[1] = batch[0].target;
    assert(retainB.retainOwnedHooks(retainB.context, 2, badRetain) == GAKU_E_CONFLICT);
    assert(otherHooks.release(otherHooks.context, retainedTargets[0]) == GAKU_OK);
    assert(otherHooks.acquire(otherHooks.context, retainBatch[0].target, retainBatch[0].detour, retainBatch[0].original) == GAKU_OK);
    assert(otherHooks.release(otherHooks.context, retainedTargets[1]) == GAKU_OK);
    assert(retainB.retainOwnedHooks(retainB.context, 2, retainedTargets) == GAKU_E_UNAVAILABLE);
    assert(otherHooks.release(otherHooks.context, retainedTargets[0]) == GAKU_OK);
    assert(otherHooks.acquireBatch(otherHooks.context, 2, retainBatch) == GAKU_OK);
    assert(retainA.retainOwnedHooks(retainA.context, 2, retainedTargets) == GAKU_E_CONFLICT);
    assert(retainB.retainOwnedHooks(reinterpret_cast<void*>(uintptr_t{1}), 2, retainedTargets) == GAKU_E_INVALID_ARGUMENT);
    assert(retainB.retainOwnedHooks(retainB.context, 0, retainedTargets) == GAKU_E_INVALID_ARGUMENT);
    assert(retainB.retainOwnedHooks(retainB.context, 257, retainedTargets) == GAKU_E_INVALID_ARGUMENT);
    assert(retainB.retainOwnedHooks(retainB.context, 2, retainedTargets) == GAKU_OK);
    assert(retainB.retainOwnedHooks(retainB.context, 2, retainedTargets) == GAKU_OK); // idempotent
    assert(otherHooks.release(otherHooks.context, retainedTargets[0]) == GAKU_E_CONFLICT);
    assert(otherHooks.release(otherHooks.context, retainedTargets[1]) == GAKU_E_CONFLICT);
    assert(RetainedTargetA(4) == 315 && RetainedTargetB(4) == 416);
    void* unpromotedTarget = reinterpret_cast<void*>(&UnpromotedTarget);
    assert(otherHooks.acquire(otherHooks.context, unpromotedTarget, reinterpret_cast<void*>(&UnpromotedDetour),
        reinterpret_cast<void**>(&unpromotedOriginal)) == GAKU_OK);
    void* rollbackTargets[]{unpromotedTarget, batch[0].target};
    assert(retainB.retainOwnedHooks(retainB.context, 2, rollbackTargets) == GAKU_E_CONFLICT);
    assert(UnpromotedTarget(4) == 517);

    // --- Shared actor entry -------------------------------------------------
    // The loader keeps the only detour on the game's actor update; a plugin that
    // needs that entry registers a participant rather than a second lease.
    GakuActorEntryService entryA{};
    assert(plugins[0].host.queryService(plugins[0].host.hostContext, GAKU_SERVICE_ACTOR_ENTRY,
        GAKU_SERVICE_VERSION, sizeof(entryA), &entryA) == GAKU_OK);
    assert(entryA.context == plugins[0].host.hostContext && entryA.registerEntry && entryA.unregisterEntry);
    auto participant = Binding(GAKU_ACTOR_ENTRY_BEFORE, "invalid", RecordBeforeA);
    participant.structSize = sizeof(participant) - 1;
    assert(entryA.registerEntry(entryA.context, &participant) == GAKU_E_SIZE);
    participant = Binding(0, "invalid", RecordBeforeA);
    assert(entryA.registerEntry(entryA.context, &participant) == GAKU_E_INVALID_ARGUMENT);
    participant = Binding(GAKU_ACTOR_ENTRY_BEFORE, "invalid", nullptr);
    assert(entryA.registerEntry(entryA.context, &participant) == GAKU_E_INVALID_ARGUMENT);
    participant = Binding(GAKU_ACTOR_ENTRY_BEFORE, "invalid", RecordBeforeA);
    participant.abiVersion = GAKU_SERVICE_VERSION + 1;
    assert(entryA.registerEntry(entryA.context, &participant) == GAKU_E_VERSION);
    assert(GakuLoader::ActorEntry::Count() == 0);

    auto beforeA = Binding(GAKU_ACTOR_ENTRY_BEFORE, "a.before", RecordBeforeA);
    auto afterA = Binding(GAKU_ACTOR_ENTRY_AFTER, "a.after", RecordAfterA);
    auto beforeB = Binding(GAKU_ACTOR_ENTRY_BEFORE, "b.before", RecordBeforeB);
    assert(entryA.registerEntry(entryA.context, &beforeA) == GAKU_OK);
    assert(entryA.registerEntry(entryA.context, &afterA) == GAKU_OK);
    GakuActorEntryService entryB{};
    assert(plugins[1].host.queryService(plugins[1].host.hostContext, GAKU_SERVICE_ACTOR_ENTRY,
        GAKU_SERVICE_VERSION, sizeof(entryB), &entryB) == GAKU_OK);
    assert(entryB.registerEntry(entryB.context, &beforeB) == GAKU_OK);
    assert(GakuLoader::ActorEntry::Count() == 3);

    void* actor = reinterpret_cast<void*>(uintptr_t{9});
    g_actorEntryEvents.clear(); g_originalCalls = 0;
    GakuLoader::ActorEntry::Invoke(actor, CountOriginal, nullptr);
    assert((g_actorEntryEvents == std::vector<std::string>{"before.a", "before.b", "original", "after.a"}));
    assert(g_originalCalls == 1);

    // Same owner and phase replaces in place: no duplicate and no reordering.
    assert(entryA.registerEntry(entryA.context, &beforeA) == GAKU_OK);
    assert(GakuLoader::ActorEntry::Count() == 3);
    g_actorEntryEvents.clear(); g_originalCalls = 0;
    GakuLoader::ActorEntry::Invoke(actor, CountOriginal, nullptr);
    assert((g_actorEntryEvents == std::vector<std::string>{"before.a", "before.b", "original", "after.a"}));
    assert(g_originalCalls == 1);

    // A throwing participant is contained: the original and the other phase
    // still run. Nothing crosses the ABI.
    auto throwing = Binding(GAKU_ACTOR_ENTRY_AFTER, "a.throws", RecordAfterThrows);
    assert(entryA.registerEntry(entryA.context, &throwing) == GAKU_OK);
    g_actorEntryEvents.clear(); g_originalCalls = 0;
    GakuLoader::ActorEntry::Invoke(actor, CountOriginal, nullptr);
    assert((g_actorEntryEvents == std::vector<std::string>{"before.a", "before.b", "original", "before.throws"}));
    assert(g_originalCalls == 1);

    // Re-registration is a new immutable entry. The invocation that is already
    // running keeps the after callback it snapshotted, even though the before
    // participant replaced it mid-call; the next invocation sees the new one.
    auto afterOld = Binding(GAKU_ACTOR_ENTRY_AFTER, "a.after", RecordAfterOld);
    assert(entryA.registerEntry(entryA.context, &afterOld) == GAKU_OK);
    g_registeringEntryService = &entryA;
    auto replacingBefore = Binding(GAKU_ACTOR_ENTRY_BEFORE, "a.before", ReplacingBefore);
    assert(entryA.registerEntry(entryA.context, &replacingBefore) == GAKU_OK);
    g_actorEntryEvents.clear(); g_originalCalls = 0;
    GakuLoader::ActorEntry::Invoke(actor, CountOriginal, nullptr);
    assert((g_actorEntryEvents == std::vector<std::string>{"before.replacing", "before.b", "original", "after.old"}));
    g_actorEntryEvents.clear(); g_originalCalls = 0;
    GakuLoader::ActorEntry::Invoke(actor, CountOriginal, nullptr);
    assert((g_actorEntryEvents == std::vector<std::string>{"before.replacing", "before.b", "original", "after.new"}));
    g_registeringEntryService = nullptr;
    assert(entryA.registerEntry(entryA.context, &beforeA) == GAKU_OK); // restore the base sequence
    assert(entryA.registerEntry(entryA.context, &afterA) == GAKU_OK);

    // The loader's own per-actor dispatch still follows the shared phases.
    assert(entryA.unregisterEntry(entryA.context, GAKU_ACTOR_ENTRY_AFTER) == GAKU_OK);
    assert(entryA.unregisterEntry(entryA.context, GAKU_ACTOR_ENTRY_AFTER) == GAKU_E_UNAVAILABLE);
    const auto ticksBeforeSharedEntry = ReadFixture(moduleA).actorTickCount;
    g_actorEntryEvents.clear();
    GakuLoader::ActorEntry::Invoke(actor, CountOriginal, nullptr);
    GakuLoader::TickActor(actor, 0.016f);
    assert(ReadFixture(moduleA).actorTickCount == ticksBeforeSharedEntry + 1);
    assert((g_actorEntryEvents == std::vector<std::string>{"before.a", "before.b", "original"}));
    assert(entryB.unregisterEntry(entryB.context, GAKU_ACTOR_ENTRY_BEFORE) == GAKU_OK);
    assert(GakuLoader::ActorEntry::Count() == 1);

    // --- Registration vs owner retirement ----------------------------------
    // Retirement clears `accepting` and removes participants under the owner
    // lock. A registration that already passed the acceptance check must finish
    // before that removal, never after it; otherwise the retired owner keeps a
    // callback that still holds its now-invalid userData.
    g_registerPaused = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_registerResume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    assert(g_registerPaused && g_registerResume);
    GakuLoader::SetActorEntryRegisterObserverForTest(PauseRegistration);
    std::atomic_int registrationStatus{999};
    std::thread lateRegistration([&] {
        auto late = Binding(GAKU_ACTOR_ENTRY_BEFORE, "b.before", RecordBeforeB);
        registrationStatus = entryB.registerEntry(entryB.context, &late);
    });
    assert(WaitForSingleObject(g_registerPaused, 5000) == WAIT_OBJECT_0);
    std::thread retireB([&] { GakuLoader::RetireOwnerForTest(plugins[1].host.hostContext); });
    Sleep(100); // a split check/insert lets the retire finish inside this pause
    SetEvent(g_registerResume);
    lateRegistration.join();
    retireB.join();
    assert(lifecycleB.isStopping(lifecycleB.context) && !lifecycleA.isStopping(lifecycleA.context));
    GakuLoader::SetActorEntryRegisterObserverForTest(nullptr);
    CloseHandle(g_registerPaused);
    CloseHandle(g_registerResume);
    g_registerPaused = g_registerResume = nullptr;
    assert(registrationStatus == GAKU_OK); // the paused call finished before retirement
    assert(entryB.registerEntry(entryB.context, &beforeB) == GAKU_E_SHUTTING_DOWN);
    g_actorEntryEvents.clear(); g_originalCalls = 0;
    GakuLoader::ActorEntry::Invoke(actor, CountOriginal, nullptr);
    assert((g_actorEntryEvents == std::vector<std::string>{"before.a", "original"}));
    assert(GakuLoader::ActorEntry::Count() == 1); // no participant survived the retired owner
    assert(RetainedTargetA(4) == 315 && RetainedTargetB(4) == 416); // promoted hooks survive real owner retirement
    assert(UnpromotedTarget(4) == 17); // failed promotion was all-or-none
    assert(retainB.retainOwnedHooks(retainB.context, 2, retainedTargets) == GAKU_E_SHUTTING_DOWN);
    assert(otherHooks.release(otherHooks.context, retainedTargets[0]) == GAKU_E_CONFLICT);

    // --- Participants vs the desktop callback gate -------------------------
    // A desktop frame holds the callback gate on another thread. The actor
    // thread's participant must stay out until that frame completes, the game
    // original must still run exactly once, and every actor must reach the
    // participant afterwards.
    g_desktopGateHeld = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_desktopGateRelease = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    assert(g_desktopGateHeld && g_desktopGateRelease);
    auto gateParticipant = Binding(GAKU_ACTOR_ENTRY_BEFORE, "gate.before", GateParticipant);
    assert(entryA.registerEntry(entryA.context, &gateParticipant) == GAKU_OK);
    std::atomic_bool frameAdmitted{false};
    std::thread desktop([&] {
        frameAdmitted.store(GakuLoader::TryDispatchDesktopFrame(HoldDesktopGate, nullptr));
    });
    assert(WaitForSingleObject(g_desktopGateHeld, 5000) == WAIT_OBJECT_0);
    std::atomic_bool actorInvokeStarted{false};
    std::thread actorInvoke([&] {
        actorInvokeStarted.store(true, std::memory_order_release);
        GakuLoader::ActorEntry::Invoke(reinterpret_cast<void*>(uintptr_t{21}), GateOriginal, nullptr);
    });
    while (!actorInvokeStarted.load(std::memory_order_acquire)) std::this_thread::yield();
    Sleep(150); // the participant is parked on the gate, not merely slow
    assert(g_gateParticipantCalls.load() == 0);
    assert(g_gateOriginalCalls == 0);
    SetEvent(g_desktopGateRelease);
    desktop.join();
    actorInvoke.join();
    assert(frameAdmitted.load());
    assert(!g_participantSawDesktopFrame.load()); // never overlapped the frame build
    assert(g_gateParticipantCalls.load() == 1);
    assert(g_gateOriginalCalls == 1);
    assert(g_gateLastActor == reinterpret_cast<void*>(uintptr_t{21}));
    // Gate free again: two distinct actors both arrive, each with one original.
    g_gateParticipantCalls.store(0);
    g_gateOriginalCalls = 0;
    auto* firstActor = reinterpret_cast<void*>(uintptr_t{31});
    auto* secondActor = reinterpret_cast<void*>(uintptr_t{32});
    GakuLoader::ActorEntry::Invoke(firstActor, GateOriginal, nullptr);
    GakuLoader::ActorEntry::Invoke(secondActor, GateOriginal, nullptr);
    assert(g_gateParticipantCalls.load() == 2 && g_gateOriginalCalls == 2);
    assert(g_gateLastActor == secondActor);
    CloseHandle(g_desktopGateHeld);
    CloseHandle(g_desktopGateRelease);
    g_desktopGateHeld = g_desktopGateRelease = nullptr;

    // --- Shutdown ----------------------------------------------------------
    assert(enqueue() == GAKU_OK);
    std::thread shutdown([] { GakuLoader::Shutdown(); });
    shutdown.join();
    assert(lifecycleA.isStopping(lifecycleA.context));
    assert(ReadFixture(moduleA).shutdownCount == 0); // never teardown on a watchdog thread
    GakuHotkeyService retiringHotkeys{};
    assert(plugins[0].host.queryService(plugins[0].host.hostContext, GAKU_SERVICE_HOTKEYS,
        1, sizeof(retiringHotkeys), &retiringHotkeys) == GAKU_OK);
    std::atomic_bool stopRegistering{false};
    std::thread registering([&] {
        while (!stopRegistering) {
            const auto result = retiringHotkeys.registerHotkey(retiringHotkeys.context, &overrideBinding);
            assert(result == GAKU_OK || result == GAKU_E_SHUTTING_DOWN);
            if (result == GAKU_E_SHUTTING_DOWN) break;
            std::this_thread::yield();
        }
    });
    GakuLoader::TickFrame(0.016f);
    stopRegistering = true;
    registering.join();
    assert(retiringHotkeys.registerHotkey(retiringHotkeys.context, &overrideBinding) == GAKU_E_SHUTTING_DOWN);
    a = ReadFixture(moduleA);
    b = ReadFixture(moduleB);
    assert(a.shutdownCount == 1 && a.shutdownThread == mainThread);
    assert(b.shutdownCount == 1 && b.shutdownThread == mainThread);
    assert(GakuLoader::Hotkeys::Snapshot().empty()); // bindings retired with their owners
    assert(GakuLoader::ActorEntry::Count() == 0);    // participants retired with their owners
    assert(a.taskCount == 1); // queued work canceled at shutdown
    assert(enqueue() == GAKU_E_SHUTTING_DOWN);
    assert(hook(2) == 5); // owner retirement disabled its hook without a global operation
    GakuLoader::DisableOwnedHooksForExit();
    assert(RetainedTargetA(4) == 315 && RetainedTargetB(4) == 416); // survives exit's batched disable too
    assert(PersistentTarget(4) == 25); // diagnostic seam stays protected during unwind
    assert(GakuLoader::ReleaseHostHook(reinterpret_cast<void*>(&PersistentTarget)) == GAKU_OK);
    assert(PersistentTarget(4) == 5);
    GakuLoader::Shutdown(); // idempotent
    assert(ReadFixture(moduleA).shutdownCount == 1 && ReadFixture(moduleA).contractFailures == 0);

    // ABI size failures must never touch caller storage or its canary.
    struct { unsigned char data[sizeof(GakuLogService)]; uint64_t canary; } output{};
    memset(&output, 0x7b, sizeof(output)); const auto beforeOutput = output;
    GakuServiceHeader header{1, sizeof(header)};
    assert(GakuLoader::CopyServiceTable(&header, sizeof(header), 2, sizeof(output), &output) == GAKU_E_VERSION);
    assert(memcmp(&beforeOutput, &output, sizeof(output)) == 0);

    puts("PASS: native activation before runtime; worker-thread runtime-ready; actor-free frame pump; FIFO batch; "
         "service ownership; shared actor entry phases/original-once/replacement/immutable-snapshot/containment/"
         "retirement/registration-interleave/desktop-gate-serialization; "
         "hotkey conflict/edge/focus/text/manager/rebind/persistence/reset/unbind/capture/write-failure/"
         "exact-identity/legacy-ambiguity/case/restart/long-section/truncation semantics; owned hook batch publication/rollback; "
         "process-lifetime promotion/validation/atomic-rollback/retirement/exit; whole-frame admission; "
         "pre-reset DLL refusal; main-thread shutdown");
    return 0;
}
