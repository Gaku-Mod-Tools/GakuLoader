// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

#pragma once
#include <windows.h>
#include <atomic>
#include <string>

namespace GakuLoader::LogLevel {

// Host-side severity. This deliberately mirrors nothing in GakuPlugin.h: the
// plugin ABI keeps its three-value GakuLogLevel so an old plugin keeps working
// against a new loader, and Debug never crosses the ABI at all. A plugin that
// wants Debug strips it at compile time and sends nothing.
enum class Level : int { Debug = 0, Info = 1, Warn = 2, Error = 3 };

inline std::atomic<int> g_threshold{static_cast<int>(Level::Info)};

// Default is Info: every message the loader emitted before this layer existed
// still prints. A typo or a missing key must never silence a build, so Parse
// falls back instead of tightening.
inline Level Threshold() {
    return static_cast<Level>(g_threshold.load(std::memory_order_relaxed));
}
inline void SetThreshold(Level level) {
    g_threshold.store(static_cast<int>(level), std::memory_order_relaxed);
}
inline bool Enabled(Level level) {
    return static_cast<int>(level) >= g_threshold.load(std::memory_order_relaxed);
}

// "debug" / "info" / "warn" / "error", case-insensitive, trimmed by the caller.
inline Level Parse(const wchar_t* value, Level fallback) {
    if (!value || !value[0]) return fallback;
    std::wstring text(value);
    const auto first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return fallback;
    const auto last = text.find_last_not_of(L" \t\r\n");
    text = text.substr(first, last - first + 1);
    if (lstrcmpiW(text.c_str(), L"debug") == 0) return Level::Debug;
    if (lstrcmpiW(text.c_str(), L"info") == 0) return Level::Info;
    if (lstrcmpiW(text.c_str(), L"warn") == 0 || lstrcmpiW(text.c_str(), L"warning") == 0) return Level::Warn;
    if (lstrcmpiW(text.c_str(), L"error") == 0) return Level::Error;
    return fallback;
}

inline const wchar_t* Name(Level level) {
    switch (level) {
    case Level::Debug: return L"debug";
    case Level::Info: return L"info";
    case Level::Warn: return L"warn";
    case Level::Error: return L"error";
    }
    return L"info";
}

// Fixed five-character token for the record prefix. The padding is deliberate:
// a column that shifts by one character between INFO and ERROR is unreadable
// when a 30-line burst goes past in a console.
inline const char* Token(Level level) {
    switch (level) {
    case Level::Debug: return "DEBUG";
    case Level::Info: return "INFO ";
    case Level::Warn: return "WARN ";
    case Level::Error: return "ERROR";
    }
    return "INFO ";
}

} // namespace GakuLoader::LogLevel
