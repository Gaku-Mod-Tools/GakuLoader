#include "DesktopAppearance.hpp"
#include <array>
#include <cstdio>
#include <stdexcept>

using namespace GakuLoader::DesktopAppearance;

namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
float Linear(float value) {
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}
float Luminance(ImVec4 color) {
    return 0.2126f * Linear(color.x) + 0.7152f * Linear(color.y) + 0.0722f * Linear(color.z);
}
float Contrast(ImVec4 a, ImVec4 b) {
    const float x = Luminance(a), y = Luminance(b);
    return ((std::max)(x, y) + 0.05f) / ((std::min)(x, y) + 0.05f);
}
void TestApplePalette(GakuUiTheme theme) {
    ApplyAppearance(theme, 1.0f, 1.0f);
    const auto& style = ImGui::GetStyle();
    for (const auto& color : style.Colors) {
        for (float channel : {color.x, color.y, color.z, color.w})
            Check(std::isfinite(channel) && channel >= 0.0f && channel <= 1.0f, "invalid palette channel");
    }
    for (int surface : {ImGuiCol_WindowBg, ImGuiCol_ChildBg, ImGuiCol_PopupBg,
         ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive,
         ImGuiCol_Button, ImGuiCol_ButtonHovered, ImGuiCol_ButtonActive,
         ImGuiCol_Header, ImGuiCol_HeaderHovered, ImGuiCol_HeaderActive,
         ImGuiCol_TableHeaderBg}) {
        Check(Contrast(style.Colors[ImGuiCol_Text], style.Colors[surface]) >= 4.5f,
            "Apple normal text contrast below 4.5:1");
    }
    for (int surface : {ImGuiCol_WindowBg, ImGuiCol_ChildBg, ImGuiCol_PopupBg})
        Check(Contrast(style.Colors[ImGuiCol_TextDisabled], style.Colors[surface]) >= 4.5f,
            "Apple secondary text contrast below 4.5:1");

    const float baselineRounding = style.FrameRounding;
    const float baselinePadding = style.WindowPadding.x;
    ApplyAppearance(theme, 1.25f, 1.5f);
    // ImGui snaps scaled geometry to whole pixels.
    Check(std::abs(ImGui::GetStyle().FrameRounding - baselineRounding * 1.875f) < 1.0f &&
          ImGui::GetIO().FontGlobalScale == 1.0f,
        "Apple geometry must follow UI/DPI while rasterized fonts stay at native scale");
    ApplyAppearance(GAKU_UI_THEME_DARK, 2.0f, 1.0f);
    ApplyAppearance(theme, 1.0f, 1.0f);
    Check(std::abs(ImGui::GetStyle().FrameRounding - baselineRounding) < 0.01f &&
          std::abs(ImGui::GetStyle().WindowPadding.x - baselinePadding) < 0.01f &&
          ImGui::GetIO().FontGlobalScale == 1.0f, "theme switching accumulated scale");
}
void TestPersistence() {
    wchar_t directory[MAX_PATH]{}, filename[MAX_PATH]{};
    Check(GetTempPathW(MAX_PATH, directory) > 0, "temporary directory");
    Check(GetTempFileNameW(directory, L"gka", 0, filename) != 0, "temporary config");
    const std::filesystem::path path(filename);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{path};
    for (GakuUiTheme theme : {GAKU_UI_THEME_DARK, GAKU_UI_THEME_SAPPHIRE,
         GAKU_UI_THEME_PAPER_AND_INK, GAKU_UI_THEME_APPLE_LIGHT, GAKU_UI_THEME_APPLE_DARK}) {
        SaveAppearanceSettings(path, theme, 1.25f);
        GakuUiTheme restored = GAKU_UI_THEME_DARK;
        float scale = 1.0f;
        LoadAppearanceSettings(path, restored, scale);
        Check(restored == theme && scale == 1.25f, "appearance config round-trip");
    }
    WritePrivateProfileStringW(L"appearance", L"theme", L"apple light", path.c_str());
    GakuUiTheme restored = GAKU_UI_THEME_DARK;
    float scale = 1.0f;
    LoadAppearanceSettings(path, restored, scale);
    Check(restored == GAKU_UI_THEME_APPLE_LIGHT, "case-insensitive friendly theme name");
}
}

int main() {
    static_assert(GAKU_UI_THEME_DARK == 0 && GAKU_UI_THEME_SAPPHIRE == 1 && GAKU_UI_THEME_PAPER_AND_INK == 2);
    static_assert(sizeof(GakuUiAppearanceV1) == 16 && sizeof(GakuAppearanceService) == 16);
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    try {
        TestApplePalette(GAKU_UI_THEME_APPLE_LIGHT);
        TestApplePalette(GAKU_UI_THEME_APPLE_DARK);
        TestPersistence();
        ImGui::DestroyContext();
        std::puts("PASS: Apple palette contrast, DPI switching, config persistence and ABI layout");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        ImGui::DestroyContext();
        return 1;
    }
}
