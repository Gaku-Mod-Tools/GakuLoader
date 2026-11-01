// Production-backend regression for the batched hook path.
//
// The loader enables and disables its whole lease set through
// MH_EnableHooks/MH_DisableHooks. That batch used to freeze the process with
// ALL_HOOKS_POS, so every thread whose RIP sat in *any* hook's trampoline was
// relocated - including hooks owned by another plugin that this batch did not
// touch. This fixture links the vendored MinHook directly (no GAKULOADER_TEST_SEAMS,
// no PluginHost seam) and keeps a test-owned thread parked at a known address.
#include "GakuTestCompat.hpp"
#include <windows.h>
#include <MinHook.h>
#include <cstdio>
#include <cstdint>

namespace {

using TargetFn = int (*)(int);
TargetFn originalA = nullptr;
TargetFn originalB = nullptr;

// Distinct bodies, no tail call: each needs a real prologue for MinHook.
GAKU_NOINLINE int TargetA(int value) { volatile int result = value + 11; return result; }
GAKU_NOINLINE int TargetB(int value) { volatile int result = value + 23; return result; }
int DetourA(int value) { return originalA(value) + 100; }
int DetourB(int value) { return originalB(value) + 200; }

DWORD WINAPI Parked(LPVOID) { return 0; }

struct ParkedThread {
    HANDLE handle{nullptr};
    bool valid{false};

    bool Create() {
        handle = CreateThread(nullptr, 0, Parked, nullptr, CREATE_SUSPENDED, nullptr);
        valid = handle != nullptr;
        return valid;
    }
    bool ParkAt(void* address) {
        CONTEXT context{};
        context.ContextFlags = CONTEXT_CONTROL;
        if (!GetThreadContext(handle, &context)) return false;
#if defined(_M_X64) || defined(__x86_64__)
        context.Rip = reinterpret_cast<DWORD64>(address);
#else
        context.Eip = reinterpret_cast<DWORD>(address);
#endif
        return SetThreadContext(handle, &context) != 0;
    }
    void* InstructionPointer() {
        CONTEXT context{};
        context.ContextFlags = CONTEXT_CONTROL;
        if (!GetThreadContext(handle, &context)) return nullptr;
#if defined(_M_X64) || defined(__x86_64__)
        return reinterpret_cast<void*>(context.Rip);
#else
        return reinterpret_cast<void*>(context.Eip);
#endif
    }
    ~ParkedThread() {
        if (!handle) return;
        // The parked thread never executes; its RIP is only a probe.
        TerminateThread(handle, 0);
        WaitForSingleObject(handle, 1000);
        CloseHandle(handle);
    }
};

int failures = 0;
void Check(bool condition, const char* what) {
    if (condition) return;
    ++failures;
    std::printf("FAIL: %s\n", what);
}

bool Setup() {
    if (MH_Initialize() != MH_OK) return false;
    if (MH_CreateHook(reinterpret_cast<LPVOID>(&TargetA), reinterpret_cast<LPVOID>(&DetourA),
            reinterpret_cast<LPVOID*>(&originalA)) != MH_OK) return false;
    if (MH_CreateHook(reinterpret_cast<LPVOID>(&TargetB), reinterpret_cast<LPVOID>(&DetourB),
            reinterpret_cast<LPVOID*>(&originalB)) != MH_OK) return false;
    if (MH_EnableHook(reinterpret_cast<LPVOID>(&TargetA)) != MH_OK) return false;
    if (MH_EnableHook(reinterpret_cast<LPVOID>(&TargetB)) != MH_OK) return false;
    return TargetA(1) == 112 && TargetB(1) == 224;
}

// Disabling A must leave a thread parked in B's trampoline exactly where it was:
// B keeps its patch, so moving that thread to the B target would make it re-enter
// the B detour when it resumes.
void DisableLeavesUnselectedTargetAlone() {
    ParkedThread thread;
    if (!thread.Create() || !thread.ParkAt(reinterpret_cast<void*>(originalB))) {
        Check(false, "parked thread for disable case");
        return;
    }
    LPVOID batch[] = {reinterpret_cast<LPVOID>(&TargetA)};
    const auto status = MH_DisableHooks(batch, 1);
    void* rip = thread.InstructionPointer();
    Check(status == MH_OK, "MH_DisableHooks({A}) returns MH_OK");
    Check(rip == reinterpret_cast<void*>(originalB), "unselected B trampoline RIP is not relocated on disable");
    Check(rip != reinterpret_cast<void*>(&TargetB), "unselected B RIP did not become the patched target");
    Check(TargetB(1) == 224, "B is still hooked after disabling A");
    Check(TargetA(1) == 12, "A is disabled");
}

// Enabling A must not move a thread parked on B's unpatched entry into B's
// trampoline, for the symmetric reason.
void EnableLeavesUnselectedTargetAlone() {
    if (MH_DisableHook(reinterpret_cast<LPVOID>(&TargetB)) != MH_OK) {
        Check(false, "disable B before enable case");
        return;
    }
    ParkedThread thread;
    if (!thread.Create() || !thread.ParkAt(reinterpret_cast<void*>(&TargetB))) {
        Check(false, "parked thread for enable case");
        MH_EnableHook(reinterpret_cast<LPVOID>(&TargetB));
        return;
    }
    LPVOID batch[] = {reinterpret_cast<LPVOID>(&TargetA)};
    const auto status = MH_EnableHooks(batch, 1);
    void* rip = thread.InstructionPointer();
    Check(status == MH_OK, "MH_EnableHooks({A}) returns MH_OK");
    Check(rip == reinterpret_cast<void*>(&TargetB), "unselected B entry RIP is not relocated on enable");
    Check(rip != reinterpret_cast<void*>(originalB), "unselected B RIP did not become the trampoline");
    Check(TargetB(1) == 24, "B is still unhooked after enabling A");
    Check(TargetA(1) == 112, "A is enabled again");
    Check(MH_EnableHook(reinterpret_cast<LPVOID>(&TargetB)) == MH_OK, "B can be re-enabled");
}

// The selected entry must still be relocated: disabling A has to move a thread
// that was inside A's trampoline back to A's restored entry.
void SelectedTargetIsRelocated() {
    ParkedThread thread;
    if (!thread.Create() || !thread.ParkAt(reinterpret_cast<void*>(originalA))) {
        Check(false, "parked thread for selected case");
        return;
    }
    LPVOID batch[] = {reinterpret_cast<LPVOID>(&TargetA)};
    const auto status = MH_DisableHooks(batch, 1);
    Check(status == MH_OK, "MH_DisableHooks({A}) returns MH_OK for selected case");
    Check(thread.InstructionPointer() == reinterpret_cast<void*>(&TargetA),
        "selected A trampoline RIP is relocated to the restored target");
    Check(TargetA(1) == 12, "A stays disabled");
}

// A batch containing both entries is still one window and both transitions are
// observed; this is the path the loader uses for its own leases.
void BatchCoversEverySelectedEntry() {
    if (MH_EnableHook(reinterpret_cast<LPVOID>(&TargetA)) != MH_OK) {
        Check(false, "re-enable A");
        return;
    }
    ParkedThread threadA, threadB;
    if (!threadA.Create() || !threadA.ParkAt(reinterpret_cast<void*>(originalA)) ||
        !threadB.Create() || !threadB.ParkAt(reinterpret_cast<void*>(originalB))) {
        Check(false, "parked threads for whole-batch case");
        return;
    }
    LPVOID batch[] = {reinterpret_cast<LPVOID>(&TargetA), reinterpret_cast<LPVOID>(&TargetB)};
    const auto status = MH_DisableHooks(batch, 2);
    Check(status == MH_OK, "MH_DisableHooks({A,B}) returns MH_OK");
    Check(threadA.InstructionPointer() == reinterpret_cast<void*>(&TargetA), "A relocated in a two-entry batch");
    Check(threadB.InstructionPointer() == reinterpret_cast<void*>(&TargetB), "B relocated in a two-entry batch");
    Check(TargetA(1) == 12 && TargetB(1) == 24, "both entries are disabled");
}

}

int main() {
    if (!Setup()) {
        std::printf("FAIL: could not prepare the two-hook fixture\n");
        return 2;
    }
    DisableLeavesUnselectedTargetAlone();
    EnableLeavesUnselectedTargetAlone();
    SelectedTargetIsRelocated();
    BatchCoversEverySelectedEntry();
    if (failures) {
        std::printf("FAIL: minhook batch isolation, %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("PASS: minhook batch relocation covers exactly the selected entries (enable/disable)\n");
    return 0;
}
