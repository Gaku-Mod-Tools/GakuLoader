// SPDX-License-Identifier: MIT
//
// GakuLoader Public Plugin SDK
// See the applicable MIT license notice.

#pragma once
#include "GakuPlugin.h"

#define GAKU_SERVICE_PROCESS_LIFETIME_HOOKS "gakuloader.process-lifetime-hooks"
#define GAKU_PROCESS_LIFETIME_HOOKS_VERSION 1u

#ifdef __cplusplus
extern "C" {
#endif

/* Optional, separate service: the frozen GakuHookService v1 layout is unchanged.
   Only hooks already owned and enabled by this table's caller can be retained.
   A successful promotion is irreversible for the plugin and survives owner
   retirement and host exit teardown. The plugin must pin its module and retain
   detour/trampoline state before promotion; detours can run until process exit. */
typedef struct GakuProcessLifetimeHooksV1 {
    uint32_t abiVersion; uint32_t structSize;
    void* context;
    /* 1..256 distinct non-null target addresses. Validation is all-or-none:
       a failed request changes no lease. Repeating an already promoted set is
       allowed. release() on a promoted plugin hook returns GAKU_E_CONFLICT. */
    GakuStatus (*retainOwnedHooks)(void* context, uint32_t count, void* const* targets);
} GakuProcessLifetimeHooksV1;

#ifdef __cplusplus
}
#endif
