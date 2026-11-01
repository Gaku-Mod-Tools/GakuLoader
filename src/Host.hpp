#pragma once
#include "GakuPlugin.h"
#include <filesystem>
#include <string>
#include <vector>
#include <windows.h>

namespace GakuLoader {

struct LoadedPlugin {
    std::wstring dllPath;
    HMODULE handle{nullptr};
    std::string id, name, version;
    GakuPluginDescriptor descriptor{};
    GakuHostApi host{};
    void* state{nullptr};
    void* owner{nullptr};
    bool activated{false};
    bool runtimeReady{false};
    bool failed{false};
};

// Roots only. Plugin DLLs are not touched until ActivatePlugins().
void Initialize(const std::filesystem::path& gameRoot, const std::filesystem::path& localRoot);
// Early native activation: loads every enabled DLL and calls its activate
// callback before the game runtime is resolvable. Never call from DllMain.
void ActivatePlugins();
// IL2CPP/GameAssembly became resolvable. Safe from a loader worker thread; the
// plugin contract forbids managed-object work here.
void NotifyRuntimeReady();
// Existing queue dispatch at a known Unity-thread startup boundary. This does
// not synthesize a frame or invoke plugin ticks, and is not a plugin ABI phase.
void DispatchMainThreadTasks();
// Infrastructure supplies an independently confirmed Unity window thread id;
// a plugin frame callback never gets to claim main-thread identity.
bool BindMainThread(DWORD threadId);
// Unity main thread, at most once per frame, with or without actors.
void TickFrame(float deltaTime);
// Unity main thread, once per live actor after its native LateUpdate.
void TickActor(void* activeActor, float deltaTime);
bool IsRuntimeReady();
bool IsShuttingDown();

// All-or-nothing CPU frame admission. Busy frames leave the previous desktop
// backbuffer intact; GPU submission and native dialogs run after this returns.
bool TryDispatchDesktopFrame(void (*buildFrame)(void*), void* userData);
// Loader-internal, not part of the plugin ABI: runs `body` inside the same gate
// that serializes tick and desktop callbacks. The shared actor entry uses it so
// a participant can never read a field while the desktop UI writes it. The game
// original is deliberately not covered - it is not plugin state and must not
// extend the gate. Announcing a main-thread waiter makes desktop frames yield
// instead of starving the actor.
void RunInCallbackGate(void (*body)(void*), void* userData);
struct DesktopPluginPanel {
    std::string id, name, version;
    bool activated{false}, runtimeReady{false}, failed{false}, hasPanel{false};
};
struct DesktopHostSnapshot {
    std::vector<DesktopPluginPanel> plugins;
    bool runtimeReady{false}, shuttingDown{false}, activated{false};
};
// Non-blocking copies and selected-panel dispatch; UI never borrows the list.
bool SnapshotDesktopHost(DesktopHostSnapshot& output);
void DispatchDesktopPanel(const char* pluginId);
// On other threads this requests shutdown; callbacks run at next main dispatch.
// UnityQuit calls this on the main thread before disabling owned hooks.
void Shutdown();

// These helpers are for loader-owned infrastructure only. Plugins use
// gakuloader.hooks.
GakuStatus AcquireHostHook(void* target, void* detour, void** original);
// Narrow diagnostic seams remain protected until the process has exited.
GakuStatus AcquireProcessLifetimeHostHook(void* target, void* detour, void** original);
GakuStatus ReleaseHostHook(void* target);
// Marks the process as leaving before plugin shutdown. Owner retirement then
// leaves hook leases to DisableOwnedHooksForExit's single batched disable.
void BeginExitTeardown();
void DisableOwnedHooksForExit();
bool CanShutdownOnCurrentThread();
// Non-blocking snapshot for QuitGuard: never acquires callback/service gates.
void LogShutdownProgress();

void LogInfo(const char* fmt, ...);
void LogWarn(const char* fmt, ...);
void LogError(const char* fmt, ...);
std::filesystem::path LogPath();
std::filesystem::path ConfigPath();
const std::vector<LoadedPlugin>& GetLoadedPlugins();

#if defined(GAKULOADER_TEST_SEAMS)
// Compiled only by the host contract tests: the real poller owns a Win32 thread
// and would make key assertions non-deterministic.
namespace Hotkeys { struct PollContext; }
void DisableHotkeyPollerForTest();
// Runs exactly the production poll->dispatch path once; returns the F1 edge.
bool PumpHotkeysForTest(const Hotkeys::PollContext& context);
void SetHookEnableObserverForTest(void (*observer)(uint32_t enabledCount));
void FailHookEnableForTest(uint32_t oneBasedIndex);
// Runs between the owner-acceptance check and the participant insert, inside
// the same critical section. The interleave test parks here while another
// thread retires the owner.
void SetActorEntryRegisterObserverForTest(void (*observer)());
// Production owner retirement (services, hotkeys, participants, hook leases).
void RetireOwnerForTest(void* owner);
#endif
}
