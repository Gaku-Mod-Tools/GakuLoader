#pragma once
#include "GakuPlugin.h"
#include "HostConfig.hpp"
#include <imgui.h>
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <string>

namespace GakuLoader::DesktopAppearance {

inline void SetupDarkTheme(ImGuiStyle& style) {
    ImGui::StyleColorsDark(&style);
    ImVec4* colors = style.Colors;

    colors[ImGuiCol_Text]                  = ImVec4(0.92f, 0.93f, 0.95f, 1.00f);
    colors[ImGuiCol_TextDisabled]          = ImVec4(0.50f, 0.53f, 0.58f, 1.00f);
    colors[ImGuiCol_WindowBg]              = ImVec4(0.12f, 0.13f, 0.15f, 1.00f);
    colors[ImGuiCol_ChildBg]               = ImVec4(0.14f, 0.15f, 0.18f, 1.00f);
    colors[ImGuiCol_PopupBg]               = ImVec4(0.13f, 0.14f, 0.17f, 0.98f);
    colors[ImGuiCol_Border]                = ImVec4(0.24f, 0.26f, 0.30f, 0.70f);
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]               = ImVec4(0.18f, 0.20f, 0.24f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.24f, 0.27f, 0.33f, 1.00f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.28f, 0.32f, 0.38f, 1.00f);
    colors[ImGuiCol_TitleBg]               = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);
    colors[ImGuiCol_TitleBgActive]         = ImVec4(0.14f, 0.16f, 0.20f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.10f, 0.11f, 0.13f, 0.75f);
    colors[ImGuiCol_MenuBarBg]             = ImVec4(0.13f, 0.14f, 0.17f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.10f, 0.11f, 0.13f, 0.50f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.26f, 0.29f, 0.35f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.32f, 0.36f, 0.44f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.38f, 0.43f, 0.52f, 1.00f);
    colors[ImGuiCol_CheckMark]             = ImVec4(0.35f, 0.65f, 0.95f, 1.00f);
    colors[ImGuiCol_SliderGrab]            = ImVec4(0.30f, 0.55f, 0.85f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.40f, 0.68f, 0.98f, 1.00f);
    colors[ImGuiCol_Button]                = ImVec4(0.20f, 0.23f, 0.28f, 1.00f);
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.28f, 0.33f, 0.40f, 1.00f);
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.34f, 0.40f, 0.50f, 1.00f);
    colors[ImGuiCol_Header]                = ImVec4(0.20f, 0.24f, 0.30f, 1.00f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.26f, 0.32f, 0.40f, 1.00f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.32f, 0.38f, 0.48f, 1.00f);
    colors[ImGuiCol_Separator]             = ImVec4(0.24f, 0.26f, 0.30f, 0.70f);
    colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.35f, 0.45f, 0.60f, 1.00f);
    colors[ImGuiCol_SeparatorActive]       = ImVec4(0.45f, 0.58f, 0.75f, 1.00f);
    colors[ImGuiCol_ResizeGrip]            = ImVec4(0.25f, 0.30f, 0.38f, 0.50f);
    colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.35f, 0.42f, 0.55f, 0.70f);
    colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.45f, 0.55f, 0.70f, 0.90f);
    colors[ImGuiCol_Tab]                   = ImVec4(0.16f, 0.18f, 0.22f, 1.00f);
    colors[ImGuiCol_TabHovered]            = ImVec4(0.26f, 0.32f, 0.40f, 1.00f);
    colors[ImGuiCol_TabActive]             = ImVec4(0.22f, 0.26f, 0.33f, 1.00f);
    colors[ImGuiCol_TabUnfocused]          = ImVec4(0.14f, 0.15f, 0.18f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]         = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]     = ImVec4(0.26f, 0.29f, 0.35f, 1.00f);
    colors[ImGuiCol_TableBorderLight]      = ImVec4(0.20f, 0.22f, 0.26f, 1.00f);
    colors[ImGuiCol_TableRowBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);
    colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.25f, 0.45f, 0.75f, 0.40f);
    colors[ImGuiCol_NavHighlight]          = ImVec4(0.35f, 0.65f, 0.95f, 1.00f);

    style.WindowRounding    = 5.0f;
    style.FrameRounding     = 4.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 4.0f;
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.PopupBorderSize   = 1.0f;
    style.WindowPadding     = ImVec2(10.0f, 10.0f);
    style.FramePadding      = ImVec2(6.0f, 4.0f);
    style.ItemSpacing       = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
    style.ScrollbarSize     = 14.0f;
    style.GrabMinSize       = 10.0f;
}

inline void SetupSapphireTheme(ImGuiStyle& style) {
    ImGui::StyleColorsDark(&style);
    ImVec4* colors = style.Colors;

    colors[ImGuiCol_Text]                  = ImVec4(0.92f, 0.95f, 0.98f, 1.00f);
    colors[ImGuiCol_TextDisabled]          = ImVec4(0.48f, 0.55f, 0.68f, 1.00f);
    colors[ImGuiCol_WindowBg]              = ImVec4(0.08f, 0.10f, 0.15f, 1.00f);
    colors[ImGuiCol_ChildBg]               = ImVec4(0.10f, 0.12f, 0.18f, 1.00f);
    colors[ImGuiCol_PopupBg]               = ImVec4(0.09f, 0.11f, 0.17f, 0.98f);
    colors[ImGuiCol_Border]                = ImVec4(0.20f, 0.28f, 0.42f, 0.65f);
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]               = ImVec4(0.13f, 0.17f, 0.26f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.18f, 0.24f, 0.36f, 1.00f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.22f, 0.30f, 0.46f, 1.00f);
    colors[ImGuiCol_TitleBg]               = ImVec4(0.07f, 0.09f, 0.14f, 1.00f);
    colors[ImGuiCol_TitleBgActive]         = ImVec4(0.12f, 0.16f, 0.26f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.07f, 0.09f, 0.14f, 0.75f);
    colors[ImGuiCol_MenuBarBg]             = ImVec4(0.09f, 0.11f, 0.17f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.07f, 0.09f, 0.14f, 0.50f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.18f, 0.26f, 0.42f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.24f, 0.34f, 0.54f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.30f, 0.42f, 0.65f, 1.00f);
    colors[ImGuiCol_CheckMark]             = ImVec4(0.38f, 0.72f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab]            = ImVec4(0.30f, 0.58f, 0.90f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.42f, 0.72f, 1.00f, 1.00f);
    colors[ImGuiCol_Button]                = ImVec4(0.16f, 0.24f, 0.38f, 1.00f);
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.22f, 0.34f, 0.54f, 1.00f);
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.28f, 0.44f, 0.68f, 1.00f);
    colors[ImGuiCol_Header]                = ImVec4(0.16f, 0.24f, 0.38f, 1.00f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.22f, 0.34f, 0.52f, 1.00f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.28f, 0.42f, 0.64f, 1.00f);
    colors[ImGuiCol_Separator]             = ImVec4(0.20f, 0.28f, 0.42f, 0.65f);
    colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.30f, 0.45f, 0.68f, 1.00f);
    colors[ImGuiCol_SeparatorActive]       = ImVec4(0.40f, 0.60f, 0.88f, 1.00f);
    colors[ImGuiCol_ResizeGrip]            = ImVec4(0.20f, 0.32f, 0.50f, 0.50f);
    colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.28f, 0.44f, 0.68f, 0.70f);
    colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.36f, 0.56f, 0.85f, 0.90f);
    colors[ImGuiCol_Tab]                   = ImVec4(0.13f, 0.18f, 0.28f, 1.00f);
    colors[ImGuiCol_TabHovered]            = ImVec4(0.22f, 0.34f, 0.52f, 1.00f);
    colors[ImGuiCol_TabActive]             = ImVec4(0.18f, 0.27f, 0.44f, 1.00f);
    colors[ImGuiCol_TabUnfocused]          = ImVec4(0.10f, 0.13f, 0.20f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.14f, 0.20f, 0.32f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]         = ImVec4(0.13f, 0.18f, 0.28f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]     = ImVec4(0.22f, 0.32f, 0.48f, 1.00f);
    colors[ImGuiCol_TableBorderLight]      = ImVec4(0.16f, 0.22f, 0.34f, 1.00f);
    colors[ImGuiCol_TableRowBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(0.18f, 0.28f, 0.48f, 0.10f);
    colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.25f, 0.45f, 0.75f, 0.45f);
    colors[ImGuiCol_NavHighlight]          = ImVec4(0.38f, 0.72f, 1.00f, 1.00f);

    style.WindowRounding    = 5.0f;
    style.FrameRounding     = 4.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 4.0f;
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.PopupBorderSize   = 1.0f;
    style.WindowPadding     = ImVec2(10.0f, 10.0f);
    style.FramePadding      = ImVec2(6.0f, 4.0f);
    style.ItemSpacing       = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
    style.ScrollbarSize     = 14.0f;
    style.GrabMinSize       = 10.0f;
}

inline void SetupPaperAndInkTheme(ImGuiStyle& style) {
    ImGui::StyleColorsLight(&style);
    ImVec4* colors = style.Colors;

    colors[ImGuiCol_Text]                  = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_TextDisabled]          = ImVec4(0.50f, 0.50f, 0.52f, 1.00f);
    colors[ImGuiCol_WindowBg]              = ImVec4(0.95f, 0.95f, 0.94f, 1.00f);
    colors[ImGuiCol_ChildBg]               = ImVec4(0.91f, 0.91f, 0.89f, 1.00f);
    colors[ImGuiCol_PopupBg]               = ImVec4(0.97f, 0.97f, 0.96f, 0.98f);
    colors[ImGuiCol_Border]                = ImVec4(0.72f, 0.72f, 0.70f, 0.85f);
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]               = ImVec4(0.86f, 0.86f, 0.84f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.80f, 0.80f, 0.78f, 1.00f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.74f, 0.74f, 0.72f, 1.00f);
    colors[ImGuiCol_TitleBg]               = ImVec4(0.90f, 0.90f, 0.88f, 1.00f);
    colors[ImGuiCol_TitleBgActive]         = ImVec4(0.84f, 0.84f, 0.82f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.90f, 0.90f, 0.88f, 0.75f);
    colors[ImGuiCol_MenuBarBg]             = ImVec4(0.90f, 0.90f, 0.88f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.91f, 0.91f, 0.89f, 0.50f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.74f, 0.74f, 0.72f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.62f, 0.62f, 0.60f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.48f, 0.48f, 0.46f, 1.00f);
    colors[ImGuiCol_CheckMark]             = ImVec4(0.18f, 0.36f, 0.58f, 1.00f);
    colors[ImGuiCol_SliderGrab]            = ImVec4(0.38f, 0.46f, 0.58f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.24f, 0.34f, 0.48f, 1.00f);
    colors[ImGuiCol_Button]                = ImVec4(0.83f, 0.83f, 0.81f, 1.00f);
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.76f, 0.76f, 0.74f, 1.00f);
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.68f, 0.68f, 0.66f, 1.00f);
    colors[ImGuiCol_Header]                = ImVec4(0.83f, 0.84f, 0.86f, 1.00f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.75f, 0.77f, 0.81f, 1.00f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.67f, 0.70f, 0.76f, 1.00f);
    colors[ImGuiCol_Separator]             = ImVec4(0.72f, 0.72f, 0.70f, 0.85f);
    colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.50f, 0.52f, 0.56f, 1.00f);
    colors[ImGuiCol_SeparatorActive]       = ImVec4(0.32f, 0.34f, 0.38f, 1.00f);
    colors[ImGuiCol_ResizeGrip]            = ImVec4(0.65f, 0.65f, 0.63f, 0.50f);
    colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.50f, 0.50f, 0.48f, 0.70f);
    colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.35f, 0.35f, 0.33f, 0.90f);
    colors[ImGuiCol_Tab]                   = ImVec4(0.85f, 0.85f, 0.83f, 1.00f);
    colors[ImGuiCol_TabHovered]            = ImVec4(0.77f, 0.79f, 0.83f, 1.00f);
    colors[ImGuiCol_TabActive]             = ImVec4(0.95f, 0.95f, 0.94f, 1.00f);
    colors[ImGuiCol_TabUnfocused]          = ImVec4(0.87f, 0.87f, 0.85f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.91f, 0.91f, 0.89f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]         = ImVec4(0.85f, 0.85f, 0.84f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]     = ImVec4(0.68f, 0.68f, 0.66f, 1.00f);
    colors[ImGuiCol_TableBorderLight]      = ImVec4(0.78f, 0.78f, 0.76f, 1.00f);
    colors[ImGuiCol_TableRowBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(0.00f, 0.00f, 0.00f, 0.04f);
    colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.20f, 0.40f, 0.70f, 0.30f);
    colors[ImGuiCol_NavHighlight]          = ImVec4(0.18f, 0.36f, 0.58f, 1.00f);

    style.WindowRounding    = 5.0f;
    style.FrameRounding     = 4.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 4.0f;
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.WindowPadding     = ImVec2(10.0f, 10.0f);
    style.FramePadding      = ImVec2(6.0f, 4.0f);
    style.ItemSpacing       = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
    style.ScrollbarSize     = 14.0f;
    style.GrabMinSize       = 10.0f;
}

inline void ApplyAppearance(GakuUiTheme theme, float uiScale, float dpiScale) {
    ImGuiStyle baseStyle;
    switch (theme) {
    case GAKU_UI_THEME_SAPPHIRE:
        SetupSapphireTheme(baseStyle);
        break;
    case GAKU_UI_THEME_PAPER_AND_INK:
        SetupPaperAndInkTheme(baseStyle);
        break;
    case GAKU_UI_THEME_DARK:
    default:
        SetupDarkTheme(baseStyle);
        break;
    }
    const float effectiveScale = (std::max)(0.5f, uiScale * dpiScale);
    baseStyle.ScaleAllSizes(effectiveScale);
    ImGui::GetStyle() = baseStyle;
    ImGui::GetIO().FontGlobalScale = effectiveScale;
}

inline float GetWindowDpiScale(HWND window) {
    if (!window) return 1.0f;
    typedef UINT (WINAPI* PFN_GetDpiForWindow)(HWND);
    static auto pfnGetDpiForWindow = reinterpret_cast<PFN_GetDpiForWindow>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (pfnGetDpiForWindow) {
        const UINT dpi = pfnGetDpiForWindow(window);
        if (dpi > 0) return static_cast<float>(dpi) / 96.0f;
    }
    return 1.0f;
}

inline void LoadAppearanceSettings(const std::filesystem::path& configPath, GakuUiTheme& outTheme, float& outUiScale) {
    if (configPath.empty()) return;
    const std::wstring themeStr = ReadConfigValue(configPath, L"appearance", L"theme");
    if (lstrcmpiW(themeStr.c_str(), L"Sapphire") == 0) {
        outTheme = GAKU_UI_THEME_SAPPHIRE;
    } else if (lstrcmpiW(themeStr.c_str(), L"PaperAndInk") == 0 || lstrcmpiW(themeStr.c_str(), L"Paper & Ink") == 0) {
        outTheme = GAKU_UI_THEME_PAPER_AND_INK;
    } else if (!themeStr.empty()) {
        outTheme = GAKU_UI_THEME_DARK;
    }

    const std::wstring scaleStr = ReadConfigValue(configPath, L"appearance", L"scale");
    if (!scaleStr.empty()) {
        try {
            int scalePct = std::stoi(scaleStr);
            if (scalePct >= 50 && scalePct <= 300) {
                outUiScale = static_cast<float>(scalePct) / 100.0f;
            }
        } catch (...) {}
    }
}

inline void SaveAppearanceSettings(const std::filesystem::path& configPath, GakuUiTheme theme, float uiScale) {
    if (configPath.empty()) return;
    const wchar_t* themeStr = L"Dark";
    if (theme == GAKU_UI_THEME_SAPPHIRE) themeStr = L"Sapphire";
    else if (theme == GAKU_UI_THEME_PAPER_AND_INK) themeStr = L"PaperAndInk";

    int scalePct = static_cast<int>(std::round(uiScale * 100.0f));
    wchar_t scaleBuf[16]{};
    _itow_s(scalePct, scaleBuf, 10);

    WritePrivateProfileStringW(L"appearance", L"theme", themeStr, configPath.c_str());
    WritePrivateProfileStringW(L"appearance", L"scale", scaleBuf, configPath.c_str());
}

} // namespace GakuLoader::DesktopAppearance
