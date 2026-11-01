#pragma once
#include "GakuPlugin.h"

namespace GakuLoader::ActorEntry {

// The loader owns the only detour on CampusActorController::LateUpdate. Plugins
// that need that entry register a participant here; they never acquire a second
// lease on the same target. This is deliberately one named seam, not a hook bus.
GakuStatus Register(void* owner, const GakuActorEntryBinding* binding);
GakuStatus Unregister(void* owner, uint32_t phase);
// Called by the owner registry when a plugin retires or fails.
void RetireOwner(void* owner);

// Runs before participants, then `original` exactly once (when non-null), then
// after participants. Participants are snapshotted before the original runs, so
// registration from inside a callback takes effect on the next invocation. A
// re-registration publishes a new immutable entry; a snapshot already taken
// keeps the callback and userData it captured.
// Exceptions never escape into the game; the original still runs.
void Invoke(void* actor, void (*original)(void* actor, void* methodInfo), void* methodInfo);

// Test/diagnostic surface: total registered participants.
size_t Count();
// Monotonic count of completed invocations, including frames with no
// participants. Used by the acceptance summary and the frame-pump fixture.
uint64_t InvocationCount();

}
