#include "GakuTestCompat.hpp"
#include "GakuPlugin.h"
#include "GakuFixtureContract.hpp"
#include <windows.h>
#include <atomic>
#include <cstring>

// Host-contract fixture for the GakuLoader ABI. Two variants are compiled from
// this file so the hotkey registry can observe two real owners claiming the
// same combo.
#ifndef FIXTURE_VARIANT
#define FIXTURE_VARIANT 1
#endif

namespace {
GakuHostApi host{};
GakuMainThreadService queue{};
GakuHookService hooks{};
GakuHotkeyService hotkeys{};
std::atomic_uint activateCount{0}, readyCount{0}, ticks{0}, actorTicks{0}, desktops{0}, shutdowns{0};
std::atomic_uint activateThread{0}, readyThread{0}, tickThread{0}, shutdownThread{0}, hotkeyThread{0};
std::atomic_uint tasks{0}, nestedTasks{0}, active{0}, overlaps{0}, failures{0};
std::atomic_uint actorA{0}, actorB{0}, otherActor{0};
std::atomic_uint hotkeyCount{0}, hotkeyRegistrations{0};
std::atomic_int hotkeyStatus{0};
std::atomic_bool blockDesktop{false}, desktopEntered{false}, releaseDesktop{false};
std::atomic_bool blockTick{false}, tickEntered{false}, releaseTick{false};
std::atomic_bool hotkeysRegistered{false};

#if FIXTURE_VARIANT == 1
constexpr const char* kId = "fixture.a";
constexpr const char* kName = "Fixture A";
constexpr uint32_t kSecondVirtualKey = 0x78; // F9
constexpr uint32_t kSecondModifiers = 0;
constexpr const char* kSecondAction = "fixture.beta";
constexpr const char* kServiceName = "fixture.value.a";
#else
constexpr const char* kId = "fixture.b";
constexpr const char* kName = "Fixture B";
constexpr uint32_t kSecondVirtualKey = 0x79; // F10
constexpr uint32_t kSecondModifiers = GAKU_HOTKEY_MOD_CTRL | GAKU_HOTKEY_MOD_SHIFT;
constexpr const char* kSecondAction = "fixture.gamma";
constexpr const char* kServiceName = "fixture.value.b";
#endif

struct Provider { uint32_t abiVersion; uint32_t structSize; uint32_t value; };
void Check(bool good) { if (!good) failures.fetch_add(1); }
void Nested(void*) { nestedTasks.fetch_add(1); }
void Task(void*) {
    tasks.fetch_add(1);
    Check(queue.isMainThread(queue.context) == 1);
    Check(queue.enqueue(queue.context, Nested, nullptr) == GAKU_OK);
}
void Enter() { if (active.fetch_add(1) != 0) overlaps.fetch_add(1); }
void Leave() { active.fetch_sub(1); }
void Hotkey(void*) {
    hotkeyCount.fetch_add(1);
    hotkeyThread.store(GetCurrentThreadId());
}
void RegisterHotkeys() {
    if (hotkeysRegistered.exchange(true)) return;
    GakuHotkeyBinding alpha{GAKU_SERVICE_VERSION, sizeof(GakuHotkeyBinding), "fixture.alpha",
        "Fixture alpha", 'M', GAKU_HOTKEY_MOD_CTRL, Hotkey, nullptr};
    const auto alphaStatus = hotkeys.registerHotkey(hotkeys.context, &alpha);
    if (alphaStatus == GAKU_OK) hotkeyRegistrations.fetch_add(1);
    hotkeyStatus.store(alphaStatus);
    GakuHotkeyBinding second{GAKU_SERVICE_VERSION, sizeof(GakuHotkeyBinding), kSecondAction,
        "Fixture second", kSecondVirtualKey, kSecondModifiers, Hotkey, nullptr};
    if (hotkeys.registerHotkey(hotkeys.context, &second) == GAKU_OK) hotkeyRegistrations.fetch_add(1);
    // F1 is reserved by the manager and must be refused per binding.
    GakuHotkeyBinding reserved{GAKU_SERVICE_VERSION, sizeof(GakuHotkeyBinding), "fixture.f1",
        "Reserved", 0x70, 0, Hotkey, nullptr};
    Check(hotkeys.registerHotkey(hotkeys.context, &reserved) == GAKU_E_CONFLICT);
}
GakuStatus Activate(const GakuHostApi* api, void** state) {
    host = *api;
    *state = &host;
    activateCount.fetch_add(1);
    activateThread.store(GetCurrentThreadId());
    Check(host.abiVersion == GAKU_ABI_VERSION && host.structSize == sizeof(GakuHostApi));
    Check(host.queryService && host.registerService);
    GakuRuntimeService runtime{};
    Check(host.queryService(host.hostContext, GAKU_SERVICE_RUNTIME, GAKU_SERVICE_VERSION, sizeof(runtime), &runtime) == GAKU_OK);
    Check(runtime.gameRoot && runtime.localRoot && runtime.pluginsRoot && runtime.getGameAssembly);
    Check(host.queryService(host.hostContext, GAKU_SERVICE_MAIN_THREAD, GAKU_SERVICE_VERSION, sizeof(queue), &queue) == GAKU_OK);
    Check(host.queryService(host.hostContext, GAKU_SERVICE_HOOKS, GAKU_SERVICE_VERSION, sizeof(hooks), &hooks) == GAKU_OK);
    Check(host.queryService(host.hostContext, GAKU_SERVICE_HOTKEYS, GAKU_SERVICE_VERSION, sizeof(hotkeys), &hotkeys) == GAKU_OK);
    // Activation is native and pre-runtime: this thread is not the Unity main thread.
    Check(queue.isMainThread(queue.context) == 0);
    GakuLogService log{};
    memset(&log, 0x5a, sizeof(log)); const GakuLogService sentinel = log;
    Check(host.queryService(host.hostContext, GAKU_SERVICE_LOG, GAKU_SERVICE_VERSION, sizeof(log) - 1, &log) == GAKU_E_SIZE);
    Check(memcmp(&log, &sentinel, sizeof(log)) == 0);
    Check(host.queryService(host.hostContext, GAKU_SERVICE_LOG, GAKU_SERVICE_VERSION + 1, sizeof(log), &log) == GAKU_E_VERSION);
    Check(memcmp(&log, &sentinel, sizeof(log)) == 0);
    Check(host.queryService(host.hostContext, "missing.service", GAKU_SERVICE_VERSION, sizeof(log), &log) == GAKU_E_UNAVAILABLE);
    Check(host.queryService(host.hostContext, GAKU_SERVICE_LOG, GAKU_SERVICE_VERSION, sizeof(log), &log) == GAKU_OK);
    Check(log.write != nullptr);
    log.write(log.context, GAKU_LOG_INFO, "literal %s %n fixture log");
    Provider service{7, sizeof(Provider), 12345};
    Check(host.registerService(host.hostContext, kServiceName, sizeof(service), &service) == GAKU_OK);
    Check(host.registerService(host.hostContext, kServiceName, sizeof(service), &service) == GAKU_E_CONFLICT);
    Check(host.registerService(host.hostContext, "gakuloader.forbidden", sizeof(service), &service) == GAKU_E_CONFLICT);
    service.value = 0; Provider copied{};
    Check(host.queryService(host.hostContext, kServiceName, 7, sizeof(copied), &copied) == GAKU_OK && copied.value == 12345);
    RegisterHotkeys();
    Check(queue.enqueue(queue.context, Task, nullptr) == GAKU_OK);
    return GAKU_OK;
}
void RuntimeReady(void*) {
    readyCount.fetch_add(1);
    readyThread.store(GetCurrentThreadId());
}
void Tick(void*, float) {
    Enter();
    ticks.fetch_add(1);
    tickThread.store(GetCurrentThreadId());
    Check(queue.isMainThread(queue.context) == 1);
    if (blockTick.load()) {
        tickEntered = true;
        const auto deadline = GetTickCount64() + 2000;
        while (!releaseTick && GetTickCount64() < deadline) Sleep(1);
    }
    Sleep(2); Leave();
}
void ActorTick(void*, void* actor, float) {
    Enter();
    actorTicks.fetch_add(1);
    if (actor == reinterpret_cast<void*>(uintptr_t{1})) ++actorA;
    else if (actor == reinterpret_cast<void*>(uintptr_t{2})) ++actorB;
    else ++otherActor;
    Sleep(2); Leave();
}
void Desktop(void*) {
    Enter();
    desktops.fetch_add(1);
    if (blockDesktop.load()) {
        desktopEntered.store(true);
        // A finite timeout makes regressions fail a timing assertion rather
        // than hanging the test suite behind a simulated modal callback.
        const auto deadline = GetTickCount64() + 500;
        while (!releaseDesktop.load() && GetTickCount64() < deadline) Sleep(1);
    }
    Sleep(1); Leave();
}
void Shutdown(void*) {
    Enter(); shutdowns.fetch_add(1); shutdownThread.store(GetCurrentThreadId()); Leave();
}
void UnregisterHotkeys() {
    hotkeys.unregisterHotkey(hotkeys.context, "fixture.alpha");
    hotkeys.unregisterHotkey(hotkeys.context, kSecondAction);
    hotkeys.unregisterHotkey(hotkeys.context, "fixture.f1");
}
using TargetFn = int (*)(int);
TargetFn original = nullptr;
GAKU_NOINLINE int Target(int value) { volatile int result = value + 1; return result; }
int Detour(int value) { return original(value) + 10; }
}

extern "C" __declspec(dllexport) GakuStatus GakuPlugin_Query(uint32_t abi, uint32_t size, GakuPluginDescriptor* output) {
    if (abi != GAKU_ABI_VERSION) return GAKU_E_VERSION;
    if (!output) return GAKU_E_INVALID_ARGUMENT;
    if (size < sizeof(*output)) return GAKU_E_SIZE;
    GakuPluginDescriptor descriptor{GAKU_ABI_VERSION, sizeof(GakuPluginDescriptor), kId, kName, "1.0"};
    descriptor.activate = Activate;
    descriptor.onRuntimeReady = RuntimeReady;
    descriptor.onTick = Tick;
    descriptor.onActorTick = ActorTick;
    descriptor.onDesktopUi = Desktop;
    descriptor.shutdown = Shutdown;
    *output = descriptor;
    return GAKU_OK;
}
extern "C" __declspec(dllexport) GakuFixtureSnapshot GakuFixture_Snapshot() {
    return {activateCount.load(), readyCount.load(), ticks.load(), actorTicks.load(), desktops.load(), shutdowns.load(),
        activateThread.load(), readyThread.load(), tickThread.load(), shutdownThread.load(), hotkeyThread.load(),
        tasks.load(), nestedTasks.load(), overlaps.load(), failures.load(),
        actorA.load(), actorB.load(), otherActor.load(),
        hotkeyCount.load(), hotkeyRegistrations.load(), static_cast<uint32_t>(hotkeyStatus.load())};
}
extern "C" __declspec(dllexport) int32_t GakuFixture_Enqueue() {
    return queue.enqueue(queue.context, Task, nullptr);
}
extern "C" __declspec(dllexport) int32_t GakuFixture_Hook(uint32_t action) {
    if (action == 0) return hooks.acquire(hooks.context, reinterpret_cast<void*>(&Target), reinterpret_cast<void*>(&Detour), reinterpret_cast<void**>(&original));
    if (action == 1) return hooks.release(hooks.context, reinterpret_cast<void*>(&Target));
    return Target(4);
}
extern "C" __declspec(dllexport) uint32_t GakuFixture_BlockDesktop(uint32_t action) {
    if (action == 0) { desktopEntered.store(false); releaseDesktop.store(false); blockDesktop.store(true); }
    if (action == 2) { releaseDesktop.store(true); blockDesktop.store(false); }
    return desktopEntered.load() ? 1u : 0u;
}
extern "C" __declspec(dllexport) uint32_t GakuFixture_BlockTick(uint32_t action) {
    if (action == 0) { tickEntered.store(false); releaseTick.store(false); blockTick.store(true); }
    if (action == 2) { releaseTick.store(true); blockTick.store(false); }
    return tickEntered.load() ? 1u : 0u;
}
extern "C" __declspec(dllexport) void GakuFixture_UnregisterHotkeys() { UnregisterHotkeys(); }
