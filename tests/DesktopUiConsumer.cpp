// Built with MinGW; the host and its ImGui are built with MSVC /MT.
// This exercises the actual cross-module context and allocator contract.
#include "GakuPlugin.h"
#include <imgui.h>
#include <cstring>

extern "C" __declspec(dllexport) int TestDesktopUiConsumerDraw(const GakuDesktopUiService* service) {
    if (!service || service->abiVersion != GAKU_SERVICE_VERSION || service->structSize < sizeof(*service)) return 1;
    const GakuImGuiAbi abi{IMGUI_VERSION_NUM, sizeof(ImGuiIO), sizeof(ImGuiStyle),
        sizeof(ImDrawVert), sizeof(ImDrawIdx), sizeof(ImWchar)};
    GakuDesktopUiBinding binding{};
    if (service->bind(&abi, sizeof(binding), &binding) != GAKU_OK) return 2;
    if (!binding.imguiContext || !binding.alloc || !binding.free) return 3;
    GakuDesktopUiBinding mismatch; std::memset(&mismatch, 0x5A, sizeof(mismatch));
    const auto before = mismatch;
    auto wrong = abi; ++wrong.ioSize;
    if (service->bind(&wrong, sizeof(mismatch), &mismatch) != GAKU_E_VERSION ||
        std::memcmp(&mismatch, &before, sizeof(before))) return 4;
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGuiMemAllocFunc oldAlloc; ImGuiMemFreeFunc oldFree; void* oldUser;
    ImGui::GetAllocatorFunctions(&oldAlloc, &oldFree, &oldUser);
    ImGui::SetAllocatorFunctions(binding.alloc, binding.free, binding.allocatorUserData);
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(binding.imguiContext));
    // Exercise persistent host-owned window/widget allocations in a foreign CRT.
    ImGui::SetNextWindowPos(ImVec2(200, 100), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(350, 220), ImGuiCond_Always);
    if (ImGui::Begin("MinGW consumer of MSVC GakuLoader")) {
        static bool checkbox = true;
        static float slider = 0.5f;
        static char text[64] = "cross-DLL context + allocator";
        ImGui::TextUnformatted("GakuLoader desktop capability smoke");
        ImGui::Checkbox("Persistent allocation", &checkbox);
        ImGui::SliderFloat("Value", &slider, 0.0f, 1.0f);
        ImGui::InputText("Text", text, sizeof(text));
    }
    ImGui::End();
    ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(120,20), ImVec2(180,100), IM_COL32(0,255,255,255));
    ImGui::SetCurrentContext(previous);
    ImGui::SetAllocatorFunctions(oldAlloc, oldFree, oldUser);
    return 0;
}
