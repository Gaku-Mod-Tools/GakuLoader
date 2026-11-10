// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

#pragma once
#include <windows.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>

namespace GakuLoader::DesktopFonts {
constexpr float kBasePixelSize = 18.0f;
// A full Han atlas grows with the square of the raster size. Keep the largest
// supported request within a 256 MiB RGBA upload rather than exhausting a game
// process for an extreme UI-scale/DPI combination. The manager retains its last
// working scale when a request is rejected.
constexpr float kMaxPixelSize = 54.0f;
constexpr uint64_t kMaxTextureBytes = 256ull * 1024 * 1024;
struct BuildResult {
    bool ready{false}, multilingual{false};
    float pixelSize{0};
    int atlasWidth{0}, atlasHeight{0};
};

inline float PixelSize(float uiScale, float dpiScale) {
    if (!std::isfinite(uiScale) || !std::isfinite(dpiScale) || uiScale <= 0 || dpiScale <= 0) return 0;
    return (std::max)(1.0f, std::round(kBasePixelSize * uiScale * dpiScale));
}

inline ImFont* AddInstalledFont(ImFontAtlas& atlas, float pixelSize) {
    wchar_t windowsPath[32768]{};
    const UINT length = GetWindowsDirectoryW(windowsPath, ARRAYSIZE(windowsPath));
    if (!length || length >= ARRAYSIZE(windowsPath)) return nullptr;
    const auto fonts = std::filesystem::path(windowsPath) / L"Fonts";
    // These are read from Windows at runtime, never redistributed. Load through
    // a Unicode filesystem path; ImGui's filename helper uses narrow fopen.
    for (const auto* filename : {L"msyh.ttc", L"msgothic.ttc", L"YuGothM.ttc", L"simsun.ttc", L"mingliu.ttc"}) {
        std::ifstream file(fonts / filename, std::ios::binary | std::ios::ate);
        if (!file) continue;
        const auto lengthBytes = file.tellg();
        if (lengthBytes <= 0 || lengthBytes > 128 * 1024 * 1024) continue;
        const int size = static_cast<int>(lengthBytes);
        void* bytes = IM_ALLOC(static_cast<size_t>(size));
        if (!bytes) continue;
        file.seekg(0);
        if (!file.read(static_cast<char*>(bytes), size)) { IM_FREE(bytes); continue; }
        ImFontConfig config;
        config.SizePixels = pixelSize;
        config.OversampleH = config.OversampleV = 1;
        config.PixelSnapH = true;
        // ChineseFull includes both Han variants, kana, punctuation and Latin.
        // The atlas takes ownership of the buffer, including on later rebuild.
        return atlas.AddFontFromMemoryTTF(bytes, size, pixelSize, &config, atlas.GetGlyphRangesChineseFull());
    }
    return nullptr;
}

// UI thread, outside any active ImGui frame. The caller invalidates DX11 device
// objects first and recreates them afterwards (also after a rejected request).
// This desktop context must own its atlas, as the default CreateContext does.
// Failed requests leave its current atlas and FontDefault untouched.
inline BuildResult BuildFonts(float uiScale, float dpiScale) {
    BuildResult result;
    result.pixelSize = PixelSize(uiScale, dpiScale);
    if (!ImGui::GetCurrentContext() || result.pixelSize <= 0 || result.pixelSize > kMaxPixelSize) return result;
    auto* candidate = IM_NEW(ImFontAtlas)();
    candidate->TexDesiredWidth = result.pixelSize <= 32.0f ? 4096 : 8192;
    ImFont* font = AddInstalledFont(*candidate, result.pixelSize);
    result.multilingual = font != nullptr;
    if (!font) {
        ImFontConfig config;
        config.SizePixels = result.pixelSize;
        config.OversampleH = config.OversampleV = 1;
        config.PixelSnapH = true;
        font = candidate->AddFontDefault(&config);
    }
    if (!font || !candidate->Build()) { IM_DELETE(candidate); return result; }
    result.atlasWidth = candidate->TexWidth;
    result.atlasHeight = candidate->TexHeight;
    const auto rgbaBytes = static_cast<uint64_t>(candidate->TexWidth) * candidate->TexHeight * 4;
    if (candidate->TexWidth > 16384 || candidate->TexHeight > 16384 || rgbaBytes > kMaxTextureBytes) {
        IM_DELETE(candidate); return result;
    }
    // Uploaded texture pixels do not need the original OS font buffers. Keep
    // glyph metrics and atlas pixels; rebuilding reads the installed font again.
    candidate->ClearInputData();
    auto& io = ImGui::GetIO();
    auto* previous = io.Fonts;
    io.Fonts = candidate;
    io.FontDefault = font;
    io.FontGlobalScale = 1.0f;
    IM_DELETE(previous);
    result.ready = true;
    return result;
}
} // namespace GakuLoader::DesktopFonts
