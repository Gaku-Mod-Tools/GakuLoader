#include "ActorEntry.hpp"
#include "Host.hpp"
#include "Trace.hpp"
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace GakuLoader::ActorEntry {

namespace {

constexpr size_t kMaxEntries = 16;   // 8 owners x 2 phases is already generous
constexpr size_t kMaxName = 64;

struct Entry {
    void* owner{nullptr};
    uint32_t phase{0};
    std::string name;
    void (*callback)(void* userData, void* actor){nullptr};
    void* userData{nullptr};
    // A misbehaving callback is contained per invocation; report the first
    // failure for this participant instead of writing a line every frame.
    std::atomic_bool failureReported{false};
};

std::mutex g_mutex;
std::vector<std::shared_ptr<Entry>> g_entries; // registration order
std::atomic_bool g_reportedFirstInvocation{false};
std::atomic<uint64_t> g_invocations{0};

// Bounded trace: the first few invocations prove the phase order for the
// acceptance record, the occasional later one proves it is still the same
// sequence once the scene has settled. Never per-frame.
constexpr uint64_t kTraceHead = 5;
constexpr uint64_t kTraceStride = 600;

void JoinNames(char* output, size_t capacity, const std::array<std::shared_ptr<Entry>, kMaxEntries>& phase,
    size_t count, const char* fallback) {
    output[0] = '\0';
    size_t used = 0;
    for (size_t index = 0; index < count; ++index) {
        const std::string& name = phase[index]->name;
        const char* text = name.empty() ? "(unnamed)" : name.c_str();
        const size_t length = std::char_traits<char>::length(text);
        if (used + length + 3 >= capacity) break;
        if (used) { output[used++] = ','; output[used] = '\0'; }
        std::memcpy(output + used, text, length);
        used += length;
        output[used] = '\0';
    }
    if (!used) {
        const size_t length = std::char_traits<char>::length(fallback);
        if (length < capacity) std::memcpy(output, fallback, length + 1);
    }
}

void Run(const std::shared_ptr<Entry>& entry, void* actor) {
    try {
        entry->callback(entry->userData, actor);
    } catch (...) {
        if (!entry->failureReported.exchange(true)) {
            LogError("[ActorEntry] participant %s threw across the shared entry; "
                     "the game original and other participants keep running",
                entry->name.empty() ? "(unnamed)" : entry->name.c_str());
        }
    }
}

struct PhaseBatch {
    const std::shared_ptr<Entry>* entries;
    size_t count;
    void* actor;
};

void RunPhase(void* raw) {
    const auto& batch = *static_cast<const PhaseBatch*>(raw);
    for (size_t index = 0; index < batch.count; ++index) Run(batch.entries[index], batch.actor);
}

// Participant callbacks read plugin state that the desktop UI also writes
// (free camera, subtitle offsets, ...). They therefore run inside the same gate
// that serializes tick and desktop callbacks; the game original stays outside it
// so a long native update never blocks a render pass. A phase is one gate
// acquisition, so the UI cannot slip in between two participants of the same
// phase.
void RunPhaseGated(const std::array<std::shared_ptr<Entry>, kMaxEntries>& entries, size_t count, void* actor) {
    if (!count) return;
    PhaseBatch batch{entries.data(), count, actor};
    RunInCallbackGate(&RunPhase, &batch);
}

}

GakuStatus Register(void* owner, const GakuActorEntryBinding* binding) {
    if (!owner || !binding || !binding->callback) return GAKU_E_INVALID_ARGUMENT;
    if (binding->phase != GAKU_ACTOR_ENTRY_BEFORE && binding->phase != GAKU_ACTOR_ENTRY_AFTER)
        return GAKU_E_INVALID_ARGUMENT;
    if (binding->structSize != sizeof(GakuActorEntryBinding)) return GAKU_E_SIZE;
    if (binding->abiVersion != GAKU_SERVICE_VERSION) return GAKU_E_VERSION;
    std::string name;
    if (binding->name) {
        size_t length = 0;
        while (length < kMaxName && binding->name[length]) ++length;
        name.assign(binding->name, length);
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto& entry : g_entries) {
        if (entry->owner != owner || entry->phase != binding->phase) continue;
        // Same owner and phase: swap in a complete replacement entry at the same
        // index so registration order is stable. Invoke only copies the
        // shared_ptr, so a snapshot already taken keeps the immutable version it
        // captured - a participant that re-registers from inside a callback can
        // no longer change the callback or userData that this call will run.
        auto replacement = std::make_shared<Entry>();
        replacement->owner = owner;
        replacement->phase = binding->phase;
        replacement->name = std::move(name);
        replacement->callback = binding->callback;
        replacement->userData = binding->userData;
        entry = std::move(replacement);
        return GAKU_OK;
    }
    if (g_entries.size() >= kMaxEntries) return GAKU_E_LIMIT;
    auto entry = std::make_shared<Entry>();
    entry->owner = owner;
    entry->phase = binding->phase;
    entry->name = std::move(name);
    entry->callback = binding->callback;
    entry->userData = binding->userData;
    g_entries.push_back(std::move(entry));
    // Registration is a once-per-plugin event and is the only way to tell from
    // the log which participant the shared entry is actually running.
    LogInfo("[ActorEntry] registered %s for %s (participants=%zu)", g_entries.back()->name.c_str(),
        binding->phase == GAKU_ACTOR_ENTRY_BEFORE ? "before" : "after", g_entries.size());
    return GAKU_OK;
}

GakuStatus Unregister(void* owner, uint32_t phase) {
    if (!owner) return GAKU_E_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto it = g_entries.begin(); it != g_entries.end(); ++it) {
        if ((*it)->owner == owner && (*it)->phase == phase) {
            LogInfo("[ActorEntry] unregistered %s (participants=%zu)", (*it)->name.c_str(), g_entries.size() - 1);
            g_entries.erase(it);
            return GAKU_OK;
        }
    }
    return GAKU_E_UNAVAILABLE;
}

void RetireOwner(void* owner) {
    if (!owner) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto removed = std::erase_if(g_entries, [owner](const auto& entry) { return entry->owner == owner; });
    if (removed) LogInfo("[ActorEntry] retired %zu participant(s) for owner %p", removed, owner);
}

void Invoke(void* actor, void (*original)(void* actor, void* methodInfo), void* methodInfo) {
    // Snapshot both phases before the game original runs: a participant that
    // registers from inside the original must not be appended to a half-run
    // sequence. shared_ptr keeps a concurrently retired participant alive until
    // this invocation finishes.
    std::array<std::shared_ptr<Entry>, kMaxEntries> before{}, after{};
    size_t beforeCount = 0, afterCount = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (const auto& entry : g_entries) {
            if (entry->phase == GAKU_ACTOR_ENTRY_BEFORE && beforeCount < kMaxEntries)
                before[beforeCount++] = entry;
            else if (entry->phase == GAKU_ACTOR_ENTRY_AFTER && afterCount < kMaxEntries)
                after[afterCount++] = entry;
        }
    }
    if (!g_reportedFirstInvocation.exchange(true) && (beforeCount || afterCount)) {
        LogInfo("[ActorEntry] shared actor entry active: %zu before, %zu after participant(s)",
            beforeCount, afterCount);
    }
    const uint64_t ordinal = g_invocations.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool traced = Trace::Enabled() && (ordinal <= kTraceHead || ordinal % kTraceStride == 0);
    const ULONGLONG started = traced ? GetTickCount64() : 0;
    RunPhaseGated(before, beforeCount, actor);
    // The game original runs between the two gate acquisitions: it is not
    // plugin state, and holding the gate across a native update would block the
    // desktop frame for its whole duration.
    if (original) original(actor, methodInfo);
    RunPhaseGated(after, afterCount, actor);
    if (traced) {
        char beforeNames[256], afterNames[256];
        JoinNames(beforeNames, sizeof(beforeNames), before, beforeCount, "none");
        JoinNames(afterNames, sizeof(afterNames), after, afterCount, "none");
        // One line carries the whole phase sequence, so the record does not
        // depend on interleaving with other threads' log writes.
        Trace::Event("actor-entry #%llu actor=%p before=[%s] original=1 after=[%s] elapsed_ms=%llu",
            static_cast<unsigned long long>(ordinal), actor, beforeNames, afterNames,
            static_cast<unsigned long long>(GetTickCount64() - started));
    }
}

uint64_t InvocationCount() { return g_invocations.load(std::memory_order_relaxed); }

size_t Count() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_entries.size();
}

}
