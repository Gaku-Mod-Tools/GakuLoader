#pragma once

// The hook targets in the test suite must never be inlined away: MinHook needs a real
// function body to relocate, and an inlined target silently defeats the whole isolation
// test. MSVC spells this __declspec(noinline); GCC/MinGW (still used for the
// heterogeneous fixtures) spells it __attribute__((noinline)).
#if defined(_MSC_VER)
#define GAKU_NOINLINE __declspec(noinline)
#else
#define GAKU_NOINLINE __attribute__((noinline))
#endif
