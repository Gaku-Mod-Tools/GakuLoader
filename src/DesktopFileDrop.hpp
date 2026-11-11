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
    // Live shell-drag state (OLE IDropTarget on the host window). It is never
    // retained past a drag: DragLeave, Drop, Clear and window teardown wipe it.
    bool dragActive_ = false;
    float dragX_ = -1.0f, dragY_ = -1.0f;
    uint32_t dragGeneration_ = 0, dragCount_ = 0;
    std::string dragPaths_;
    // A host above Explorer's integrity level never receives an OLE drag, so the
    // absence of hover events is a property of the host rather than of the
    // current drag. Plugins need that distinction to explain the dead preview.
    bool hostBlocked_ = false;
    bool hostElevated_ = false;
public:
    void SetHostBlocked(bool blocked, bool elevated) { hostBlocked_ = blocked; hostElevated_ = elevated; }
    bool HostBlocked() const { return hostBlocked_; }
    bool HostElevated() const { return hostElevated_; }
    uint32_t HostFlags() const {
        return (hostBlocked_ ? GAKU_FILE_DRAG_HOST_BLOCKED : 0u) |
            (hostElevated_ ? GAKU_FILE_DRAG_HOST_ELEVATED : 0u);
    }
    void BeginFrame() { targetOwner_.clear(); }
    void EndFrame() { if (targetOwner_ != batchOwner_) ClearBatch(); }
    void ClearBatch() { batchOwner_.clear(); paths_.clear(); count_ = 0; }
    void Clear() { targetOwner_.clear(); ClearBatch(); EndDrag(); }
    bool TargetContains(float x, float y) const {
        return std::isfinite(x) && std::isfinite(y) &&
            x >= target_.left && x < target_.right && y >= target_.top && y < target_.bottom;
    }
    // The host copies the dragged paths once per OS drag; peeking them every
    // UI frame must stay allocation-free.
    void BeginDrag(std::string packed, uint32_t count) {
        dragActive_ = true; dragX_ = dragY_ = -1.0f;
        dragPaths_.swap(packed); dragCount_ = count; ++dragGeneration_;
    }
    void UpdateDrag(float x, float y) { if (!dragActive_) return; dragX_ = x; dragY_ = y; }
    void EndDrag() { dragActive_ = false; dragX_ = dragY_ = -1.0f; dragPaths_.clear(); dragCount_ = 0; }
    bool DragActive() const { return dragActive_; }
    uint32_t DragCount() const { return dragCount_; }
    bool DragInsideTarget() const { return dragActive_ && TargetContains(dragX_, dragY_); }
    float DragX() const { return dragX_; }
    float DragY() const { return dragY_; }
    // Non-consuming peek of the live drag. Ownership follows the registered
    // target, so an unselected plugin can never observe another panel's drag.
    GakuStatus QueryDrag(const char* owner, uint32_t stateSize, GakuFileDragState* state,
        uint32_t capacity, char* output) {
        if (!owner || !owner[0] || !state || (capacity && !output) || (!capacity && output))
            return GAKU_E_INVALID_ARGUMENT;
        if (stateSize < sizeof(*state)) return GAKU_E_SIZE;
        GakuFileDragState value{GAKU_FILE_DRAG_VERSION, sizeof(value),
            HostFlags(), 0u, 0u, 0u, -1.0f, -1.0f};
        if (targetOwner_ == owner && dragActive_) {
            value.generation = dragGeneration_;
            value.fileCount = dragCount_;
            value.requiredBytes = static_cast<uint32_t>(dragPaths_.size());
            value.x = dragX_; value.y = dragY_;
            value.flags = GAKU_FILE_DRAG_ACTIVE | HostFlags();
            if (TargetContains(dragX_, dragY_)) value.flags |= GAKU_FILE_DRAG_INSIDE_TARGET;
        }
        if (capacity) {
            if (capacity < value.requiredBytes) { *state = value; return GAKU_E_SIZE; }
            if (value.requiredBytes) std::memcpy(output, dragPaths_.data(), value.requiredBytes);
        }
        *state = value;
        return GAKU_OK;
    }
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
        if (!TargetContains(x, y)) return AcceptResult::OutsideTarget;
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
