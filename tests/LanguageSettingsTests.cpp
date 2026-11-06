#include "LanguageSettings.hpp"
#include "ManagerStrings.hpp"
#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {
void CheckValue(uint32_t expected) {
    struct Output { GakuUiLanguageV1 language; uint64_t canary; } output;
    std::memset(&output, 0x7b, sizeof(output));
    const auto sentinel = output;
    const auto& service = GakuLoader::LanguageSettings::kService;
    assert(service.abiVersion == GAKU_LANGUAGE_VERSION && service.structSize == sizeof(service) && service.getLanguage);
    assert(service.getLanguage(sizeof(output.language) - 1, &output.language) == GAKU_E_SIZE);
    assert(std::memcmp(&output, &sentinel, sizeof(output)) == 0);
    assert(service.getLanguage(0, &output.language) == GAKU_E_SIZE);
    assert(std::memcmp(&output, &sentinel, sizeof(output)) == 0);
    assert(service.getLanguage(sizeof(output), &output.language) == GAKU_OK);
    assert(output.canary == sentinel.canary);
    assert(output.language.abiVersion == GAKU_LANGUAGE_VERSION && output.language.structSize == sizeof(output.language));
    assert(output.language.language == expected && output.language.reserved == 0);
    assert(service.getLanguage(sizeof(output.language), nullptr) == GAKU_E_INVALID_ARGUMENT);
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) { std::fputs("usage: language-settings-tests <synthetic absolute root>\n", stderr); return 2; }
    const std::filesystem::path root(argv[1]);
    assert(root.is_absolute());
    std::filesystem::create_directories(root);
    const auto config = root / L"gakuloader.ini";
    const auto missing = root / L"missing.ini";
    using namespace GakuLoader;
    LanguageSettings::Load(missing);
    const uint32_t fallback = LanguageSettings::SystemDefault();
    assert(fallback < 4);
    CheckValue(fallback);
    assert(!std::filesystem::exists(missing)); // reads and service queries do not write

    // Appearance saves preserve other configurable host settings and unrelated
    // plugin keys in the same ini, including a user's explicit false values.
    assert(WritePrivateProfileStringW(L"host", L"console", L"true", config.c_str()));
    assert(WritePrivateProfileStringW(L"diagnostics", L"trace", L"false", config.c_str()));
    assert(WritePrivateProfileStringW(L"plugins", L"optional.dll", L"false", config.c_str()));
    using namespace GakuLoader::ManagerStrings;
    assert(FormatVersion("0.1.0") == "v0.1.0");
    assert(FormatVersion("v3.6.2") == "v3.6.2");
    assert(FormatVersion("") == "" && FormatVersion("unknown") == "unknown");
    const std::array<std::pair<uint32_t, const wchar_t*>, 4> choices{{
        {GAKU_LANGUAGE_ENGLISH, L"en"}, {GAKU_LANGUAGE_SIMPLIFIED_CHINESE, L"zh-Hans"},
        {GAKU_LANGUAGE_TRADITIONAL_CHINESE, L"zh-Hant"}, {GAKU_LANGUAGE_JAPANESE, L"ja"}}};
    for (const auto& [language, tag] : choices) {
        assert(LanguageSettings::Save(config, language));
        CheckValue(language); // a previously obtained service sees changes immediately
        if (language == GAKU_LANGUAGE_SIMPLIFIED_CHINESE) {
            assert(std::strcmp(Text(Key::Activated), "正在激活") == 0);
            assert(std::strcmp(Text(Key::Ready), "已就绪") == 0);
        }
        assert(ReadConfigValue(config, L"appearance", L"language") == tag);
        assert(ReadConfigFlag(config, L"host", L"console", false));
        assert(!ReadConfigFlag(config, L"diagnostics", L"trace", true));
        assert(!ReadConfigFlag(config, L"plugins", L"optional.dll", true));
        LanguageSettings::Load(missing); // replace current choice before loading disk
        LanguageSettings::Load(config);
        CheckValue(language);
    }
    assert(WritePrivateProfileStringW(L"appearance", L"language", L"ZH-HANT", config.c_str()));
    LanguageSettings::Load(config);
    CheckValue(GAKU_LANGUAGE_TRADITIONAL_CHINESE);
    assert(WritePrivateProfileStringW(L"appearance", L"language", L"unknown-locale", config.c_str()));
    LanguageSettings::Load(config);
    CheckValue(fallback);
    assert(WritePrivateProfileStringW(L"appearance", L"language", L"", config.c_str()));
    LanguageSettings::Load(config);
    CheckValue(fallback);

    assert(LanguageSettings::Save(config, GAKU_LANGUAGE_JAPANESE));
    assert(!LanguageSettings::Save(config, 4));
    assert(!LanguageSettings::Save(config, UINT32_MAX));
    assert(!LanguageSettings::Save({}, GAKU_LANGUAGE_ENGLISH));
    CheckValue(GAKU_LANGUAGE_JAPANESE);
    assert(ReadConfigValue(config, L"appearance", L"language") == L"ja");
    const auto blocked = root / L"directory-instead-of-ini";
    std::filesystem::create_directories(blocked);
    assert(!LanguageSettings::Save(blocked, GAKU_LANGUAGE_ENGLISH));
    CheckValue(GAKU_LANGUAGE_JAPANESE); // failed persistence cannot change shared preference
    assert(std::filesystem::is_directory(blocked));

    // A plugin may poll this service off-thread while the desktop changes the
    // preference. It always receives a complete value in the documented range.
    std::atomic_bool stop{false};
    std::atomic_uint32_t samples{0};
    std::thread reader([&] {
        while (!stop.load(std::memory_order_acquire)) {
            GakuUiLanguageV1 value{};
            assert(LanguageSettings::kService.getLanguage(sizeof(value), &value) == GAKU_OK);
            assert(value.language < 4 && value.reserved == 0 && value.structSize == sizeof(value));
            samples.fetch_add(1, std::memory_order_release);
        }
    });
    while (!samples.load(std::memory_order_acquire)) std::this_thread::yield();
    for (const auto& [language, tag] : choices) {
        (void)tag;
        assert(LanguageSettings::Save(config, language));
    }
    stop.store(true, std::memory_order_release);
    reader.join();
    assert(samples.load() > 0);
    CheckValue(GAKU_LANGUAGE_JAPANESE);
    std::puts("PASS: four built-in UI languages; persistent/live service preference; system fallback; "
        "case-insensitive tags; short output canaries; invalid/save-failure isolation; other host settings preserved; concurrent polling");
    return 0;
}
