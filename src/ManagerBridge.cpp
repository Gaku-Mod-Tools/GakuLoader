// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

#include "ManagerBridge.hpp"
#include <exception>
#include <utility>

namespace GakuLoader::ManagerBridge {
namespace {
using nlohmann::json;

json ErrorJson(const Error& error) {
    json out{{"code", error.code}, {"message", error.message}};
    if (!error.details.is_null()) out["details"] = error.details;
    return out;
}

std::string ErrorResponse(uint64_t id, const char* code, const char* message) {
    json out{{"v", kProtocolVersion}, {"id", id}, {"type", "response"}, {"ok", false},
             {"error", {{"code", code}, {"message", message}}}};
    return out.dump();
}
} // namespace

Bridge::Bridge() { Reset(); }

void Bridge::Register(const std::string& method, Handler handler) {
    handlers_[method] = std::move(handler);
}

bool Bridge::HasMethod(const std::string& method) const {
    return handlers_.find(method) != handlers_.end();
}

void Bridge::SetSink(EventSink sink) { sink_ = std::move(sink); }

void Bridge::Reset() {
    ++sessionId_;
    seq_ = 0;
    closed_ = false;
    // Core handshake methods. The backend registers everything else.
    Register("bridge.hello", [this](const json&) {
        return Result::Ok(json{{"protocol", kProtocolVersion}, {"sessionId", sessionId_}});
    });
    Register("bridge.ping", [](const json&) { return Result::Ok(json{{"pong", true}}); });
}

void Bridge::Invalidate(const std::string& reasonCode) {
    if (closed_) return;
    EmitEvent("bridge.closed", json{{"reason", reasonCode}});
    closed_ = true;
}

std::string Bridge::MakeResponse(uint64_t id, const Result& result) const {
    json out{{"v", kProtocolVersion}, {"id", id}, {"type", "response"}, {"ok", result.ok}};
    if (result.ok) out["result"] = result.data.is_null() ? json::object() : result.data;
    else out["error"] = ErrorJson(result.error);
    return out.dump();
}

std::string Bridge::Dispatch(uint64_t id, const std::string& method, const json& params) {
    if (closed_) return MakeResponse(id, Result::Fail("shutting_down", "The loader bridge is closed."));
    const auto found = handlers_.find(method);
    if (found == handlers_.end())
        return MakeResponse(id, Result::Fail("unknown_method", "Unknown bridge method: " + method));
    try {
        return MakeResponse(id, found->second(params.is_null() ? json::object() : params));
    } catch (const std::exception& exception) {
        return MakeResponse(id, Result::Fail("internal_error", exception.what()));
    } catch (...) {
        return MakeResponse(id, Result::Fail("internal_error", "Unknown handler exception."));
    }
}

std::string Bridge::HandleMessage(const std::string& message) {
    json parsed;
    try {
        parsed = json::parse(message);
    } catch (const std::exception&) {
        return MakeResponse(0, Result::Fail("invalid_json", "Message is not valid JSON."));
    }
    if (!parsed.is_object()) return MakeResponse(0, Result::Fail("invalid_request", "Message must be an object."));
    if (!parsed.contains("type") || !parsed["type"].is_string())
        return MakeResponse(0, Result::Fail("invalid_request", "Missing message type."));
    const auto type = parsed["type"].get<std::string>();
    if (type != "request") return ""; // frontend does not send unsolicited events
    uint64_t id = 0;
    if (!parsed.contains("id"))
        return MakeResponse(0, Result::Fail("invalid_request", "Missing request id."));
    if (parsed["id"].is_number_unsigned()) id = parsed["id"].get<uint64_t>();
    else if (parsed["id"].is_number_integer()) {
        const auto signedId = parsed["id"].get<int64_t>();
        if (signedId < 0) return MakeResponse(0, Result::Fail("invalid_request", "Request id must not be negative."));
        id = static_cast<uint64_t>(signedId);
    } else {
        return MakeResponse(0, Result::Fail("invalid_request", "Request id must be an integer."));
    }
    if (!parsed.contains("v") || !parsed["v"].is_number_integer())
        return MakeResponse(id, Result::Fail("invalid_request", "Missing protocol version."));
    const auto version = parsed["v"].is_number_unsigned() ? parsed["v"].get<uint64_t>()
        : static_cast<uint64_t>(parsed["v"].get<int64_t>());
    if (version != kProtocolVersion)
        return MakeResponse(id, Result::Fail("unsupported_protocol", "Unsupported bridge protocol version."));
    if (!parsed.contains("method") || !parsed["method"].is_string())
        return MakeResponse(id, Result::Fail("invalid_request", "Missing method."));
    const auto method = parsed["method"].get<std::string>();
    if (method.empty()) return MakeResponse(id, Result::Fail("invalid_request", "Missing method."));
    json params = json::object();
    if (parsed.contains("params")) {
        if (!parsed["params"].is_object())
            return MakeResponse(id, Result::Fail("invalid_request", "Request params must be an object."));
        params = parsed["params"];
    }
    return Dispatch(id, method, params);
}

void Bridge::EmitEvent(const std::string& name, const json& payload) {
    if (!sink_ || closed_) return;
    json out{{"v", kProtocolVersion}, {"type", "event"}, {"name", name},
             {"payload", payload.is_null() ? json::object() : payload},
             {"seq", ++seq_}, {"sid", sessionId_}};
    try {
        sink_(out.dump());
    } catch (...) {
    }
}

std::string ForwardRequest(const std::string& message, const RawHandler& handler) {
    json parsed;
    try {
        parsed = json::parse(message);
    } catch (const std::exception&) {
        return ErrorResponse(0, "invalid_json", "Message is not valid JSON.");
    }
    if (!parsed.is_object()) return ErrorResponse(0, "invalid_request", "Message must be an object.");
    if (!parsed.contains("type") || !parsed["type"].is_string() || parsed["type"].get<std::string>() != "request")
        return ErrorResponse(0, "invalid_request", "Message must be a request.");
    uint64_t id = 0;
    if (!parsed.contains("id")) return ErrorResponse(0, "invalid_request", "Missing request id.");
    if (parsed["id"].is_number_unsigned()) id = parsed["id"].get<uint64_t>();
    else if (parsed["id"].is_number_integer()) {
        const auto signedId = parsed["id"].get<int64_t>();
        if (signedId < 0) return ErrorResponse(0, "invalid_request", "Request id must not be negative.");
        id = static_cast<uint64_t>(signedId);
    } else return ErrorResponse(0, "invalid_request", "Request id must be an integer.");
    if (!parsed.contains("v") || !parsed["v"].is_number_integer())
        return ErrorResponse(id, "invalid_request", "Missing protocol version.");
    const auto version = parsed["v"].is_number_unsigned() ? parsed["v"].get<uint64_t>()
        : static_cast<uint64_t>(parsed["v"].get<int64_t>());
    if (version != kProtocolVersion)
        return ErrorResponse(id, "unsupported_protocol", "Unsupported bridge protocol version.");
    if (!parsed.contains("method") || !parsed["method"].is_string() || parsed["method"].get<std::string>().empty())
        return ErrorResponse(id, "invalid_request", "Missing method.");
    if (parsed.contains("params") && !parsed["params"].is_object())
        return ErrorResponse(id, "invalid_request", "Request params must be an object.");
    if (!handler) return ErrorResponse(id, "internal_error", "No web panel handler is bound.");
    std::string response;
    try {
        response = handler(message);
    } catch (const std::exception& exception) {
        return ErrorResponse(id, "internal_error", exception.what());
    } catch (...) {
        return ErrorResponse(id, "internal_error", "Unknown web panel handler exception.");
    }
    if (response.empty()) return ErrorResponse(id, "internal_error", "Web panel handler returned no response.");
    try {
        const auto result = json::parse(response);
        uint64_t responseId = 0;
        bool validId = false;
        if (result.contains("id")) {
            if (result["id"].is_number_unsigned()) { responseId = result["id"].get<uint64_t>(); validId = true; }
            else if (result["id"].is_number_integer()) {
                const auto signedId = result["id"].get<int64_t>();
                if (signedId >= 0) { responseId = static_cast<uint64_t>(signedId); validId = true; }
            }
        }
        if (!result.is_object() || !result.contains("v") || !result["v"].is_number_integer() ||
            result["v"].get<uint64_t>() != kProtocolVersion ||
            !validId || responseId != id ||
            !result.contains("type") || !result["type"].is_string() || result["type"].get<std::string>() != "response" ||
            !result.contains("ok") || !result["ok"].is_boolean())
            return ErrorResponse(id, "internal_error", "Web panel handler returned an invalid response envelope.");
        return response;
    } catch (const std::exception&) {
        return ErrorResponse(id, "internal_error", "Web panel handler returned malformed JSON.");
    }
}

} // namespace GakuLoader::ManagerBridge
