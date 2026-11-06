#pragma once
#include "GakuPlugin.h"

/* Optional owner-bound cancellation signal. Safe from any plugin thread,
 * including a runtime-ready callback: no callback or service lock is taken.
 * The context and function remain valid until process exit. */
#define GAKU_SERVICE_LIFECYCLE "gakuloader.lifecycle"
#define GAKU_LIFECYCLE_VERSION 1u

#ifdef __cplusplus
extern "C" {
#endif
typedef struct GakuLifecycleServiceV1 {
    uint32_t abiVersion;
    uint32_t structSize;
    void* context;
    /* Nonzero when host shutdown is requested or this owner is retired. */
    uint32_t (*isStopping)(void* context);
} GakuLifecycleServiceV1;
#ifdef __cplusplus
}
#endif
