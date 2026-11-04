#pragma once
#include "GakuPlugin.h"

/* Optional process-wide UI language preference. Poll from desktop callbacks;
 * no plugin callbacks, strings or allocation ownership cross this boundary.
 * This header does not extend or change the frozen GakuPlugin.h tables. */
#define GAKU_SERVICE_LANGUAGE "gakuloader.language"
#define GAKU_LANGUAGE_VERSION 1u
#define GAKU_LANGUAGE_ENGLISH 0u
#define GAKU_LANGUAGE_SIMPLIFIED_CHINESE 1u
#define GAKU_LANGUAGE_TRADITIONAL_CHINESE 2u
#define GAKU_LANGUAGE_JAPANESE 3u

#ifdef __cplusplus
extern "C" {
#endif
typedef struct GakuUiLanguageV1 {
    uint32_t abiVersion;
    uint32_t structSize;
    uint32_t language;
    uint32_t reserved;
} GakuUiLanguageV1;
typedef struct GakuLanguageServiceV1 {
    uint32_t abiVersion;
    uint32_t structSize;
    GakuStatus (*getLanguage)(uint32_t outputSize, GakuUiLanguageV1* output);
} GakuLanguageServiceV1;
#ifdef __cplusplus
}
#endif
