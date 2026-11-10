// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

#pragma once
#include "GakuPaths.h"
#include <array>
#include <cstring>
#include <filesystem>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>

namespace GakuLoader::PluginPaths {

inline const wchar_t* Category(GakuPathKind kind) noexcept {
    switch (kind) {
    case GAKU_PATH_USER_DATA: return L"UserData";
    case GAKU_PATH_CONFIGS: return L"Configs";
    case GAKU_PATH_PLUGIN_DATA: return L"PluginData";
    case GAKU_PATH_CACHE: return L"Cache";
    case GAKU_PATH_LOGS: return L"Logs";
    case GAKU_PATH_TEMP: return L"Temp";
    default: return nullptr;
    }
}

inline bool ValidNamespace(std::string_view id) noexcept {
    if (id.empty() || id.size() > 64) return false;
    const auto alnum = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
    if (!alnum(id.front()) || id.back() == '.') return false;
    bool segmentEmpty = true;
    for (char c : id) {
        if (c == '.') {
            if (segmentEmpty) return false;
            segmentEmpty = true;
        } else {
            if (!alnum(c) && c != '_' && c != '-') return false;
            segmentEmpty = false;
        }
    }
    const auto stem = id.substr(0, id.find('.'));
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul") return false;
    if (stem.size() == 4 && (stem.substr(0, 3) == "com" || stem.substr(0, 3) == "lpt") &&
        stem[3] >= '1' && stem[3] <= '9') return false;
    // No lowercasing/sanitising: canonical lowercase ids map injectively to
    // Windows names. Foo cannot share foo's directory: Foo has no paths service.
    return true;
}

class State {
public:
    // Called once before publishing the owner. Pure path calculation, no I/O.
    void Configure(const std::filesystem::path& localRoot, std::string_view id) noexcept {
        try {
            std::lock_guard lock(mutex_);
            if (!ValidNamespace(id) || !localRoot.is_absolute()) {
                status_ = GAKU_E_INVALID_ARGUMENT;
                return;
            }
            const std::wstring wideId(id.begin(), id.end()); // validated ASCII
            const auto root = localRoot.lexically_normal();
            for (GakuPathKind kind = GAKU_PATH_USER_DATA; kind <= GAKU_PATH_TEMP; ++kind)
                roots_[kind - 1] = (root / Category(kind) / wideId).wstring();
            status_ = GAKU_OK;
        } catch (...) { status_ = GAKU_E_INTERNAL; }
    }

    GakuStatus Status() const {
        std::lock_guard lock(mutex_);
        return accepting_ ? status_ : GAKU_E_SHUTTING_DOWN;
    }

    GakuStatus GetRoot(GakuPathKind kind, wchar_t* output, uint32_t capacity, uint32_t* required) const {
        if (!required || (!output && capacity) || !Category(kind)) return GAKU_E_INVALID_ARGUMENT;
        std::lock_guard lock(mutex_);
        if (!accepting_) return GAKU_E_SHUTTING_DOWN;
        if (status_ != GAKU_OK) return status_;
        const auto& root = roots_[kind - 1];
        if (root.size() >= (std::numeric_limits<uint32_t>::max)()) return GAKU_E_LIMIT;
        const auto count = static_cast<uint32_t>(root.size() + 1);
        if (capacity < count) { *required = count; return GAKU_E_SIZE; }
        // A null output with enough capacity was already rejected above.
        std::memcpy(output, root.c_str(), static_cast<size_t>(count) * sizeof(wchar_t));
        *required = count;
        return GAKU_OK;
    }

    GakuStatus EnsureRoot(GakuPathKind kind, std::error_code& error) {
        if (!Category(kind)) return GAKU_E_INVALID_ARGUMENT;
        // Retirement takes this same per-owner mutex. An in-flight creation
        // completes before retirement returns; none can start afterwards.
        // Never hold the process-wide service registry lock over disk I/O.
        std::lock_guard lock(mutex_);
        if (!accepting_) return GAKU_E_SHUTTING_DOWN;
        if (status_ != GAKU_OK) return status_;
        error.clear();
        std::filesystem::create_directories(std::filesystem::path(roots_[kind - 1]), error);
        return error ? GAKU_E_INTERNAL : GAKU_OK;
    }

    void Retire() {
        std::lock_guard lock(mutex_);
        accepting_ = false;
    }

private:
    mutable std::mutex mutex_;
    std::array<std::wstring, 6> roots_;
    GakuStatus status_{GAKU_E_UNAVAILABLE};
    bool accepting_{true};
};
}
