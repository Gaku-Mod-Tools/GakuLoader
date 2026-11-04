#pragma once
#include "GakuFileDrop.h"
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace GakuLoader::DesktopFileDrop {
// UI-thread-only mailbox. No plugin memory or callbacks are retained.
class Mailbox {
    std::string targetOwner_, batchOwner_, paths_;
    GakuFileDropTarget target_{};
    uint32_t count_ = 0;
public:
    void BeginFrame() { targetOwner_.clear(); }
    void EndFrame() { if (targetOwner_ != batchOwner_) ClearBatch(); }
    void ClearBatch() { batchOwner_.clear(); paths_.clear(); count_ = 0; }
    void Clear() { targetOwner_.clear(); ClearBatch(); }
    GakuStatus SetTarget(const char* owner, const GakuFileDropTarget* target) {
        if (!owner || !owner[0]) return GAKU_E_INVALID_ARGUMENT;
        if (!target) { targetOwner_.clear(); ClearBatch(); return GAKU_OK; }
        if (target->abiVersion != GAKU_FILE_DROP_VERSION) return GAKU_E_VERSION;
        if (target->structSize < sizeof(*target)) return GAKU_E_SIZE;
        if (!std::isfinite(target->left) || !std::isfinite(target->top) ||
            !std::isfinite(target->right) || !std::isfinite(target->bottom) ||
            target->left >= target->right || target->top >= target->bottom) return GAKU_E_INVALID_ARGUMENT;
        targetOwner_ = owner; target_ = *target;
        return GAKU_OK;
    }
    bool Accept(const char* selectedOwner, float x, float y, const std::vector<std::string>& paths) {
        if (!selectedOwner || targetOwner_ != selectedOwner || targetOwner_.empty() || count_ ||
            !std::isfinite(x) || !std::isfinite(y) ||
            x < target_.left || x >= target_.right || y < target_.top || y >= target_.bottom ||
            paths.empty() || paths.size() > GAKU_FILE_DROP_MAX_FILES) return false;
        size_t bytes = 0;
        for (const auto& path : paths) {
            if (path.empty() || path.find('\0') != std::string::npos ||
                path.size() >= GAKU_FILE_DROP_MAX_BYTES - bytes) return false;
            bytes += path.size() + 1;
        }
        std::string packed; packed.reserve(bytes);
        for (const auto& path : paths) { packed.append(path); packed.push_back('\0'); }
        batchOwner_ = selectedOwner; paths_.swap(packed); count_ = static_cast<uint32_t>(paths.size());
        return true;
    }
    GakuStatus Poll(const char* owner, uint32_t size, GakuFileDropResult* result,
        uint32_t capacity, char* output) {
        if (!owner || !owner[0] || !result || (capacity && !output) || (!capacity && output))
            return GAKU_E_INVALID_ARGUMENT;
        if (size < sizeof(*result)) return GAKU_E_SIZE;
        const bool own = batchOwner_ == owner && targetOwner_ == owner;
        const GakuFileDropResult metadata{GAKU_FILE_DROP_VERSION, sizeof(*result),
            own ? count_ : 0u, own ? static_cast<uint32_t>(paths_.size()) : 0u};
        if (capacity && capacity < metadata.requiredBytes) return GAKU_E_SIZE;
        if (capacity && own && count_) { std::memcpy(output, paths_.data(), paths_.size()); ClearBatch(); }
        *result = metadata;
        return GAKU_OK;
    }
};
} // namespace GakuLoader::DesktopFileDrop
