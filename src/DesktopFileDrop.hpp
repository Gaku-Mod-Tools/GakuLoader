// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

#pragma once
#include "GakuFileDrop.h"
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace GakuLoader::DesktopFileDrop {
enum class AcceptResult {
    Accepted, MissingOwner, NoTarget, OwnerMismatch, BatchPending, InvalidPoint,
    OutsideTarget, EmptyBatch, TooManyFiles, EmptyPath, EmbeddedNul, PayloadTooLarge
};
inline const char* AcceptResultName(AcceptResult result) {
    switch (result) {
    case AcceptResult::Accepted: return "accepted";
    case AcceptResult::MissingOwner: return "missing-owner";
    case AcceptResult::NoTarget: return "no-target";
    case AcceptResult::OwnerMismatch: return "owner-mismatch";
    case AcceptResult::BatchPending: return "batch-pending";
    case AcceptResult::InvalidPoint: return "invalid-point";
    case AcceptResult::OutsideTarget: return "outside-target";
    case AcceptResult::EmptyBatch: return "empty-batch";
    case AcceptResult::TooManyFiles: return "too-many-files";
    case AcceptResult::EmptyPath: return "empty-path";
    case AcceptResult::EmbeddedNul: return "embedded-nul";
    case AcceptResult::PayloadTooLarge: return "payload-too-large";
    }
    return "unknown";
}
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
    const char* TargetOwner() const { return targetOwner_.c_str(); }
    const GakuFileDropTarget& Target() const { return target_; }
    AcceptResult AcceptDetailed(const char* selectedOwner, float x, float y, const std::vector<std::string>& paths) {
        if (!selectedOwner || !selectedOwner[0]) return AcceptResult::MissingOwner;
        if (targetOwner_.empty()) return AcceptResult::NoTarget;
        if (targetOwner_ != selectedOwner) return AcceptResult::OwnerMismatch;
        if (count_) return AcceptResult::BatchPending;
        if (!std::isfinite(x) || !std::isfinite(y)) return AcceptResult::InvalidPoint;
        if (x < target_.left || x >= target_.right || y < target_.top || y >= target_.bottom)
            return AcceptResult::OutsideTarget;
        if (paths.empty()) return AcceptResult::EmptyBatch;
        if (paths.size() > GAKU_FILE_DROP_MAX_FILES) return AcceptResult::TooManyFiles;
        size_t bytes = 0;
        for (const auto& path : paths) {
            if (path.empty()) return AcceptResult::EmptyPath;
            if (path.find('\0') != std::string::npos) return AcceptResult::EmbeddedNul;
            if (path.size() >= GAKU_FILE_DROP_MAX_BYTES - bytes) return AcceptResult::PayloadTooLarge;
            bytes += path.size() + 1;
        }
        std::string packed; packed.reserve(bytes);
        for (const auto& path : paths) { packed.append(path); packed.push_back('\0'); }
        batchOwner_ = selectedOwner; paths_.swap(packed); count_ = static_cast<uint32_t>(paths.size());
        return AcceptResult::Accepted;
    }
    bool Accept(const char* selectedOwner, float x, float y, const std::vector<std::string>& paths) {
        return AcceptDetailed(selectedOwner, x, y, paths) == AcceptResult::Accepted;
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
