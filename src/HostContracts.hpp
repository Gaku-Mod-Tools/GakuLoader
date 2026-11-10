// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

#pragma once
#include "GakuPlugin.h"
#include <cstring>
namespace GakuLoader {
// Exact version negotiation; a larger caller buffer is permitted but only the
// table's advertised bytes are written. No reads/copies on undersized buffers.
inline GakuStatus CopyServiceTable(const void* source, uint32_t sourceSize,
    uint32_t requestedVersion, uint32_t outputSize, void* output) {
    if (!source || !output || sourceSize < sizeof(GakuServiceHeader)) return GAKU_E_INVALID_ARGUMENT;
    GakuServiceHeader header{};
    std::memcpy(&header, source, sizeof(header));
    if (header.structSize != sourceSize) return GAKU_E_SIZE;
    if (header.abiVersion != requestedVersion) return GAKU_E_VERSION;
    if (outputSize < sourceSize) return GAKU_E_SIZE;
    std::memcpy(output, source, sourceSize);
    return GAKU_OK;
}
}
