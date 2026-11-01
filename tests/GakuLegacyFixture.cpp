#include <windows.h>
#include <stdint.h>
#include <string>

// A pre-ABI-reset plugin: it exports only the retired entry points. The loader
// must load it for probing, find no GakuPlugin_Query, refuse it, and never call
// GmlPlugin_Query or GakumasPlugin_*. Marker files make that observable after
// the module has been unloaded.
namespace {
HMODULE g_instance = nullptr;
void WriteMarker(const wchar_t* name) {
    wchar_t module[MAX_PATH]{};
    if (!g_instance || !GetModuleFileNameW(g_instance, module, MAX_PATH)) return;
    wchar_t* slash = wcsrchr(module, L'\\');
    if (!slash) return;
    *(slash + 1) = L'\0';
    std::wstring path(module);
    path += name;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
}
}

extern "C" __declspec(dllexport) int32_t GmlPlugin_Query(uint32_t, uint32_t, void*) {
    WriteMarker(L"legacy-query.txt");
    return -4;
}
extern "C" __declspec(dllexport) bool GakumasPlugin_GetInfo(void*) { return false; }
extern "C" __declspec(dllexport) bool GakumasPlugin_Init(const void*) {
    WriteMarker(L"legacy-init.txt");
    return false;
}
extern "C" __declspec(dllexport) void GakumasPlugin_OnUnityReady() {}
extern "C" __declspec(dllexport) void GakumasPlugin_OnLateUpdate(void*, float) {}
extern "C" __declspec(dllexport) void GakumasPlugin_OnRenderGui() {}
extern "C" __declspec(dllexport) void GakumasPlugin_Shutdown() {}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        g_instance = instance;
        WriteMarker(L"legacy-dllmain.txt");
    }
    return TRUE;
}
