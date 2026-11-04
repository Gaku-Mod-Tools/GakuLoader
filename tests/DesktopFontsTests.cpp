#include "DesktopFonts.hpp"
#include "DesktopAppearance.hpp"
#include <cassert>
#include <cstdio>
#include <limits>

int main() {
    using namespace GakuLoader;
    auto* context = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().LogFilename = nullptr;
    assert(DesktopFonts::PixelSize(1.0f, 1.0f) == 18.0f);
    assert(DesktopFonts::PixelSize(1.25f, 1.5f) == 34.0f);
    assert(DesktopFonts::PixelSize(std::numeric_limits<float>::infinity(), 1.0f) == 0);
    for (const auto scale : {1.0f, 1.5f, 3.0f}) {
        const auto result = DesktopFonts::BuildFonts(scale, 1.0f);
        assert(result.ready);
        assert(ImGui::GetIO().Fonts->IsBuilt());
        assert(ImGui::GetIO().FontDefault->FontSize == result.pixelSize);
        assert(ImGui::GetIO().FontGlobalScale == 1.0f);
        assert(static_cast<uint64_t>(result.atlasWidth) * result.atlasHeight * 4 <= DesktopFonts::kMaxTextureBytes);
        if (result.multilingual) {
            for (const auto glyph : {0x7B80, 0x7E41, 0x9AD4, 0x3042, 0x30A2})
                assert(ImGui::GetIO().FontDefault->FindGlyphNoFallback(static_cast<ImWchar>(glyph)));
        }
        DesktopAppearance::ApplyAppearance(GAKU_UI_THEME_DARK, scale, 1.0f);
        assert(ImGui::GetIO().FontGlobalScale == 1.0f);
        ImGui::GetIO().DisplaySize = ImVec2(640, 480);
        ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        ImGui::Begin("Font rebuild regression");
        ImGui::TextUnformatted("Simplified 简体 / Traditional 繁體 / Japanese あア");
        assert(ImGui::GetFont()->FontSize == result.pixelSize);
        ImGui::End();
        ImGui::Render();
        assert(ImGui::GetDrawData());
        std::printf("scale=%.2f raster=%.0f atlas=%dx%d multilingual=%d\n", scale, result.pixelSize,
            result.atlasWidth, result.atlasHeight, result.multilingual);
    }
    const auto fractionalDpi = DesktopFonts::BuildFonts(1.25f, 1.5f);
    assert(fractionalDpi.ready && fractionalDpi.pixelSize == 34.0f);
    auto* priorAtlas = ImGui::GetIO().Fonts;
    auto* priorFont = ImGui::GetIO().FontDefault;
    assert(!DesktopFonts::BuildFonts(3.0f, 2.0f).ready);
    assert(!DesktopFonts::BuildFonts(-1.0f, 1.0f).ready);
    assert(ImGui::GetIO().Fonts == priorAtlas && ImGui::GetIO().FontDefault == priorFont);
    ImGui::DestroyContext(context);
    std::puts("Desktop font rasterization and bounded rebuild checks passed.");
}
