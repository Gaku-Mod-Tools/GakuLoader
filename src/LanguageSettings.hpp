// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

#pragma once
#include "GakuLanguage.h"
#include "HostConfig.hpp"
#include <atomic>

namespace GakuLoader::LanguageSettings {
inline std::atomic_uint32_t g_language{GAKU_LANGUAGE_ENGLISH};
inline const wchar_t* const kTags[] = {L"en", L"zh-Hans", L"zh-Hant", L"ja"};
inline const char* const kNames[] = {"English", "简体中文", "繁體中文", "日本語"};
inline uint32_t Current() { return g_language.load(std::memory_order_acquire); }
inline uint32_t SystemDefault() {
    const auto language = GetUserDefaultUILanguage();
    if (PRIMARYLANGID(language) == LANG_CHINESE)
        return SUBLANGID(language) == SUBLANG_CHINESE_TRADITIONAL ||
            SUBLANGID(language) == SUBLANG_CHINESE_HONGKONG || SUBLANGID(language) == SUBLANG_CHINESE_MACAU
            ? GAKU_LANGUAGE_TRADITIONAL_CHINESE : GAKU_LANGUAGE_SIMPLIFIED_CHINESE;
    return PRIMARYLANGID(language) == LANG_JAPANESE ? GAKU_LANGUAGE_JAPANESE : GAKU_LANGUAGE_ENGLISH;
}
inline void Load(const std::filesystem::path& path) {
    const auto tag = ReadConfigValue(path, L"appearance", L"language");
    uint32_t language = SystemDefault();
    for (uint32_t i = 0; i < 4; ++i) if (lstrcmpiW(tag.c_str(), kTags[i]) == 0) language = i;
    g_language.store(language, std::memory_order_release);
}
inline bool Save(const std::filesystem::path& path, uint32_t language) {
    if (language >= 4 || path.empty() ||
        !WritePrivateProfileStringW(L"appearance", L"language", kTags[language], path.c_str())) return false;
    g_language.store(language, std::memory_order_release);
    return true;
}
inline GakuStatus Get(uint32_t size, GakuUiLanguageV1* output) {
    if (!output) return GAKU_E_INVALID_ARGUMENT;
    if (size < sizeof(*output)) return GAKU_E_SIZE;
    *output = {GAKU_LANGUAGE_VERSION, sizeof(*output), Current(), 0};
    return GAKU_OK;
}
inline const GakuLanguageServiceV1 kService{GAKU_LANGUAGE_VERSION, sizeof(GakuLanguageServiceV1), Get};
}
