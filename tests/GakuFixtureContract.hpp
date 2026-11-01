#pragma once
#include <stdint.h>
struct GakuFixtureSnapshot {
    uint32_t activateCount, readyCount, tickCount, actorTickCount, desktopCount, shutdownCount;
    uint32_t activateThread, readyThread, tickThread, shutdownThread, hotkeyThread;
    uint32_t taskCount, nestedTaskCount, overlapCount, contractFailures;
    uint32_t actorACount, actorBCount, otherActorCount;
    uint32_t hotkeyCount, hotkeyRegistrations, hotkeyStatus;
};
using FixtureSnapshotFn = GakuFixtureSnapshot (*)();
using FixtureEnqueueFn = int32_t (*)();
using FixtureHookFn = int32_t (*)(uint32_t action);
using FixtureBlockFn = uint32_t (*)(uint32_t action);
using FixtureVoidFn = void (*)();
