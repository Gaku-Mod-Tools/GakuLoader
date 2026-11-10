// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

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

// Apple-inspired hierarchy and restrained accent colors, rendered entirely by
// ImGui. Opaque surfaces keep text readable over arbitrary game imagery; this
// does not implement a native Apple material or a backdrop-blur renderer.
inline void SetupAppleTheme(ImGuiStyle& style, bool dark) {
    if (dark) ImGui::StyleColorsDark(&style);
    else ImGui::StyleColorsLight(&style);
    ImVec4* colors = style.Colors;
    const auto adaptive = [dark](ImVec4 light, ImVec4 night) { return dark ? night : light; };
    const ImVec4 accent = adaptive(ImVec4(0.00f, 0.39f, 0.82f, 1.00f), ImVec4(0.31f, 0.65f, 1.00f, 1.00f));
    const ImVec4 separator = adaptive(ImVec4(0.78f, 0.79f, 0.81f, 1.00f), ImVec4(0.29f, 0.29f, 0.32f, 1.00f));

    colors[ImGuiCol_Text]                  = adaptive(ImVec4(0.11f, 0.11f, 0.13f, 1.00f), ImVec4(0.96f, 0.96f, 0.98f, 1.00f));
    colors[ImGuiCol_TextDisabled]          = adaptive(ImVec4(0.40f, 0.41f, 0.44f, 1.00f), ImVec4(0.65f, 0.65f, 0.69f, 1.00f));
    colors[ImGuiCol_WindowBg]              = adaptive(ImVec4(0.96f, 0.96f, 0.97f, 1.00f), ImVec4(0.11f, 0.11f, 0.12f, 1.00f));
    colors[ImGuiCol_ChildBg]               = adaptive(ImVec4(1.00f, 1.00f, 1.00f, 1.00f), ImVec4(0.15f, 0.15f, 0.16f, 1.00f));
    colors[ImGuiCol_PopupBg]               = adaptive(ImVec4(1.00f, 1.00f, 1.00f, 1.00f), ImVec4(0.19f, 0.19f, 0.20f, 1.00f));
    colors[ImGuiCol_Border]                = separator;
    colors[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_FrameBg]               = adaptive(ImVec4(0.91f, 0.92f, 0.94f, 1.00f), ImVec4(0.21f, 0.21f, 0.23f, 1.00f));
    colors[ImGuiCol_FrameBgHovered]        = adaptive(ImVec4(0.86f, 0.89f, 0.94f, 1.00f), ImVec4(0.26f, 0.29f, 0.34f, 1.00f));
    colors[ImGuiCol_FrameBgActive]         = adaptive(ImVec4(0.79f, 0.85f, 0.94f, 1.00f), ImVec4(0.24f, 0.35f, 0.48f, 1.00f));
    colors[ImGuiCol_TitleBg]               = colors[ImGuiCol_WindowBg];
    colors[ImGuiCol_TitleBgActive]         = adaptive(ImVec4(0.92f, 0.93f, 0.95f, 1.00f), ImVec4(0.17f, 0.17f, 0.19f, 1.00f));
    colors[ImGuiCol_TitleBgCollapsed]      = colors[ImGuiCol_TitleBg];
    colors[ImGuiCol_MenuBarBg]             = colors[ImGuiCol_TitleBgActive];
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_ScrollbarGrab]         = adaptive(ImVec4(0.66f, 0.67f, 0.70f, 1.00f), ImVec4(0.39f, 0.40f, 0.43f, 1.00f));
    colors[ImGuiCol_ScrollbarGrabHovered]  = adaptive(ImVec4(0.53f, 0.54f, 0.57f, 1.00f), ImVec4(0.49f, 0.50f, 0.54f, 1.00f));
    colors[ImGuiCol_ScrollbarGrabActive]   = adaptive(ImVec4(0.42f, 0.44f, 0.47f, 1.00f), ImVec4(0.60f, 0.61f, 0.65f, 1.00f));
    colors[ImGuiCol_CheckMark]             = accent;
    colors[ImGuiCol_SliderGrab]            = accent;
    colors[ImGuiCol_SliderGrabActive]      = adaptive(ImVec4(0.00f, 0.29f, 0.66f, 1.00f), ImVec4(0.52f, 0.76f, 1.00f, 1.00f));
    colors[ImGuiCol_Button]                = adaptive(ImVec4(0.91f, 0.93f, 0.96f, 1.00f), ImVec4(0.24f, 0.25f, 0.28f, 1.00f));
    colors[ImGuiCol_ButtonHovered]         = adaptive(ImVec4(0.83f, 0.88f, 0.96f, 1.00f), ImVec4(0.27f, 0.33f, 0.42f, 1.00f));
    colors[ImGuiCol_ButtonActive]          = adaptive(ImVec4(0.73f, 0.82f, 0.94f, 1.00f), ImVec4(0.22f, 0.38f, 0.55f, 1.00f));
    colors[ImGuiCol_Header]                = adaptive(ImVec4(0.85f, 0.91f, 0.99f, 1.00f), ImVec4(0.18f, 0.29f, 0.42f, 1.00f));
    colors[ImGuiCol_HeaderHovered]         = colors[ImGuiCol_ButtonHovered];
    colors[ImGuiCol_HeaderActive]          = colors[ImGuiCol_ButtonActive];
    colors[ImGuiCol_Separator]             = separator;
    colors[ImGuiCol_SeparatorHovered]      = accent;
    colors[ImGuiCol_SeparatorActive]       = colors[ImGuiCol_SliderGrabActive];
    colors[ImGuiCol_ResizeGrip]            = adaptive(ImVec4(0.36f, 0.45f, 0.58f, 0.22f), ImVec4(0.40f, 0.57f, 0.76f, 0.25f));
    colors[ImGuiCol_ResizeGripHovered]     = accent;
    colors[ImGuiCol_ResizeGripActive]      = colors[ImGuiCol_SliderGrabActive];
    colors[ImGuiCol_Tab]                   = colors[ImGuiCol_FrameBg];
    colors[ImGuiCol_TabHovered]            = colors[ImGuiCol_ButtonHovered];
    colors[ImGuiCol_TabActive]             = colors[ImGuiCol_ChildBg];
    colors[ImGuiCol_TabUnfocused]          = colors[ImGuiCol_WindowBg];
    colors[ImGuiCol_TabUnfocusedActive]    = colors[ImGuiCol_FrameBg];
    colors[ImGuiCol_TableHeaderBg]         = adaptive(ImVec4(0.93f, 0.94f, 0.96f, 1.00f), ImVec4(0.19f, 0.19f, 0.21f, 1.00f));
    colors[ImGuiCol_TableBorderStrong]     = separator;
    colors[ImGuiCol_TableBorderLight]      = adaptive(ImVec4(0.88f, 0.89f, 0.91f, 1.00f), ImVec4(0.23f, 0.23f, 0.25f, 1.00f));
    colors[ImGuiCol_TableRowBg]            = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt]         = adaptive(ImVec4(0.12f, 0.18f, 0.28f, 0.035f), ImVec4(1, 1, 1, 0.025f));
    colors[ImGuiCol_TextSelectedBg]        = adaptive(ImVec4(0.00f, 0.42f, 1.00f, 0.20f), ImVec4(0.20f, 0.52f, 1.00f, 0.35f));
    colors[ImGuiCol_DragDropTarget]        = accent;
    colors[ImGuiCol_NavHighlight]          = accent;
    colors[ImGuiCol_NavWindowingHighlight] = adaptive(ImVec4(0.10f, 0.30f, 0.60f, 0.75f), ImVec4(0.70f, 0.84f, 1.00f, 0.75f));
    colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.08f, 0.09f, 0.12f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.06f, 0.08f, 0.12f, dark ? 0.52f : 0.28f);
    colors[ImGuiCol_PlotLines]             = accent;
    colors[ImGuiCol_PlotLinesHovered]      = adaptive(ImVec4(0.73f, 0.30f, 0.00f, 1.00f), ImVec4(1.00f, 0.65f, 0.28f, 1.00f));
    colors[ImGuiCol_PlotHistogram]         = accent;
    colors[ImGuiCol_PlotHistogramHovered]  = colors[ImGuiCol_PlotLinesHovered];

    style.WindowRounding    = 12.0f;
    style.ChildRounding     = 10.0f;
    style.FrameRounding     = 7.0f;
    style.PopupRounding     = 12.0f;
    style.ScrollbarRounding = 9.0f;
    style.GrabRounding      = 7.0f;
    style.TabRounding       = 7.0f;
    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.PopupBorderSize   = 1.0f;
    style.WindowPadding     = ImVec2(14.0f, 12.0f);
    style.FramePadding      = ImVec2(9.0f, 5.0f);
    style.ItemSpacing       = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing  = ImVec2(7.0f, 5.0f);
    style.CellPadding       = ImVec2(8.0f, 6.0f);
    style.ScrollbarSize     = 12.0f;
    style.GrabMinSize       = 12.0f;
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
    case GAKU_UI_THEME_APPLE_LIGHT:
        SetupAppleTheme(baseStyle, false);
        break;
    case GAKU_UI_THEME_APPLE_DARK:
        SetupAppleTheme(baseStyle, true);
        break;
    case GAKU_UI_THEME_DARK:
    default:
        SetupDarkTheme(baseStyle);
        break;
    }
    const float effectiveScale = (std::max)(0.5f, uiScale * dpiScale);
    baseStyle.ScaleAllSizes(effectiveScale);
    ImGui::GetStyle() = baseStyle;
    // DesktopFonts rasterizes glyphs at the actual UI/DPI pixel size. Scaling
    // that bitmap again would blur it and apply the scale twice.
    ImGui::GetIO().FontGlobalScale = 1.0f;
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
    } else if (lstrcmpiW(themeStr.c_str(), L"AppleLight") == 0 || lstrcmpiW(themeStr.c_str(), L"Apple Light") == 0) {
        outTheme = GAKU_UI_THEME_APPLE_LIGHT;
    } else if (lstrcmpiW(themeStr.c_str(), L"AppleDark") == 0 || lstrcmpiW(themeStr.c_str(), L"Apple Dark") == 0) {
        outTheme = GAKU_UI_THEME_APPLE_DARK;
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

inline bool SaveAppearanceSettings(const std::filesystem::path& configPath, GakuUiTheme theme, float uiScale) {
    if (configPath.empty()) return false;
    const wchar_t* themeStr = L"Dark";
    if (theme == GAKU_UI_THEME_SAPPHIRE) themeStr = L"Sapphire";
    else if (theme == GAKU_UI_THEME_PAPER_AND_INK) themeStr = L"PaperAndInk";
    else if (theme == GAKU_UI_THEME_APPLE_LIGHT) themeStr = L"AppleLight";
    else if (theme == GAKU_UI_THEME_APPLE_DARK) themeStr = L"AppleDark";

    int scalePct = static_cast<int>(std::round(uiScale * 100.0f));
    wchar_t scaleBuf[16]{};
    _itow_s(scalePct, scaleBuf, 10);

    const bool themeSaved = WritePrivateProfileStringW(L"appearance", L"theme", themeStr, configPath.c_str()) != FALSE;
    const bool scaleSaved = WritePrivateProfileStringW(L"appearance", L"scale", scaleBuf, configPath.c_str()) != FALSE;
    return themeSaved && scaleSaved;
}

} // namespace GakuLoader::DesktopAppearance
