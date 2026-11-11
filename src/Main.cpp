// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

#include <windows.h>
#include <filesystem>
#include <cwchar>
#include "Proxy.hpp"
#include "Host.hpp"
#include "GameHook.hpp"
#include "QuitGuard.hpp"
#include "DesktopUi.hpp"
#include "HostConfig.hpp"

namespace {

bool g_isGameProcess = false;

bool IsGameProcess() {
    wchar_t executable[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return false;
    const wchar_t* name = std::wcsrchr(executable, L'\\');
    return lstrcmpiW(name ? name + 1 : executable, L"gakumas.exe") == 0;
}

DWORD WINAPI LoaderStartupThread(LPVOID) {
    // Runtime unload is unsupported: hooks/service pointers are process-lifetime.
    // Pin outside DllMain so an accidental FreeLibrary cannot strand detours.
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&LoaderStartupThread), &self);
    wchar_t executable[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return 0;
    const auto gameRoot = std::filesystem::path(executable).parent_path();
    const auto localRoot = gameRoot / L"GakuLoader";
    const auto config = localRoot / L"Configs" / L"gakuloader.ini";
    if (GakuLoader::ReadConfigFlag(config, L"host", L"console", false) && !GetConsoleWindow()) {
        if (AllocConsole()) {
            SetConsoleTitleW(L"GakuLoader");
            // AllocConsole only rebinds the standard handles when they were
            // unset. A launcher that started the game with redirected stdout
            // leaves them pointing at its pipe, so the window the user asked
            // for stays blank while every record goes to the file. Bind
            // CONOUT$ explicitly: the loader never writes to a redirected
            // stdout anyway (WriteLineLocked requires a real console).
            if (const HANDLE conout = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
                conout != INVALID_HANDLE_VALUE) {
                SetStdHandle(STD_OUTPUT_HANDLE, conout);
                SetStdHandle(STD_ERROR_HANDLE, conout);
            }
        }
    }
    try {
        GakuLoader::Initialize(gameRoot, localRoot);
        // Desktop services must exist before activation: a plugin may bind them
        // from its activate callback.
        GakuLoader::DesktopUi::Initialize();
        // Early native activation: no Unity, no Vuplex, no IL2CPP. This thread
        // is not the Windows loader lock, so plugin DllMain may load modules.
        GakuLoader::ActivatePlugins();
    } catch (const std::exception& exception) {
        GakuLoader::LogError("Exception during loader startup: %s", exception.what());
    }

    // Runtime resolution and main-thread pumping stay independent of activation.
    GakuLoader::StartHookThread();

    // Arm the exit guard on its own thread: it must work even when the game
    // runtime never becomes resolvable.
    GakuLoader::StartQuitGuard();
    return 0;
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hinstDLL);
        GakuLoader::InitProxy();
        g_isGameProcess = IsGameProcess();
        if (g_isGameProcess) {
            const HANDLE startup = CreateThread(nullptr, 0, LoaderStartupThread, nullptr, 0, nullptr);
            if (startup) CloseHandle(startup);
        }
        break;

    case DLL_PROCESS_DETACH:
        // Process termination runs under the loader lock after other threads
        // have stopped. The OS reclaims these resources; waiting or invoking
        // plugin/IL2CPP teardown here can deadlock the process.
        if (lpvReserved != nullptr) break;
        if (!g_isGameProcess) {
            GakuLoader::ShutdownProxy();
            break;
        }
        // Never wait for workers, call foreign plugin code, destroy D3D/ImGui,
        // or FreeLibrary a plugin under the loader lock. The game module is
        // pinned above; orderly callbacks run through UnityQuit.
        break;
    }
    return TRUE;
}
