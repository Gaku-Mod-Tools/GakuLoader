#include "DesktopFileDrop.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using GakuLoader::DesktopFileDrop::Mailbox;

namespace {
constexpr const char* kOwner = "plugin.alpha";
constexpr const char* kOtherOwner = "plugin.beta";
const GakuFileDropTarget kTarget{GAKU_FILE_DROP_VERSION, sizeof(GakuFileDropTarget),
    100.0f, 200.0f, 500.0f, 700.0f};

std::string Pack(const std::vector<std::string>& files) {
    std::string result;
    for (const auto& file : files) { result.append(file); result.push_back('\0'); }
    return result;
}

GakuFileDropResult Peek(Mailbox& box, const char* owner = kOwner) {
    GakuFileDropResult result{};
    assert(box.Poll(owner, sizeof(result), &result, 0, nullptr) == GAKU_OK);
    assert(result.abiVersion == GAKU_FILE_DROP_VERSION);
    assert(result.structSize == sizeof(result));
    return result;
}

void UnicodeBatchAndExactlyOnce() {
    Mailbox box;
    const std::vector<std::string> files{"曲库/动作 🎵.vmd", "曲库/カメラ.gkcamera", "曲库/song.wav"};
    const auto expected = Pack(files);
    assert(box.SetTarget(kOwner, &kTarget) == GAKU_OK);
    assert(box.Accept(kOwner, 150, 250, files));
    auto metadata = Peek(box);
    assert(metadata.fileCount == files.size() && metadata.requiredBytes == expected.size());
    assert(Peek(box).fileCount == files.size()); // metadata does not consume
    std::vector<char> output(expected.size() + 8, '\x55');
    assert(box.Poll(kOwner, sizeof(metadata), &metadata,
        static_cast<uint32_t>(expected.size()), output.data()) == GAKU_OK);
    assert(metadata.fileCount == files.size());
    assert(std::memcmp(output.data(), expected.data(), expected.size()) == 0);
    assert(std::all_of(output.begin() + expected.size(), output.end(), [](char c) { return c == '\x55'; }));
    assert(Peek(box).fileCount == 0 && Peek(box).requiredBytes == 0);
    assert(box.Accept(kOwner, 150, 250, files)); // consumed slot accepts another batch
}

void OwnerIsolationAndQueueRetention() {
    Mailbox box;
    assert(box.SetTarget(kOwner, &kTarget) == GAKU_OK);
    assert(!box.Accept(kOtherOwner, 150, 250, {"other.vmd"}));
    assert(box.Accept(kOwner, 150, 250, {"first.vmd"}));
    assert(!box.Accept(kOwner, 150, 250, {"second.vmd"}));
    GakuFileDropResult result{};
    std::array<char, 64> output;
    output.fill('\x55');
    assert(box.Poll(kOtherOwner, sizeof(result), &result, static_cast<uint32_t>(output.size()), output.data()) == GAKU_OK);
    assert(result.fileCount == 0 && result.requiredBytes == 0);
    assert(std::all_of(output.begin(), output.end(), [](char c) { return c == '\x55'; }));
    assert(Peek(box).fileCount == 1); // other owner's poll cannot consume
    assert(box.Poll(kOwner, sizeof(result), &result, static_cast<uint32_t>(output.size()), output.data()) == GAKU_OK);
    assert(std::strcmp(output.data(), "first.vmd") == 0);
}

void RetryAndInvalidArguments() {
    Mailbox box;
    assert(box.SetTarget(kOwner, &kTarget) == GAKU_OK);
    assert(box.Accept(kOwner, 150, 250, {"file.vmd"}));
    const GakuFileDropResult sentinel{123u, 456u, 789u, 1011u};
    GakuFileDropResult result = sentinel;
    std::array<char, 32> output;
    output.fill('\x55');
    assert(box.Poll(kOwner, sizeof(result), &result, 1, output.data()) == GAKU_E_SIZE);
    assert(std::memcmp(&result, &sentinel, sizeof(result)) == 0);
    assert(std::all_of(output.begin(), output.end(), [](char c) { return c == '\x55'; }));
    assert(box.Poll(kOwner, sizeof(result) - 1, &result, 32, output.data()) == GAKU_E_SIZE);
    assert(std::memcmp(&result, &sentinel, sizeof(result)) == 0);
    assert(box.Poll(nullptr, sizeof(result), &result, 0, nullptr) == GAKU_E_INVALID_ARGUMENT);
    assert(box.Poll("", sizeof(result), &result, 0, nullptr) == GAKU_E_INVALID_ARGUMENT);
    assert(box.Poll(kOwner, sizeof(result), nullptr, 0, nullptr) == GAKU_E_INVALID_ARGUMENT);
    assert(box.Poll(kOwner, sizeof(result), &result, 1, nullptr) == GAKU_E_INVALID_ARGUMENT);
    assert(box.Poll(kOwner, sizeof(result), &result, 0, output.data()) == GAKU_E_INVALID_ARGUMENT);
    assert(std::memcmp(&result, &sentinel, sizeof(result)) == 0);
    assert(Peek(box).fileCount == 1);
    assert(box.Poll(kOwner, sizeof(result), &result, 32, output.data()) == GAKU_OK);
    assert(std::strcmp(output.data(), "file.vmd") == 0);
}

void HitBounds() {
    Mailbox box;
    assert(box.SetTarget(kOwner, &kTarget) == GAKU_OK);
    assert(!box.Accept(kOwner, 99.9f, 250, {"file.vmd"}));
    assert(!box.Accept(kOwner, 150, 199.9f, {"file.vmd"}));
    assert(!box.Accept(kOwner, 500, 250, {"file.vmd"}));
    assert(!box.Accept(kOwner, 150, 700, {"file.vmd"}));
    assert(!box.Accept(kOwner, std::numeric_limits<float>::quiet_NaN(), 250, {"file.vmd"}));
    assert(!box.Accept(kOwner, 150, std::numeric_limits<float>::infinity(), {"file.vmd"}));
    assert(box.Accept(kOwner, 100, 200, {"file.vmd"}));
    box.ClearBatch();
    assert(box.Accept(kOwner, 499.9f, 699.9f, {"file.vmd"}));
}

void TargetContract() {
    Mailbox box;
    assert(!box.Accept(kOwner, 150, 250, {"file.vmd"}));
    assert(box.SetTarget(nullptr, &kTarget) == GAKU_E_INVALID_ARGUMENT);
    assert(box.SetTarget("", &kTarget) == GAKU_E_INVALID_ARGUMENT);
    auto target = kTarget;
    target.abiVersion++;
    assert(box.SetTarget(kOwner, &target) == GAKU_E_VERSION);
    target = kTarget; target.structSize--;
    assert(box.SetTarget(kOwner, &target) == GAKU_E_SIZE);
    target = kTarget; target.right = target.left;
    assert(box.SetTarget(kOwner, &target) == GAKU_E_INVALID_ARGUMENT);
    target = kTarget; target.bottom = target.top - 1;
    assert(box.SetTarget(kOwner, &target) == GAKU_E_INVALID_ARGUMENT);
    target = kTarget; target.left = std::numeric_limits<float>::quiet_NaN();
    assert(box.SetTarget(kOwner, &target) == GAKU_E_INVALID_ARGUMENT);
    target = kTarget; target.bottom = std::numeric_limits<float>::infinity();
    assert(box.SetTarget(kOwner, &target) == GAKU_E_INVALID_ARGUMENT);
    assert(!box.Accept(kOwner, 150, 250, {"file.vmd"}));
}

void FrameRenewalAndExpiry() {
    Mailbox box;
    assert(box.SetTarget(kOwner, &kTarget) == GAKU_OK);
    box.EndFrame();
    assert(box.Accept(kOwner, 150, 250, {"file.vmd"}));
    box.BeginFrame();
    assert(box.SetTarget(kOwner, &kTarget) == GAKU_OK);
    assert(Peek(box).fileCount == 1); // renewing same page preserves accepted batch
    box.EndFrame();
    box.BeginFrame();
    box.EndFrame(); // plugin page not drawn in this complete frame
    assert(Peek(box).fileCount == 0);
    assert(box.SetTarget(kOwner, &kTarget) == GAKU_OK);
    assert(Peek(box).fileCount == 0); // revisiting cannot reopen stale import
    assert(box.Accept(kOwner, 150, 250, {"next.vmd"}));
    box.BeginFrame();
    assert(box.SetTarget(kOtherOwner, &kTarget) == GAKU_OK);
    box.EndFrame();
    assert(box.SetTarget(kOwner, &kTarget) == GAKU_OK);
    assert(Peek(box).fileCount == 0); // switching selected plugin expires batch
}

void DisableAndClear() {
    Mailbox box;
    assert(box.SetTarget(kOwner, &kTarget) == GAKU_OK);
    assert(box.Accept(kOwner, 150, 250, {"file.vmd"}));
    assert(box.SetTarget(kOwner, nullptr) == GAKU_OK);
    assert(!box.Accept(kOwner, 150, 250, {"next.vmd"}));
    assert(box.SetTarget(kOwner, &kTarget) == GAKU_OK);
    assert(Peek(box).fileCount == 0);
    assert(box.Accept(kOwner, 150, 250, {"next.vmd"}));
    box.Clear();
    assert(!box.Accept(kOwner, 150, 250, {"file.vmd"}));
    assert(box.SetTarget(kOwner, &kTarget) == GAKU_OK);
    assert(Peek(box).fileCount == 0);
}

void MalformedAndOverflow() {
    Mailbox box;
    assert(box.SetTarget(kOwner, &kTarget) == GAKU_OK);
    assert(!box.Accept(kOwner, 150, 250, {}));
    assert(!box.Accept(kOwner, 150, 250, {""}));
    assert(!box.Accept(kOwner, 150, 250, {std::string("one\0two", 7)}));
    assert(!box.Accept(kOwner, 150, 250,
        std::vector<std::string>(GAKU_FILE_DROP_MAX_FILES + 1, "file.vmd")));
    assert(!box.Accept(kOwner, 150, 250, {std::string(GAKU_FILE_DROP_MAX_BYTES, 'x')}));
    assert(!box.Accept(kOwner, 150, 250,
        {std::string(GAKU_FILE_DROP_MAX_BYTES - 2, 'x'), "x"}));
    assert(Peek(box).fileCount == 0);
    assert(box.Accept(kOwner, 150, 250,
        std::vector<std::string>(GAKU_FILE_DROP_MAX_FILES, "file.vmd")));
    assert(Peek(box).fileCount == GAKU_FILE_DROP_MAX_FILES);
    box.ClearBatch();
    assert(box.Accept(kOwner, 150, 250, {std::string(GAKU_FILE_DROP_MAX_BYTES - 1, 'x')}));
    assert(Peek(box).requiredBytes == GAKU_FILE_DROP_MAX_BYTES);
}
void DiagnosticReasonsPreserveBatch() {
    using GakuLoader::DesktopFileDrop::AcceptResult;
    Mailbox box;
    assert(box.AcceptDetailed(kOwner, 150, 250, {"file.vmd"}) == AcceptResult::NoTarget);
    assert(box.SetTarget(kOwner, &kTarget) == GAKU_OK);
    assert(std::strcmp(box.TargetOwner(), kOwner) == 0 && box.Target().left == kTarget.left);
    assert(box.AcceptDetailed(kOtherOwner, 150, 250, {"file.vmd"}) == AcceptResult::OwnerMismatch);
    assert(box.AcceptDetailed(kOwner, 5, 5, {"file.vmd"}) == AcceptResult::OutsideTarget);
    assert(box.AcceptDetailed(kOwner, 150, 250, {std::string(GAKU_FILE_DROP_MAX_BYTES, 'x')}) == AcceptResult::PayloadTooLarge);
    assert(Peek(box).fileCount == 0); // rejected diagnostics never enqueue a batch
    assert(box.AcceptDetailed(kOwner, 150, 250, {"first.vmd"}) == AcceptResult::Accepted);
    assert(box.AcceptDetailed(kOwner, 150, 250, {"second.vmd"}) == AcceptResult::BatchPending);
    GakuFileDropResult result{};
    char output[32]{};
    assert(box.Poll(kOwner, sizeof(result), &result, sizeof(output), output) == GAKU_OK);
    assert(result.fileCount == 1 && std::strcmp(output, "first.vmd") == 0);
    assert(box.Accept(kOwner, 150, 250, {"next.vmd"})); // existing bool API keeps its semantics
}
void DragHoverState() {
    Mailbox box;
    GakuFileDragState state{};
    std::array<char, 64> output;
    // No target registered: the host reports an empty drag to every owner.
    assert(box.QueryDrag(kOwner, sizeof(state), &state, 0, nullptr) == GAKU_OK);
    assert(state.abiVersion == GAKU_FILE_DRAG_VERSION && state.structSize == sizeof(state));
    assert(state.flags == 0 && state.fileCount == 0 && state.x < 0.0f && state.y < 0.0f);
    assert(box.SetTarget(kOwner, &kTarget) == GAKU_OK);

    const std::vector<std::string> files{"a.vmd", "b.wav"};
    const auto packed = Pack(files);
    box.BeginDrag(packed, static_cast<uint32_t>(files.size()));
    box.UpdateDrag(150.0f, 250.0f); // inside kTarget
    assert(box.QueryDrag(kOwner, sizeof(state), &state, 0, nullptr) == GAKU_OK);
    assert((state.flags & GAKU_FILE_DRAG_ACTIVE) != 0);
    assert((state.flags & GAKU_FILE_DRAG_INSIDE_TARGET) != 0);
    assert(state.generation == 1 && state.fileCount == files.size());
    assert(state.requiredBytes == packed.size());
    assert(state.x == 150.0f && state.y == 250.0f);
    // Hover dragging across the window edge only clears the inside bit.
    box.UpdateDrag(150.0f, 900.0f);
    assert(box.QueryDrag(kOwner, sizeof(state), &state, 0, nullptr) == GAKU_OK);
    assert((state.flags & GAKU_FILE_DRAG_ACTIVE) != 0 && (state.flags & GAKU_FILE_DRAG_INSIDE_TARGET) == 0);
    // Another owner never observes this panel's drag.
    assert(box.QueryDrag(kOtherOwner, sizeof(state), &state, 0, nullptr) == GAKU_OK);
    assert(state.flags == 0 && state.fileCount == 0 && state.requiredBytes == 0);
    // Peeking is non-consuming and never fabricates a drop batch.
    assert(box.QueryDrag(kOwner, sizeof(state), &state,
        static_cast<uint32_t>(output.size()), output.data()) == GAKU_OK);
    assert(std::memcmp(output.data(), packed.data(), packed.size()) == 0);
    assert(Peek(box).fileCount == 0);
    // Short buffers report the requirement and write nothing.
    output.fill('\x55');
    assert(box.QueryDrag(kOwner, sizeof(state), &state, 3, output.data()) == GAKU_E_SIZE);
    assert(state.requiredBytes == packed.size());
    assert(std::all_of(output.begin(), output.end(), [](char c) { return c == '\x55'; }));
    // Every new OS drag gets a new generation so observers can re-validate once.
    box.BeginDrag(packed, static_cast<uint32_t>(files.size()));
    assert(box.QueryDrag(kOwner, sizeof(state), &state, 0, nullptr) == GAKU_OK && state.generation == 2);
    box.EndDrag();
    assert(box.QueryDrag(kOwner, sizeof(state), &state, 0, nullptr) == GAKU_OK);
    assert(state.flags == 0 && state.fileCount == 0);
    // Clear() drops the drag with the batch: a hidden window keeps no hover.
    box.BeginDrag(packed, static_cast<uint32_t>(files.size()));
    box.Clear();
    assert(box.QueryDrag(kOwner, sizeof(state), &state, 0, nullptr) == GAKU_OK && state.flags == 0);
    // A host that can never receive an OLE drag reports the blocked capability
    // even with nothing in flight, so a plugin can explain the dark preview
    // instead of waiting for hover events UIPI will never deliver.
    box.SetHostBlocked(true, true);
    assert(box.QueryDrag(kOwner, sizeof(state), &state, 0, nullptr) == GAKU_OK);
    assert((state.flags & (GAKU_FILE_DRAG_HOST_BLOCKED | GAKU_FILE_DRAG_HOST_ELEVATED)) ==
        (GAKU_FILE_DRAG_HOST_BLOCKED | GAKU_FILE_DRAG_HOST_ELEVATED) && state.fileCount == 0);
    assert(box.SetTarget(kOwner, &kTarget) == GAKU_OK);
    box.BeginDrag(packed, static_cast<uint32_t>(files.size()));
    box.UpdateDrag(150.0f, 250.0f);
    assert(box.QueryDrag(kOwner, sizeof(state), &state, 0, nullptr) == GAKU_OK);
    assert((state.flags & (GAKU_FILE_DRAG_ACTIVE | GAKU_FILE_DRAG_INSIDE_TARGET |
        GAKU_FILE_DRAG_HOST_BLOCKED | GAKU_FILE_DRAG_HOST_ELEVATED)) ==
        (GAKU_FILE_DRAG_ACTIVE | GAKU_FILE_DRAG_INSIDE_TARGET |
        GAKU_FILE_DRAG_HOST_BLOCKED | GAKU_FILE_DRAG_HOST_ELEVATED));
    box.EndDrag();
    // Blocked without elevation: the host just has no hover transport, and a
    // plugin must not blame administrator rights for it.
    box.SetHostBlocked(true, false);
    assert(box.QueryDrag(kOwner, sizeof(state), &state, 0, nullptr) == GAKU_OK);
    assert((state.flags & GAKU_FILE_DRAG_HOST_BLOCKED) != 0);
    assert((state.flags & GAKU_FILE_DRAG_HOST_ELEVATED) == 0);
    box.SetHostBlocked(false, false);
    assert(box.QueryDrag(kOwner, sizeof(state), &state, 0, nullptr) == GAKU_OK && state.flags == 0);
    // Argument contract matches poll().
    assert(box.QueryDrag(nullptr, sizeof(state), &state, 0, nullptr) == GAKU_E_INVALID_ARGUMENT);
    assert(box.QueryDrag(kOwner, sizeof(state) - 1, &state, 0, nullptr) == GAKU_E_SIZE);
    assert(box.QueryDrag(kOwner, sizeof(state), nullptr, 0, nullptr) == GAKU_E_INVALID_ARGUMENT);
    assert(box.QueryDrag(kOwner, sizeof(state), &state, 1, nullptr) == GAKU_E_INVALID_ARGUMENT);
    assert(box.QueryDrag(kOwner, sizeof(state), &state, 0, output.data()) == GAKU_E_INVALID_ARGUMENT);
}
}

int main() {
    UnicodeBatchAndExactlyOnce();
    OwnerIsolationAndQueueRetention();
    RetryAndInvalidArguments();
    HitBounds();
    TargetContract();
    FrameRenewalAndExpiry();
    DisableAndClear();
    MalformedAndOverflow();
    DiagnosticReasonsPreserveBatch();
    DragHoverState();
    std::puts("PASS: 10 desktop file-drop broker contract groups, including rejection diagnostics and live drag state (offline only).");
    return 0;
}
