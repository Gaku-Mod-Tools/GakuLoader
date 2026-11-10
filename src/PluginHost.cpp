// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

#include "Host.hpp"
#include "HostContracts.hpp"
#include "HostConfig.hpp"
#include "ActorEntry.hpp"
#include "DesktopUi.hpp"
#include "DesktopDialogs.hpp"
#include "Hotkeys.hpp"
#include "Trace.hpp"
#include "CrashTrace.hpp"
#include "GakuProcessLifetimeHooks.h"
#include "GakuLifecycle.h"
#include "PluginPaths.hpp"
#include "LanguageSettings.hpp"
#include <minhook.h>
#include <cstdio>
#include <cstdarg>
#include <atomic>
#include <algorithm>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <cstring>

namespace GakuLoader {
namespace {
std::vector<std::wstring> g_pluginFiles;
struct PluginOwner {
    std::string id;
    std::atomic_bool accepting{true};
    PluginPaths::State paths;
};
struct RegisteredService { PluginOwner* owner; std::vector<unsigned char> table; };
struct QueuedTask { PluginOwner* owner; GakuTaskFn callback; void* userData; };
struct HookLease { PluginOwner* owner; void* detour; void* original; bool enabled; bool processLifetime; };
std::vector<LoadedPlugin> g_plugins;
// Owner tokens and loaded modules remain valid until process exit. Services
// cannot leave dangling code pointers through an unsupported hot-unload path.
std::vector<std::unique_ptr<PluginOwner>> g_owners;
PluginOwner g_hostOwner{"gakuloader", true};
std::recursive_mutex g_callbacks;
std::atomic_uint g_mainWaiters{0};
std::mutex g_servicesMutex, g_logMutex;
std::map<std::string, RegisteredService> g_services;
std::map<void*, HookLease> g_hooks;
std::deque<QueuedTask> g_tasks;
bool g_initialized = false, g_activated = false;
std::atomic_bool g_runtimeReady{false};
std::atomic_bool g_shutdownRequested{false};
// Published only while holding g_callbacks. The desktop shell must be able to
// copy status while a lifecycle callback holds that gate for several seconds.
std::atomic<std::shared_ptr<const DesktopHostSnapshot>> g_desktopSnapshot;
void PublishDesktopHostSnapshot() {
    auto result = std::make_shared<DesktopHostSnapshot>();
    result->runtimeReady = g_runtimeReady.load(std::memory_order_acquire);
    result->activated = g_activated;
    result->shuttingDown = g_shutdownRequested.load(std::memory_order_acquire);
    result->pluginFiles = g_pluginFiles;
    result->plugins.reserve(g_plugins.size());
    for (const auto& plugin : g_plugins) {
        result->plugins.push_back({plugin.id, plugin.name, plugin.version,
            plugin.activated, plugin.runtimeReady, plugin.failed, plugin.descriptor.onDesktopUi != nullptr});
    }
    g_desktopSnapshot.store(std::move(result), std::memory_order_release);
}
// Set before plugin shutdown when the process is leaving. Owner retirement then
// leaves its hook leases to one batched disable instead of freezing the process
// once per target.
std::atomic_bool g_exitTeardown{false};
std::atomic<const char*> g_shutdownPhase{"not-started"};
std::atomic<const char*> g_shutdownPlugin{nullptr};
// Acceptance counters: only read by the trace summary, never by dispatch.
std::atomic<uint64_t> g_frameTicks{0}, g_actorTicks{0};
std::atomic<void*> g_lastActor{nullptr};
std::atomic<ULONGLONG> g_shutdownPhaseTick{0};
std::atomic<DWORD> g_mainThreadId{0};
std::wstring g_gameRootW, g_localRootW, g_pluginsRootW;
std::filesystem::path g_configPath, g_logPath;
constexpr size_t kMaxTasks = 1024, kMaxServices = 64, kMaxServiceBytes = 65536;
HANDLE g_hotkeyThread = nullptr, g_hotkeyStopEvent = nullptr;
#if defined(GAKULOADER_TEST_SEAMS)
void (*g_hookEnableObserver)(uint32_t) = nullptr;
uint32_t g_hookEnableFailure = 0;
void (*g_actorEntryRegisterObserver)() = nullptr;
#endif
std::atomic<DWORD> g_hotkeyThreadId{0};
std::atomic_bool g_hotkeyStop{false};

void WriteToConsoleAndLog(const char* level, const char* message) {
    if (!message) return;
    std::lock_guard<std::mutex> lock(g_logMutex);
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD consoleMode = 0;
    if (out && out != INVALID_HANDLE_VALUE && GetConsoleMode(out, &consoleMode)) {
        DWORD written = 0;
        const std::string line = std::string("[") + level + "] " + message + "\r\n";
        // A plain string crosses the ABI; percent characters never become a format.
        // A launcher may redirect stdout into Player.log or a reporting pipe.
        // Only an actual Console is an allowed sink, never stdout disk/pipe.
        const int count = MultiByteToWideChar(CP_UTF8, 0, line.data(), static_cast<int>(line.size()), nullptr, 0);
        if (count > 0) {
            std::wstring wide(static_cast<size_t>(count), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, line.data(), static_cast<int>(line.size()), wide.data(), count);
            WriteConsoleW(out, wide.data(), static_cast<DWORD>(wide.size()), &written, nullptr);
        }
    }
    const auto path = g_logPath.empty() ? std::filesystem::path(L"GakuLoader/Logs/gakuloader.log") : g_logPath;
    FILE* file = _wfopen(path.c_str(), L"a");
    if (file) { fprintf(file, "[%s] %s\n", level, message); fclose(file); }
    // Never route mod diagnostics through Unity Debug, Player.log or a shared
    // OutputDebugString sink. Official reporting suppression is a separate hook.
}
void HostLogInfo(const char* format, ...) {
    va_list args; va_start(args, format); char buffer[2048];
    vsnprintf(buffer, sizeof(buffer), format, args); va_end(args);
    WriteToConsoleAndLog("GakuLoader", buffer);
}
void HostLogWarn(const char* format, ...) {
    va_list args; va_start(args, format); char buffer[2048];
    vsnprintf(buffer, sizeof(buffer), format, args); va_end(args);
    WriteToConsoleAndLog("GakuLoader:WARN", buffer);
}
void HostLogError(const char* format, ...) {
    va_list args; va_start(args, format); char buffer[2048];
    vsnprintf(buffer, sizeof(buffer), format, args); va_end(args);
    WriteToConsoleAndLog("GakuLoader:ERROR", buffer);
}
void* GetGameAssembly() { return GetModuleHandleW(L"GameAssembly.dll"); }
void ServiceLog(void*, GakuLogLevel level, const char* message) {
    WriteToConsoleAndLog(level == GAKU_LOG_ERROR ? "Plugin:ERROR" :
        level == GAKU_LOG_WARNING ? "Plugin:WARN" : "Plugin", message);
}
uint32_t IsMainThread(void*) {
    const DWORD thread = g_mainThreadId.load(std::memory_order_acquire);
    return thread != 0 && thread == GetCurrentThreadId();
}
GakuStatus Enqueue(void* context, GakuTaskFn callback, void* userData) {
    if (!context || !callback) return GAKU_E_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(g_servicesMutex);
    auto* owner = static_cast<PluginOwner*>(context);
    if (!owner->accepting) return GAKU_E_SHUTTING_DOWN;
    if (g_tasks.size() >= kMaxTasks) return GAKU_E_LIMIT;
    g_tasks.push_back({owner, callback, userData});
    return GAKU_OK;
}
GakuStatus AcquireHookImpl(void* context, void* target, void* detour, void** original, bool processLifetime) {
    if (!context || !target || !detour || !original) return GAKU_E_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(g_servicesMutex);
    auto* owner = static_cast<PluginOwner*>(context);
    if (!owner->accepting) return GAKU_E_SHUTTING_DOWN;
    const auto previous = g_hooks.find(target);
    if (previous != g_hooks.end()) {
        auto& lease = previous->second;
        if (lease.owner != owner || lease.detour != detour || lease.enabled) return GAKU_E_CONFLICT;
        *original = lease.original;
        if (MH_EnableHook(target) != MH_OK) return GAKU_E_INTERNAL;
        lease.enabled = true;
        return GAKU_OK;
    }
    const auto initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) return GAKU_E_INTERNAL;
    void* trampoline = nullptr;
    const auto created = MH_CreateHook(target, detour, &trampoline);
    if (created == MH_ERROR_ALREADY_CREATED) return GAKU_E_CONFLICT;
    if (created != MH_OK) return GAKU_E_INTERNAL;
    // Publish the trampoline before patched code can enter its detour.
    *original = trampoline;
    if (MH_EnableHook(target) != MH_OK) { MH_RemoveHook(target); return GAKU_E_INTERNAL; }
    g_hooks.emplace(target, HookLease{owner, detour, trampoline, true, processLifetime});
    return GAKU_OK;
}
GakuStatus AcquireHook(void* context, void* target, void* detour, void** original) {
    return AcquireHookImpl(context, target, detour, original, false);
}
GakuStatus AcquireBatch(void* context, uint32_t count, const GakuHookRequest* requests) {
    if (!context || !requests || !count || count > 256) return GAKU_E_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(g_servicesMutex);
    auto* owner = static_cast<PluginOwner*>(context);
    if (!owner->accepting) return GAKU_E_SHUTTING_DOWN;
    // Validate the whole batch before creating anything or changing outputs.
    for (uint32_t i = 0; i < count; ++i) {
        const auto& r = requests[i];
        if (!r.target || !r.detour || !r.original) return GAKU_E_INVALID_ARGUMENT;
        for (uint32_t j = 0; j < i; ++j)
            if (requests[j].target == r.target || requests[j].original == r.original)
                return GAKU_E_INVALID_ARGUMENT;
        const auto found = g_hooks.find(r.target);
        if (found != g_hooks.end() && (found->second.owner != owner ||
            found->second.detour != r.detour || found->second.enabled)) return GAKU_E_CONFLICT;
    }
    const auto initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) return GAKU_E_INTERNAL;
    struct Prepared { void* target; void* original; bool created; };
    std::vector<Prepared> prepared;
    prepared.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const auto& r = requests[i];
        const auto found = g_hooks.find(r.target);
        void* original = found == g_hooks.end() ? nullptr : found->second.original;
        if (found == g_hooks.end()) {
            const auto status = MH_CreateHook(r.target, r.detour, &original);
            if (status != MH_OK) {
                for (const auto& p : prepared) if (p.created) MH_RemoveHook(p.target);
                return status == MH_ERROR_ALREADY_CREATED ? GAKU_E_CONFLICT : GAKU_E_INTERNAL;
            }
        }
        prepared.push_back({r.target, original, found == g_hooks.end()});
    }
    // From this point reservations and every trampoline are process-lifetime.
    // Publish *all* originals before enabling the first patched instruction.
    for (uint32_t i = 0; i < count; ++i) {
        const auto& p = prepared[i];
        if (p.created) g_hooks.emplace(p.target, HookLease{owner, requests[i].detour, p.original, false, false});
        *requests[i].original = p.original;
    }
    // One suspended window for the whole batch. Enabling per target costs a
    // process-wide freeze each time, which is seconds for a plugin that installs
    // two hundred detours.
    std::vector<void*> enableTargets;
    enableTargets.reserve(count);
    for (uint32_t i = 0; i < count; ++i) enableTargets.push_back(prepared[i].target);
#if defined(GAKULOADER_TEST_SEAMS)
    for (uint32_t i = 0; i < count; ++i) {
        const auto status = g_hookEnableFailure == i + 1 ? MH_ERROR_MEMORY_PROTECT
                                                        : MH_EnableHook(prepared[i].target);
        if (status != MH_OK) {
            for (uint32_t j = 0; j < i; ++j) {
                auto& lease = g_hooks.at(prepared[j].target);
                const auto disabled = MH_DisableHook(prepared[j].target);
                if (disabled == MH_OK || disabled == MH_ERROR_DISABLED) lease.enabled = false;
            }
            return GAKU_E_INTERNAL;
        }
        g_hooks.at(prepared[i].target).enabled = true;
        if (g_hookEnableObserver) g_hookEnableObserver(i + 1);
    }
#else
    if (MH_EnableHooks(enableTargets.data(), static_cast<UINT32>(enableTargets.size())) != MH_OK) {
        for (const auto& p : prepared) {
            auto& lease = g_hooks.at(p.target);
            const auto disabled = MH_DisableHook(p.target);
            if (disabled == MH_OK || disabled == MH_ERROR_DISABLED) lease.enabled = false;
        }
        return GAKU_E_INTERNAL;
    }
    for (const auto& p : prepared) g_hooks.at(p.target).enabled = true;
#endif
    return GAKU_OK;
}
GakuStatus ReleaseHook(void* context, void* target) {
    if (!context || !target) return GAKU_E_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(g_servicesMutex);
    const auto found = g_hooks.find(target);
    if (found == g_hooks.end()) return GAKU_E_UNAVAILABLE;
    if (found->second.owner != context) return GAKU_E_CONFLICT;
    if (found->second.processLifetime && found->second.owner != &g_hostOwner) return GAKU_E_CONFLICT;
    // Keep trampoline allocations alive until process exit: disabling a hook
    // does not prove another thread has returned from its trampoline yet.
    if (found->second.enabled) {
        const auto status = MH_DisableHook(target);
        if (status != MH_OK && status != MH_ERROR_DISABLED) return GAKU_E_INTERNAL;
        found->second.enabled = false;
    }
    return GAKU_OK;
}
GakuStatus RetainOwnedHooks(void* context, uint32_t count, void* const* targets) {
    if (!context || !targets || count == 0 || count > 256) return GAKU_E_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(g_servicesMutex);
    auto* owner = static_cast<PluginOwner*>(context);
    const bool knownOwner = std::any_of(g_owners.begin(), g_owners.end(), [owner](const auto& candidate) {
        return candidate.get() == owner;
    });
    if (!knownOwner) return GAKU_E_INVALID_ARGUMENT;
    if (!owner->accepting || g_exitTeardown.load(std::memory_order_acquire) ||
        g_shutdownRequested.load(std::memory_order_acquire)) return GAKU_E_SHUTTING_DOWN;
    // Validate every entry under the same owner lock before promoting any.
    // No patching, allocation or detour execution is needed for this operation.
    for (uint32_t i = 0; i < count; ++i) {
        if (!targets[i]) return GAKU_E_INVALID_ARGUMENT;
        for (uint32_t j = 0; j < i; ++j)
            if (targets[j] == targets[i]) return GAKU_E_INVALID_ARGUMENT;
        const auto found = g_hooks.find(targets[i]);
        if (found == g_hooks.end()) return GAKU_E_UNAVAILABLE;
        if (found->second.owner != owner) return GAKU_E_CONFLICT;
        if (!found->second.enabled) return GAKU_E_UNAVAILABLE;
    }
    for (uint32_t i = 0; i < count; ++i) g_hooks.at(targets[i]).processLifetime = true;
    return GAKU_OK;
}
void RetireOwner(PluginOwner* owner) {
    const auto started = GetTickCount64();
    // Mark closed before removing bindings; registration uses the same mutex.
    { std::lock_guard<std::mutex> lock(g_servicesMutex); owner->accepting = false; }
    owner->paths.Retire();
    DesktopDialogs::CancelOwner(owner);
    Hotkeys::RetireOwner(owner);
    ActorEntry::RetireOwner(owner);
    size_t hooksLeft = 0;
    {
        std::lock_guard<std::mutex> lock(g_servicesMutex);
        owner->accepting = false;
        for (auto it = g_services.begin(); it != g_services.end();) {
            if (it->second.owner == owner) it = g_services.erase(it); else ++it;
        }
        g_tasks.erase(std::remove_if(g_tasks.begin(), g_tasks.end(), [owner](const QueuedTask& task) {
            return task.owner == owner;
        }), g_tasks.end());
        // During exit every lease is disabled in one pass by
        // DisableOwnedHooksForExit; disabling them here would freeze the
        // process once per target (hundreds for a translation plugin).
        for (auto& [target, lease] : g_hooks) {
            if (lease.owner != owner) continue;
            if (lease.enabled) ++hooksLeft;
            if (lease.enabled && !lease.processLifetime && !g_exitTeardown.load(std::memory_order_acquire)) {
                MH_DisableHook(target); lease.enabled = false;
            }
        }
    }
    // Teardown evidence: nothing owned by this plugin may remain registered.
    HostLogInfo("[Owner] retired %s; actor_entries_left=%zu hotkey_rows_left=%zu services_left=%zu "
        "hook_leases_left=%zu retire_ms=%llu",
        owner->id.c_str(), ActorEntry::Count(), Hotkeys::Snapshot().size(), g_services.size(),
        hooksLeft, static_cast<unsigned long long>(GetTickCount64() - started));
}
GakuStatus RegisterService(void* context, const char* name, uint32_t size, const void* table) {
    if (!context || !name || !name[0] || !table || strlen(name) > 127) return GAKU_E_INVALID_ARGUMENT;
    if (size < sizeof(GakuServiceHeader) || size > kMaxServiceBytes) return GAKU_E_SIZE;
    GakuServiceHeader header{}; memcpy(&header, table, sizeof(header));
    if (header.structSize != size) return GAKU_E_SIZE;
    if (!header.abiVersion) return GAKU_E_VERSION;
    if (strncmp(name, "gakuloader.", 11) == 0) return GAKU_E_CONFLICT;
    std::lock_guard<std::mutex> lock(g_servicesMutex);
    auto* owner = static_cast<PluginOwner*>(context);
    if (!owner->accepting) return GAKU_E_SHUTTING_DOWN;
    if (g_services.count(name)) return GAKU_E_CONFLICT;
    if (g_services.size() >= kMaxServices) return GAKU_E_LIMIT;
    const auto* first = static_cast<const unsigned char*>(table);
    g_services.emplace(name, RegisteredService{owner, std::vector<unsigned char>(first, first + size)});
    return GAKU_OK;
}
GakuStatus RegisterHotkey(void* context, const GakuHotkeyBinding* binding) {
    if (!context) return GAKU_E_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(g_servicesMutex);
    auto* owner = static_cast<PluginOwner*>(context);
    if (!owner->accepting) return GAKU_E_SHUTTING_DOWN;
    const auto status = Hotkeys::Register(context, owner->id.c_str(), binding);
    if (status == GAKU_E_CONFLICT && binding && binding->virtualKey == Hotkeys::kManagerKey) {
        HostLogWarn("[Hotkeys] %s: F1 is reserved by the manager; binding %s refused", owner->id.c_str(),
            binding->actionId ? binding->actionId : "?");
    }
    return status;
}
GakuStatus UnregisterHotkey(void* context, const char* actionId) {
    if (!context) return GAKU_E_INVALID_ARGUMENT;
    return Hotkeys::Unregister(context, actionId);
}
GakuStatus RegisterActorEntry(void* context, const GakuActorEntryBinding* binding) {
    if (!context || !binding) return GAKU_E_INVALID_ARGUMENT;
    // Hold the owner lock across the check *and* the insert. RetireOwner clears
    // `accepting` under this same mutex before ActorEntry::RetireOwner runs, so
    // a registration either lands before retirement (and is then retired with
    // the owner) or sees the closed flag. Checking and then inserting in a
    // separate critical section allowed both to interleave and left a retired
    // owner's callback holding already-invalid userData.
    std::lock_guard<std::mutex> lock(g_servicesMutex);
    if (!static_cast<PluginOwner*>(context)->accepting) return GAKU_E_SHUTTING_DOWN;
#if defined(GAKULOADER_TEST_SEAMS)
    // Runs inside the critical section on purpose: a regression that splits the
    // check and the insert into two sections lets the observer's owner retire
    // first, which the interleave test detects as a surviving participant.
    if (g_actorEntryRegisterObserver) g_actorEntryRegisterObserver();
#endif
    return ActorEntry::Register(context, binding);
}
GakuStatus UnregisterActorEntry(void* context, uint32_t phase) {
    if (!context) return GAKU_E_INVALID_ARGUMENT;
    return ActorEntry::Unregister(context, phase);
}
GakuStatus RequestPluginPanel(void* context) {
    if (!context) return GAKU_E_INVALID_ARGUMENT;
    auto* owner = static_cast<PluginOwner*>(context);
    if (!owner->accepting) return GAKU_E_SHUTTING_DOWN;
    return DesktopUi::RequestShowPanel(owner->id.c_str());
}
GakuStatus SetFileDropTarget(void* context, const GakuFileDropTarget* target) {
    if (!context) return GAKU_E_INVALID_ARGUMENT;
    auto* owner = static_cast<PluginOwner*>(context);
    if (!owner->accepting) return GAKU_E_SHUTTING_DOWN;
    try { return DesktopUi::SetFileDropTarget(owner->id.c_str(), target); }
    catch (...) { return GAKU_E_INTERNAL; }
}
GakuStatus PollFileDrop(void* context, uint32_t size, GakuFileDropResult* result,
    uint32_t capacity, char* paths) {
    if (!context) return GAKU_E_INVALID_ARGUMENT;
    auto* owner = static_cast<PluginOwner*>(context);
    if (!owner->accepting) return GAKU_E_SHUTTING_DOWN;
    return DesktopUi::PollFileDrop(owner->id.c_str(), size, result, capacity, paths);
}
PluginOwner* FindPathOwner(void* context) {
    // Owner objects live until exit. Only borrow a pointer under the registry
    // lock, then use the owner's own path lock for filesystem operations.
    std::lock_guard lock(g_servicesMutex);
    const auto found = std::find_if(g_owners.begin(), g_owners.end(), [context](const auto& owner) {
        return owner.get() == context;
    });
    return found == g_owners.end() ? nullptr : found->get();
}
GakuStatus GetPathRoot(void* context, GakuPathKind kind, wchar_t* output, uint32_t capacity, uint32_t* required) {
    try {
        auto* owner = FindPathOwner(context);
        if (!owner) return GAKU_E_INVALID_ARGUMENT;
        if (!owner->accepting) return GAKU_E_SHUTTING_DOWN;
        return owner->paths.GetRoot(kind, output, capacity, required);
    } catch (...) { return GAKU_E_INTERNAL; }
}
GakuStatus EnsurePathRoot(void* context, GakuPathKind kind) {
    try {
        auto* owner = FindPathOwner(context);
        if (!owner) return GAKU_E_INVALID_ARGUMENT;
        if (!owner->accepting) return GAKU_E_SHUTTING_DOWN;
        std::error_code error;
        const auto status = owner->paths.EnsureRoot(kind, error);
        if (error) HostLogWarn("[Paths] owner=%s category=%u create failed OS=%d (%s)",
            owner->id.c_str(), kind, error.value(), error.category().name());
        return status;
    } catch (...) {
        try { HostLogWarn("[Paths] category=%u create failed with an internal exception", kind); } catch (...) {}
        return GAKU_E_INTERNAL;
    }
}
uint32_t OwnerIsStopping(void* context) {
    const auto* owner = static_cast<const PluginOwner*>(context);
    return !owner || g_shutdownRequested.load(std::memory_order_acquire) ||
        !owner->accepting.load(std::memory_order_acquire) ? 1u : 0u;
}
GakuStatus QueryService(void* context, const char* name, uint32_t version, uint32_t size, void* output) {
    if (!context || !name || !name[0] || !output) return GAKU_E_INVALID_ARGUMENT;
    // Built-in tables contain no mutable plugin-domain state. Each owner gets
    // its own lease/queue token instead of a global unscoped control surface.
    if (strcmp(name, GAKU_SERVICE_LOG) == 0) {
        const GakuLogService table{GAKU_SERVICE_VERSION, sizeof(GakuLogService), context, ServiceLog};
        return CopyServiceTable(&table, sizeof(table), version, size, output);
    }
    if (strcmp(name, GAKU_SERVICE_RUNTIME) == 0) {
        const GakuRuntimeService table{GAKU_SERVICE_VERSION, sizeof(GakuRuntimeService), g_gameRootW.c_str(),
            g_localRootW.c_str(), g_pluginsRootW.c_str(), GetGameAssembly};
        return CopyServiceTable(&table, sizeof(table), version, size, output);
    }
    if (strcmp(name, GAKU_SERVICE_LANGUAGE) == 0) {
        return CopyServiceTable(&LanguageSettings::kService, sizeof(LanguageSettings::kService), version, size, output);
    }
    if (strcmp(name, GAKU_SERVICE_LIFECYCLE) == 0) {
        const GakuLifecycleServiceV1 table{GAKU_LIFECYCLE_VERSION, sizeof(GakuLifecycleServiceV1),
            context, OwnerIsStopping};
        return CopyServiceTable(&table, sizeof(table), version, size, output);
    }
    if (strcmp(name, GAKU_SERVICE_PATHS) == 0) {
        try {
            auto* owner = FindPathOwner(context);
            if (!owner) return GAKU_E_INVALID_ARGUMENT;
            if (!owner->accepting) return GAKU_E_SHUTTING_DOWN;
            const auto status = owner->paths.Status();
            if (status != GAKU_OK) return status;
            const GakuPathsV1 table{GAKU_PATHS_VERSION, sizeof(GakuPathsV1), context, GetPathRoot, EnsurePathRoot};
            return CopyServiceTable(&table, sizeof(table), version, size, output);
        } catch (...) { return GAKU_E_INTERNAL; }
    }
    if (strcmp(name, GAKU_SERVICE_MAIN_THREAD) == 0) {
        const GakuMainThreadService table{GAKU_SERVICE_VERSION, sizeof(GakuMainThreadService), context,
            Enqueue, IsMainThread};
        return CopyServiceTable(&table, sizeof(table), version, size, output);
    }
    if (strcmp(name, GAKU_SERVICE_HOOKS) == 0) {
        const GakuHookService table{GAKU_SERVICE_VERSION, sizeof(GakuHookService), context, AcquireHook, ReleaseHook, AcquireBatch};
        return CopyServiceTable(&table, sizeof(table), version, size, output);
    }
    if (strcmp(name, GAKU_SERVICE_PROCESS_LIFETIME_HOOKS) == 0) {
        const GakuProcessLifetimeHooksV1 table{GAKU_PROCESS_LIFETIME_HOOKS_VERSION,
            sizeof(GakuProcessLifetimeHooksV1), context, RetainOwnedHooks};
        return CopyServiceTable(&table, sizeof(table), version, size, output);
    }
    if (strcmp(name, GAKU_SERVICE_DESKTOP_UI) == 0) {
        const auto* table = DesktopUi::GetService();
        return table ? CopyServiceTable(table, sizeof(*table), version, size, output) : GAKU_E_UNAVAILABLE;
    }
    if (strcmp(name, GAKU_SERVICE_APPEARANCE) == 0) {
        const auto* table = DesktopUi::GetAppearanceService();
        return table ? CopyServiceTable(table, sizeof(*table), version, size, output) : GAKU_E_UNAVAILABLE;
    }
    if (strcmp(name, GAKU_SERVICE_DESKTOP_WINDOW) == 0) {
        const auto* table = DesktopUi::GetWindowService();
        if (!table) return GAKU_E_UNAVAILABLE;
        auto bound = *table;
        bound.context = context;
        bound.requestShow = RequestPluginPanel;
        return CopyServiceTable(&bound, sizeof(bound), version, size, output);
    }
    if (strcmp(name, GAKU_SERVICE_FILE_DIALOG) == 0) {
        const auto table = DesktopDialogs::ServiceForOwner(context);
        return CopyServiceTable(&table, sizeof(table), version, size, output);
    }
    if (strcmp(name, GAKU_SERVICE_FILE_DROP) == 0) {
        const GakuFileDropService table{GAKU_FILE_DROP_VERSION, sizeof(GakuFileDropService),
            context, SetFileDropTarget, PollFileDrop};
        return CopyServiceTable(&table, sizeof(table), version, size, output);
    }
    if (strcmp(name, GAKU_SERVICE_HOTKEYS) == 0) {
        const GakuHotkeyService table{GAKU_SERVICE_VERSION, sizeof(GakuHotkeyService), context,
            RegisterHotkey, UnregisterHotkey};
        return CopyServiceTable(&table, sizeof(table), version, size, output);
    }
    if (strcmp(name, GAKU_SERVICE_ACTOR_ENTRY) == 0) {
        const GakuActorEntryService table{GAKU_SERVICE_VERSION, sizeof(GakuActorEntryService), context,
            RegisterActorEntry, UnregisterActorEntry};
        return CopyServiceTable(&table, sizeof(table), version, size, output);
    }
    std::lock_guard<std::mutex> lock(g_servicesMutex);
    if (!static_cast<PluginOwner*>(context)->accepting) return GAKU_E_SHUTTING_DOWN;
    const auto found = g_services.find(name);
    if (found == g_services.end()) return GAKU_E_UNAVAILABLE;
    return CopyServiceTable(found->second.table.data(), static_cast<uint32_t>(found->second.table.size()),
        version, size, output);
}
void DrainTasks() {
    std::deque<QueuedTask> tasks;
    { std::lock_guard<std::mutex> lock(g_servicesMutex); tasks.swap(g_tasks); }
    // The batch is finite: tasks enqueued by a task wait until a later frame.
    for (const auto& task : tasks) {
        bool accepting;
        { std::lock_guard<std::mutex> lock(g_servicesMutex); accepting = task.owner->accepting; }
        if (accepting) {
            try { task.callback(task.userData); }
            catch (...) { HostLogError("Queued plugin callback threw across ABI: %s", task.owner->id.c_str()); }
        }
    }
}

// --- Hotkey poller -----------------------------------------------------------
// One dedicated thread owns key edges. Dispatch is enqueued onto the Unity main
// thread, never run here: plugin hotkey callbacks follow the same threading
// contract as onTick. The manager toggle is UI-only and never waits for a frame.
struct HotkeyTask { GakuHotkeyFn callback; void* userData; };
void RunHotkeyTask(void* data) {
    auto* task = static_cast<HotkeyTask*>(data);
    try { task->callback(task->userData); }
    catch (...) { HostLogError("Hotkey callback threw across ABI"); }
    delete task;
}
bool ReadKey(uint32_t virtualKey) { return (GetAsyncKeyState(static_cast<int>(virtualKey)) & 0x8000) != 0; }
void DispatchHotkey(const Hotkeys::Action& action) {
    if (g_shutdownRequested.load(std::memory_order_acquire)) return;
    auto* task = new HotkeyTask{action.callback, action.userData};
    if (Enqueue(action.owner, RunHotkeyTask, task) != GAKU_OK) delete task;
}
DWORD WINAPI HotkeyPoller(LPVOID) {
    g_hotkeyThreadId.store(GetCurrentThreadId(), std::memory_order_release);
    Hotkeys::PollContext context{};
    context.isKeyDown = ReadKey;
    Hotkeys::PollResult result;
    while (!g_hotkeyStop.load(std::memory_order_acquire)) {
        const HWND foreground = GetForegroundWindow();
        DWORD foregroundPid = 0;
        if (foreground) GetWindowThreadProcessId(foreground, &foregroundPid);
        const bool ours = foregroundPid != 0 && foregroundPid == GetCurrentProcessId();
        const auto* window = DesktopUi::GetWindowService();
        const HWND hostWindow = window && window->getWindow ? static_cast<HWND>(window->getWindow()) : nullptr;
        wchar_t foregroundClass[64]{};
        if (foreground) GetClassNameW(foreground, foregroundClass, 64);
        // Same-process Shell dialogs are not game input targets. Suppress both
        // plugin actions and F1 there, while still tracking physical edges.
        context.foregroundOwnedByThisProcess = ours &&
            (foreground == hostWindow || lstrcmpW(foregroundClass, L"UnityWndClass") == 0);
        // Text fields inside the control window consume letter keys; F1 and
        // game-side hotkeys stay inert so typing never triggers a plugin action.
        context.hostWindowWantsTextInput = ours && hostWindow && foreground == hostWindow &&
            DesktopUi::WantsTextInput();
        Hotkeys::Poll(context, result);
        if (result.managerToggle) DesktopUi::ToggleManager();
        for (const auto& action : result.actions) DispatchHotkey(action);
        if (g_hotkeyStopEvent) WaitForSingleObject(g_hotkeyStopEvent, 12);
        else Sleep(12);
    }
    return 0;
}
#if defined(GAKULOADER_TEST_SEAMS)
std::atomic_bool g_hotkeyPollerDisabled{false};
#endif
void StartHotkeyPoller() {
    if (g_hotkeyThread) return;
#if defined(GAKULOADER_TEST_SEAMS)
    if (g_hotkeyPollerDisabled.load(std::memory_order_acquire)) return;
#endif
    g_hotkeyStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_hotkeyStop.store(false, std::memory_order_release);
    g_hotkeyThread = CreateThread(nullptr, 0, HotkeyPoller, nullptr, 0, nullptr);
    if (!g_hotkeyThread) HostLogWarn("[Hotkeys] poller thread unavailable; only the manager key is lost");
}
void StopHotkeyPoller() {
    g_hotkeyStop.store(true, std::memory_order_release);
    if (g_hotkeyStopEvent) SetEvent(g_hotkeyStopEvent);
    if (g_hotkeyThread && GetCurrentThreadId() != g_hotkeyThreadId.load(std::memory_order_acquire)) {
        const DWORD wait = WaitForSingleObject(g_hotkeyThread, 2000);
        CloseHandle(g_hotkeyThread);
        g_hotkeyThread = nullptr;
        if (wait != WAIT_OBJECT_0) HostLogWarn("[Hotkeys] poller did not stop within 2s; edges stop immediately");
    }
    if (g_hotkeyStopEvent) { CloseHandle(g_hotkeyStopEvent); g_hotkeyStopEvent = nullptr; }
}

void ShutdownOnMainThread() {
    g_shutdownRequested = true;
    PublishDesktopHostSnapshot();
    const auto started = GetTickCount64();
    g_shutdownPhaseTick.store(started, std::memory_order_relaxed);
    g_shutdownPhase.store("cancel-owner-work", std::memory_order_release);
    HostLogInfo("[Shutdown] begin main-thread teardown thread=%lu plugins=%zu", GetCurrentThreadId(), g_plugins.size());
    // Stop accepting work before callbacks. Shutdown may release hooks, but new
    // tasks and provider registrations must not outlive this boundary.
    { std::lock_guard<std::mutex> lock(g_servicesMutex);
      for (const auto& owner : g_owners) owner->accepting = false;
      g_tasks.clear(); }
    for (auto it = g_plugins.rbegin(); it != g_plugins.rend(); ++it) {
        if (!it->activated || it->failed) { RetireOwner(static_cast<PluginOwner*>(it->owner)); continue; }
        const auto pluginStarted = GetTickCount64();
        g_shutdownPlugin.store(it->id.c_str(), std::memory_order_relaxed);
        g_shutdownPhaseTick.store(pluginStarted, std::memory_order_relaxed);
        g_shutdownPhase.store("plugin-callback", std::memory_order_release);
        HostLogInfo("[Shutdown] begin plugin=%s", it->name.c_str());
        try {
            if (it->descriptor.shutdown) it->descriptor.shutdown(it->state);
        } catch (...) { HostLogError("Plugin shutdown threw across ABI: %s", it->name.c_str()); }
        HostLogInfo("[Shutdown] end plugin=%s callback_ms=%llu", it->name.c_str(),
            static_cast<unsigned long long>(GetTickCount64() - pluginStarted));
        it->activated = false;
        PublishDesktopHostSnapshot();
        g_shutdownPhaseTick.store(GetTickCount64(), std::memory_order_relaxed);
        g_shutdownPhase.store("retire-owner", std::memory_order_release);
        RetireOwner(static_cast<PluginOwner*>(it->owner));
        HostLogInfo("[Shutdown] retired plugin=%s total_ms=%llu", it->name.c_str(),
            static_cast<unsigned long long>(GetTickCount64() - pluginStarted));
    }
    g_initialized = false;
    g_shutdownPlugin.store(nullptr, std::memory_order_relaxed);
    g_shutdownPhaseTick.store(GetTickCount64(), std::memory_order_relaxed);
    g_shutdownPhase.store("complete", std::memory_order_release);
    HostLogInfo("Plugin shutdown complete on main thread; total_ms=%llu; modules/trampolines retained until process exit",
        static_cast<unsigned long long>(GetTickCount64() - started));
}

}

void LogInfo(const char* format, ...) {
    va_list args; va_start(args, format); char buffer[2048];
    vsnprintf(buffer, sizeof(buffer), format, args); va_end(args); WriteToConsoleAndLog("GakuLoader", buffer);
}
void LogWarn(const char* format, ...) {
    va_list args; va_start(args, format); char buffer[2048];
    vsnprintf(buffer, sizeof(buffer), format, args); va_end(args); WriteToConsoleAndLog("GakuLoader:WARN", buffer);
}
void LogError(const char* format, ...) {
    va_list args; va_start(args, format); char buffer[2048];
    vsnprintf(buffer, sizeof(buffer), format, args); va_end(args); WriteToConsoleAndLog("GakuLoader:ERROR", buffer);
}
std::filesystem::path LogPath() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    return g_logPath;
}
std::filesystem::path ConfigPath() {
    std::lock_guard<std::mutex> lock(g_servicesMutex);
    return g_configPath;
}
GakuStatus AcquireHostHook(void* target, void* detour, void** original) { return AcquireHook(&g_hostOwner, target, detour, original); }
GakuStatus AcquireProcessLifetimeHostHook(void* target, void* detour, void** original) {
    return AcquireHookImpl(&g_hostOwner, target, detour, original, true);
}
GakuStatus ReleaseHostHook(void* target) { return ReleaseHook(&g_hostOwner, target); }
void BeginExitTeardown() {
    // From here on, owner retirement stops touching hook patches individually:
    // DisableOwnedHooksForExit disables every lease in one suspended window.
    g_exitTeardown.store(true, std::memory_order_release);
}

void DisableOwnedHooksForExit() {
    // Close promotion before taking the list snapshot, even when this entry is
    // called without the normal BeginExitTeardown prelude.
    g_exitTeardown.store(true, std::memory_order_release);
    std::vector<void*> targets;
    {
        std::lock_guard<std::mutex> lock(g_servicesMutex);
        g_hostOwner.accepting = false;
        targets.reserve(g_hooks.size());
        for (auto& [target, lease] : g_hooks)
            if (lease.enabled && !lease.processLifetime) targets.push_back(target);
    }
    const auto started = GetTickCount64();
    MH_STATUS status = MH_OK;
    if (!targets.empty()) {
        status = MH_DisableHooks(targets.data(), static_cast<UINT32>(targets.size()));
    }
    if (status == MH_OK) {
        std::lock_guard<std::mutex> lock(g_servicesMutex);
        for (auto* target : targets) {
            const auto found = g_hooks.find(target);
            if (found != g_hooks.end()) found->second.enabled = false;
        }
    }
    HostLogInfo("Disabled %zu loader-owned runtime hook leases in %llu ms (status=%d); "
        "process-lifetime boundaries remain armed; foreign hooks untouched",
        targets.size(), static_cast<unsigned long long>(GetTickCount64() - started),
        static_cast<int>(status));
}
bool CanShutdownOnCurrentThread() {
    const DWORD thread = g_mainThreadId.load(std::memory_order_acquire);
    return thread != 0 && thread == GetCurrentThreadId();
}
void LogShutdownProgress() {
    const auto phase = g_shutdownPhase.load(std::memory_order_acquire);
    const auto plugin = g_shutdownPlugin.load(std::memory_order_relaxed);
    const auto tick = g_shutdownPhaseTick.load(std::memory_order_relaxed);
    LogWarn("[Shutdown] progress phase=%s plugin=%s phase_ms=%llu", phase, plugin ? plugin : "none",
        static_cast<unsigned long long>(tick ? GetTickCount64() - tick : 0));
}

void Initialize(const std::filesystem::path& gameRoot, const std::filesystem::path& localRoot) {
    std::lock_guard<std::recursive_mutex> lock(g_callbacks);
    if (g_initialized) return;
    // A stopped host cannot restart: retained providers and hook trampolines are
    // process lifetime. Restarting means restarting the game.
    if (g_shutdownRequested) return;
    g_gameRootW = gameRoot.wstring(); g_localRootW = localRoot.wstring();
    const auto pluginsPath = localRoot / "plugins"; g_pluginsRootW = pluginsPath.wstring();
    const auto configDir = localRoot / L"Configs";
    const auto logsDir = localRoot / L"Logs";
    for (const auto& directory : {pluginsPath, configDir, logsDir}) {
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) throw std::filesystem::filesystem_error("Cannot create GakuLoader directory", directory, error);
    }
    g_configPath = configDir / L"gakuloader.ini";
    LanguageSettings::Load(g_configPath);
    Trace::Configure(ReadConfigFlag(g_configPath, L"diagnostics", L"trace", false));
    if (Trace::Enabled()) {
        const auto crashPath = logsDir / "crash.log";
        std::error_code ignored;
        std::filesystem::remove(crashPath, ignored); // one run, one file
        CrashTrace::Arm(crashPath);
    }
    { std::lock_guard<std::mutex> logLock(g_logMutex); g_logPath = logsDir / "gakuloader.log"; }
    Hotkeys::LoadOverrides(g_configPath);
    g_initialized = true;
    PublishDesktopHostSnapshot();
    HostLogInfo("Roots ready: gameRoot=%ls localRoot=%ls; native plugin activation starts before the game runtime",
        g_gameRootW.c_str(), g_localRootW.c_str());
    StartHotkeyPoller();
}
void ActivatePlugins() {
    std::lock_guard<std::recursive_mutex> lock(g_callbacks);
    if (!g_initialized || g_activated || g_shutdownRequested) return;
    g_activated = true;
    // Evidence for the lifecycle contract, from inside the process: activation
    // must happen before the runtime is resolvable, and the game module may
    // already be mapped. Neither condition is inferred from a later callback.
    HostLogInfo("[Activate] early native activation thread=%lu uptime_ms=%llu runtimeReady=%u "
        "gameAssembly=%u vuplex=%u",
        static_cast<unsigned long>(GetCurrentThreadId()),
        static_cast<unsigned long long>(Trace::UptimeMs()), g_runtimeReady.load() ? 1u : 0u,
        GetModuleHandleW(L"GameAssembly.dll") ? 1u : 0u,
        GetModuleHandleW(L"VuplexWebViewWindows.dll") ? 1u : 0u);
    const std::filesystem::path pluginsPath(g_pluginsRootW);
    std::error_code ec;
    HostLogInfo("Scanning %ls (GakuLoader ABI v%u)", g_pluginsRootW.c_str(), GAKU_ABI_VERSION);
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(pluginsPath, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".dll" || entry.path().filename().wstring()[0] == L'.') continue;
        const auto filename = entry.path().filename().wstring();
        // These are Motion's explicitly named auxiliary libraries, not plugin
        // entry points. Do not execute their DllMain during discovery.
        if (_wcsicmp(filename.c_str(), L"GakuBurstBridge.dll") == 0 ||
            _wcsicmp(filename.c_str(), L"GakuMotion.VmdImporter.Native.dll") == 0) {
            HostLogInfo("Auxiliary library (not a plugin): %ls", filename.c_str());
            continue;
        }
        paths.push_back(entry.path());
    }
    std::sort(paths.begin(), paths.end());
    g_pluginFiles.clear();
    for (const auto& path : paths) g_pluginFiles.push_back(path.filename().wstring());
    PublishDesktopHostSnapshot();
    for (const auto& path : paths) {
        const auto filename = path.filename().wstring();
        if (!ReadConfigFlag(g_configPath, L"plugins", filename.c_str(), true)) {
            HostLogInfo("Plugin disabled by config before DLL load: %ls", filename.c_str());
            continue;
        }
        HMODULE module = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!module) { HostLogError("Cannot load %ls (Win32=%lu)", filename.c_str(), GetLastError()); continue; }
        const auto query = reinterpret_cast<FnGakuPlugin_Query>(GetProcAddress(module, "GakuPlugin_Query"));
        if (!query) {
            // ABI reset: pre-reset builds (GmlPlugin_Query / GakumasPlugin_*) are
            // never probed or called. Recompile is the only supported migration.
            HostLogError("Rejected %ls: no GakuPlugin_Query export. Rebuild it against GakuPlugin.h; "
                "the GML v1 and legacy entry points are retired.", filename.c_str());
            FreeLibrary(module); continue;
        }
        LoadedPlugin plugin;
        plugin.handle = module; plugin.dllPath = path.wstring();
        const auto status = query(GAKU_ABI_VERSION, sizeof(plugin.descriptor), &plugin.descriptor);
        if (status != GAKU_OK || plugin.descriptor.abiVersion != GAKU_ABI_VERSION ||
            plugin.descriptor.structSize != sizeof(plugin.descriptor) || !plugin.descriptor.id ||
            !plugin.descriptor.id[0] || !plugin.descriptor.name || !plugin.descriptor.activate) {
            HostLogError("Rejected incompatible GakuPlugin descriptor from %ls (status=%d)", filename.c_str(), status);
            FreeLibrary(module); continue;
        }
        plugin.id = plugin.descriptor.id;
        plugin.name = plugin.descriptor.name;
        plugin.version = plugin.descriptor.version ? plugin.descriptor.version : "";
        if (std::any_of(g_owners.begin(), g_owners.end(), [&](const auto& owner) { return owner->id == plugin.id; })) {
            HostLogError("Duplicate plugin id %s; skipping %ls", plugin.id.c_str(), filename.c_str());
            FreeLibrary(module); continue;
        }
        auto owner = std::make_unique<PluginOwner>(); owner->id = plugin.id;
        owner->paths.Configure(std::filesystem::path(g_localRootW), plugin.id);
        plugin.owner = owner.get();
        plugin.host = {GAKU_ABI_VERSION, sizeof(GakuHostApi), owner.get(), QueryService, RegisterService};
        { std::lock_guard registryLock(g_servicesMutex); g_owners.push_back(std::move(owner)); }
        const auto activated = plugin.descriptor.activate(&plugin.host, &plugin.state);
        if (activated != GAKU_OK) {
            plugin.failed = true;
            RetireOwner(static_cast<PluginOwner*>(plugin.owner));
            HostLogError("Plugin %s activation failed (status=%d); other plugins continue", plugin.name.c_str(), activated);
        } else {
            plugin.activated = true;
            HostLogInfo("Activated %s v%s (native; runtime callbacks deferred)", plugin.name.c_str(),
                plugin.version.empty() ? "?" : plugin.version.c_str());
        }
        g_plugins.push_back(std::move(plugin));
        PublishDesktopHostSnapshot();
    }
    HostLogInfo("Native activation complete; %zu plugin(s) resident", g_plugins.size());
    // Evidence from inside our own process; external module enumeration can be
    // denied by Windows. This is diagnostic only, never integration discovery.
    HostLogInfo("Loader ownership: pid=%lu legacy_vrify_loaded=%u", GetCurrentProcessId(),
        GetModuleHandleW(L"version_vrify.dll") ? 1u : 0u);
}
void NotifyRuntimeReady() {
    std::lock_guard<std::recursive_mutex> lock(g_callbacks);
    if (!g_initialized || g_runtimeReady || g_shutdownRequested) return;
    g_runtimeReady = true;
    PublishDesktopHostSnapshot();
    // uptime_ms is the only way to place "hooks installed" against a screenshot:
    // the game window is created earlier than this signal, so a UI that appears
    // before it keeps whatever text it was built with.
    HostLogInfo("[StartupTiming] NotifyRuntimeReady thread=%lu uptime_ms=%llu; dispatching onRuntimeReady (metadata/hooks only)",
        GetCurrentThreadId(), static_cast<unsigned long long>(Trace::UptimeMs()));
    for (auto& plugin : g_plugins) {
        if (g_shutdownRequested.load(std::memory_order_acquire)) break;
        if (!plugin.activated || plugin.failed) continue;
        if (!plugin.descriptor.onRuntimeReady) {
            plugin.runtimeReady = true; PublishDesktopHostSnapshot(); continue;
        }
        try {
            const auto started = Trace::UptimeMs();
            HostLogInfo("[StartupTiming] runtime callback begin plugin=%s uptime_ms=%llu thread=%lu", plugin.id.c_str(), started, GetCurrentThreadId());
            plugin.descriptor.onRuntimeReady(plugin.state);
            plugin.runtimeReady = true;
            PublishDesktopHostSnapshot();
            HostLogInfo("[StartupTiming] runtime callback complete plugin=%s uptime_ms=%llu thread=%lu elapsed_ms=%llu",
                plugin.id.c_str(), Trace::UptimeMs(), GetCurrentThreadId(), Trace::UptimeMs() - started);
        } catch (...) {
            plugin.failed = true;
            PublishDesktopHostSnapshot();
            RetireOwner(static_cast<PluginOwner*>(plugin.owner));
            HostLogError("Plugin runtime-ready callback threw across ABI: %s", plugin.name.c_str());
        }
    }
}
bool IsRuntimeReady() { return g_runtimeReady; }
bool IsShuttingDown() { return g_shutdownRequested.load(std::memory_order_acquire); }
bool BindMainThread(DWORD threadId) {
    if (!threadId) return false;
    DWORD expected = 0;
    return g_mainThreadId.compare_exchange_strong(expected, threadId) || expected == threadId;
}

void DispatchMainThreadTasks() {
    g_mainWaiters.fetch_add(1, std::memory_order_acq_rel);
    std::unique_lock<std::recursive_mutex> lock(g_callbacks);
    g_mainWaiters.fetch_sub(1, std::memory_order_acq_rel);
    if (!g_initialized || !g_runtimeReady || g_shutdownRequested || !IsMainThread(nullptr)) return;
    DrainTasks(); // exactly one existing finite FIFO batch, with owner checks
}
void TickFrame(float deltaTime) {
    // Every actor must receive its pose rewrite after the native LateUpdate.
    // Native dialogs and GPU calls are outside this gate; desktop callbacks
    // must queue game work and may not synchronously wait for the main thread.
    g_mainWaiters.fetch_add(1, std::memory_order_acq_rel);
    std::unique_lock<std::recursive_mutex> lock(g_callbacks);
    g_mainWaiters.fetch_sub(1, std::memory_order_acq_rel);
    if (!g_initialized) return;
    if (!IsMainThread(nullptr)) { HostLogError("Rejected frame dispatch on non-main thread"); return; }
    if (g_shutdownRequested) { ShutdownOnMainThread(); return; }
    // Readiness is reported by the attached runtime worker, never inferred
    // from an arbitrary caller that happens to attempt a frame dispatch.
    if (!g_runtimeReady) return;
    const uint64_t frameOrdinal = g_frameTicks.fetch_add(1, std::memory_order_relaxed) + 1;
    if (Trace::Enabled()) {
        if (frameOrdinal <= 3 || frameOrdinal % 600 == 0) {
            Trace::Event("TickFrame #%llu delta=%.4f plugins=%zu", static_cast<unsigned long long>(frameOrdinal),
                deltaTime, g_plugins.size());
        }
        static ULONGLONG lastSummary = 0;
        const ULONGLONG now = GetTickCount64();
        if (now - lastSummary >= 5000) {
            lastSummary = now;
            Trace::Event("summary frames=%llu actorTicks=%llu actorEntries=%llu participants=%zu lastActor=%p",
                static_cast<unsigned long long>(frameOrdinal),
                static_cast<unsigned long long>(g_actorTicks.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(ActorEntry::InvocationCount()), ActorEntry::Count(),
                g_lastActor.load(std::memory_order_relaxed));
        }
    }
    DrainTasks();
    for (auto& plugin : g_plugins) {
        if (!plugin.activated || plugin.failed || !plugin.descriptor.onTick) continue;
        try { plugin.descriptor.onTick(plugin.state, deltaTime); }
        catch (...) {
            plugin.failed = true; PublishDesktopHostSnapshot(); RetireOwner(static_cast<PluginOwner*>(plugin.owner));
            HostLogError("Plugin tick threw across ABI: %s", plugin.name.c_str());
        }
    }
}
void TickActor(void* activeActor, float deltaTime) {
    g_mainWaiters.fetch_add(1, std::memory_order_acq_rel);
    std::unique_lock<std::recursive_mutex> lock(g_callbacks);
    g_mainWaiters.fetch_sub(1, std::memory_order_acq_rel);
    if (!g_initialized || g_shutdownRequested || !IsMainThread(nullptr)) return;
    g_lastActor.store(activeActor, std::memory_order_relaxed);
    const uint64_t actorOrdinal = g_actorTicks.fetch_add(1, std::memory_order_relaxed) + 1;
    if (Trace::Enabled() && (actorOrdinal <= 5 || actorOrdinal % 600 == 0)) {
        Trace::Event("TickActor #%llu actor=%p delta=%.4f", static_cast<unsigned long long>(actorOrdinal),
            activeActor, deltaTime);
    }
    for (auto& plugin : g_plugins) {
        if (!plugin.activated || plugin.failed || !plugin.descriptor.onActorTick) continue;
        try { plugin.descriptor.onActorTick(plugin.state, activeActor, deltaTime); }
        catch (...) {
            plugin.failed = true; PublishDesktopHostSnapshot(); RetireOwner(static_cast<PluginOwner*>(plugin.owner));
            HostLogError("Plugin actor tick threw across ABI: %s", plugin.name.c_str());
        }
    }
}
bool TryDispatchDesktopFrame(void (*buildFrame)(void*), void* userData) {
    if (!buildFrame || g_shutdownRequested || g_mainWaiters.load(std::memory_order_acquire)) return false;
    std::unique_lock<std::recursive_mutex> lock(g_callbacks, std::try_to_lock);
    // Recheck after acquisition so a waiter announced concurrently wins the
    // next turn instead of racing a rapid sequence of desktop frames.
    if (!lock.owns_lock() || g_shutdownRequested || g_mainWaiters.load(std::memory_order_acquire)) return false;
    buildFrame(userData);
    return true;
}
void RunInCallbackGate(void (*body)(void*), void* userData) {
    if (!body) return;
    // Same announcement TickFrame makes: while a main-thread callback waits for
    // the gate, desktop frames back off instead of taking it again every render
    // pass. The gate is recursive, so a participant reached from inside
    // TickFrame (or from a desktop callback on this thread) simply continues.
    g_mainWaiters.fetch_add(1, std::memory_order_acq_rel);
    std::unique_lock<std::recursive_mutex> lock(g_callbacks);
    g_mainWaiters.fetch_sub(1, std::memory_order_acq_rel);
    body(userData);
}
bool SnapshotDesktopHost(DesktopHostSnapshot& output) {
    const auto snapshot = g_desktopSnapshot.load(std::memory_order_acquire);
    if (!snapshot) return false;
    output = *snapshot;
    output.shuttingDown = g_shutdownRequested.load(std::memory_order_acquire);
    return true;
}
void DispatchDesktopPanel(const char* pluginId) {
    if (!pluginId || !pluginId[0]) return;
    std::unique_lock<std::recursive_mutex> lock(g_callbacks, std::try_to_lock);
    if (!lock.owns_lock() || !g_initialized || g_shutdownRequested) return;
    for (auto& plugin : g_plugins) {
        if (!plugin.activated || plugin.failed || !plugin.descriptor.onDesktopUi ||
            plugin.id != pluginId) continue;
        try { plugin.descriptor.onDesktopUi(plugin.state); }
        catch (...) {
            plugin.failed = true; PublishDesktopHostSnapshot(); RetireOwner(static_cast<PluginOwner*>(plugin.owner));
            HostLogError("Plugin desktop callback threw across ABI: %s", plugin.name.c_str());
        }
        return;
    }
}
void Shutdown() {
    if (std::strcmp(g_shutdownPhase.load(std::memory_order_acquire), "complete") == 0) return;
    // QuitGuard's force-exit watchdog must never wait behind a stuck render or
    // managed callback. It only requests shutdown; normal main dispatch performs
    // callbacks. If Unity has stopped, the watchdog still terminates.
    if (!IsMainThread(nullptr)) {
        RequestShutdown();
        HostLogWarn("Shutdown requested off main thread; callbacks deferred to main dispatch"); return;
    }
    RequestShutdown();
    StopHotkeyPoller();
    g_shutdownPhaseTick.store(GetTickCount64(), std::memory_order_relaxed);
    g_shutdownPhase.store("waiting-callback-gate", std::memory_order_release);
    std::lock_guard<std::recursive_mutex> lock(g_callbacks);
    if (!g_initialized) return;
    ShutdownOnMainThread();
}
void RequestShutdown() { g_shutdownRequested.store(true, std::memory_order_release); }
const std::vector<LoadedPlugin>& GetLoadedPlugins() { return g_plugins; }

#if defined(GAKULOADER_TEST_SEAMS)
void SetHookEnableObserverForTest(void (*observer)(uint32_t)) { g_hookEnableObserver = observer; }
void FailHookEnableForTest(uint32_t index) { g_hookEnableFailure = index; }
void SetActorEntryRegisterObserverForTest(void (*observer)()) { g_actorEntryRegisterObserver = observer; }
void RetireOwnerForTest(void* owner) { RetireOwner(static_cast<PluginOwner*>(owner)); }
void DisableHotkeyPollerForTest() { g_hotkeyPollerDisabled.store(true, std::memory_order_release); }
bool PumpHotkeysForTest(const Hotkeys::PollContext& context) {
    Hotkeys::PollResult result;
    Hotkeys::Poll(context, result);
    if (result.managerToggle) DesktopUi::ToggleManager();
    for (const auto& action : result.actions) DispatchHotkey(action);
    return result.managerToggle;
}
#endif
}
