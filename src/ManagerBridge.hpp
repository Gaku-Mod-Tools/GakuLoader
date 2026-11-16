// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

#pragma once
// Typed, transport-agnostic request/response + event bridge for the Manager
// WebView2 frontend. The core owns JSON framing, request correlation, structured
// errors, event sequencing and lifecycle invalidation. Backend handlers are
// registered by the desktop UI (production) or by tests; the core never reaches
// into plugin state or the filesystem on its own.
//
// Wire protocol (UTF-8 JSON over WebView2 web messages):
//   request  {v:1, id:<u64>, type:"request", method:<string>, params:<object>}
//   response {v:1, id:<u64>, type:"response", ok:<bool>, result?:<any>,
//             error?:{code:<string>, message:<string>, details?:<any>}}
//   event    {v:1, type:"event", name:<string>, payload:<any>, seq:<u64>, sid:<u64>}
//
// All calls run on the desktop UI thread. Handlers are synchronous; the native
// backend stays authoritative and pushes change events instead of being polled.

#include <nlohmann/json.hpp>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

namespace GakuLoader::ManagerBridge {

constexpr uint32_t kProtocolVersion = 1;

struct Error {
    std::string code;
    std::string message;
    nlohmann::json details;
};

struct Result {
    bool ok{true};
    nlohmann::json data;
    Error error;
    static Result Ok(nlohmann::json value) { Result r; r.ok = true; r.data = std::move(value); return r; }
    static Result Fail(std::string code, std::string message, nlohmann::json details = nullptr) {
        Result r; r.ok = false; r.error.code = std::move(code); r.error.message = std::move(message);
        r.error.details = std::move(details); return r;
    }
};

using Handler = std::function<Result(const nlohmann::json& params)>;
using EventSink = std::function<void(const std::string& json)>;
using RawHandler = std::function<std::string(const std::string& request)>;

// Validate a request envelope and forward the original JSON to a provider-owned
// handler. The returned response must carry the same id and type="response".
// Never throws; malformed provider responses become structured internal errors.
std::string ForwardRequest(const std::string& message, const RawHandler& handler);

class Bridge {
public:
    Bridge();
    // Register (or replace) a backend method. Names are a fixed allow-list; there
    // is no generic command or filesystem passthrough.
    void Register(const std::string& method, Handler handler);
    void SetSink(EventSink sink);
    // Process one incoming web message. Returns the response JSON for a request,
    // or an empty string for anything else. Never throws.
    std::string HandleMessage(const std::string& message);
    // Push an unsolicited event to the frontend (no-op when no sink or closed).
    void EmitEvent(const std::string& name, const nlohmann::json& payload);
    // Begin a fresh document session: new session id, seq reset, reopened. Stale
    // responses/events from a previous document carry an old sid and are dropped
    // by the frontend.
    void Reset();
    // Close the bridge (backend/plugin shutdown). Pending frontend requests are
    // invalidated by the emitted event; further requests are rejected.
    void Invalidate(const std::string& reasonCode);
    bool closed() const { return closed_; }
    uint64_t sessionId() const { return sessionId_; }
    uint64_t eventSeq() const { return seq_; }
    bool HasMethod(const std::string& method) const;

private:
    std::string Dispatch(uint64_t id, const std::string& method, const nlohmann::json& params);
    std::string MakeResponse(uint64_t id, const Result& result) const;
    std::unordered_map<std::string, Handler> handlers_;
    EventSink sink_;
    uint64_t sessionId_{0};
    uint64_t seq_{0};
    bool closed_{false};
};

} // namespace GakuLoader::ManagerBridge
