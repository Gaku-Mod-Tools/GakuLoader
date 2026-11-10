// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <cwctype>

namespace GakuLoader {
// Unknown/missing values retain the caller's default. In particular, a typo
// cannot silently disable a plugin whose default is enabled.
inline bool ParseConfigFlag(const wchar_t* input, bool fallback) {
    if (!input) return fallback;
    std::wstring value(input);
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return fallback;
    const auto last = value.find_last_not_of(L" \t\r\n");
    value = value.substr(first, last - first + 1);
    if (lstrcmpiW(value.c_str(), L"true") == 0 || value == L"1" || lstrcmpiW(value.c_str(), L"yes") == 0) return true;
    if (lstrcmpiW(value.c_str(), L"false") == 0 || value == L"0" || lstrcmpiW(value.c_str(), L"no") == 0) return false;
    return fallback;
}
inline std::wstring ReadConfigValue(const std::filesystem::path& config, const wchar_t* section,
    const wchar_t* key) {
    wchar_t value[1024]{};
    GetPrivateProfileStringW(section, key, L"", value, 1024, config.c_str());
    return value;
}
inline bool ReadConfigFlag(const std::filesystem::path& config, const wchar_t* section,
    const wchar_t* key, bool fallback) {
    wchar_t value[64]{};
    GetPrivateProfileStringW(section, key, L"", value, 64, config.c_str());
    return ParseConfigFlag(value, fallback);
}
}
