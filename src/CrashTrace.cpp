#include "CrashTrace.hpp"

#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <string>

namespace GakuLoader::CrashTrace {

namespace {

std::wstring g_path;
std::atomic_int g_reported{0};

// Deliberately allocation-free and lock-free: the crash may have happened while
// the log mutex was held, and a diagnostic that deadlocks the process is worse
// than no diagnostic. One append handle per report is cheap enough.
void Append(const wchar_t* text) {
    if (g_path.empty()) return;
    const HANDLE file = CreateFileW(g_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, text, static_cast<DWORD>(std::wcslen(text) * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(file);
}

LONG WINAPI Handler(EXCEPTION_POINTERS* info) {
    if (!info || !info->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code != 0xC0000005 && code != 0xC00000FD && code != 0xC0000409 && code != 0xC0000374)
        return EXCEPTION_CONTINUE_SEARCH;
    if (g_reported.fetch_add(1, std::memory_order_relaxed) >= 8) return EXCEPTION_CONTINUE_SEARCH;

    void* address = info->ExceptionRecord->ExceptionAddress;
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(address), &module);
    wchar_t modulePath[MAX_PATH]{};
    if (module) GetModuleFileNameW(module, modulePath, MAX_PATH);
    const unsigned long long offset = module
        ? static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(module))
        : 0;
    const void* access = (code == 0xC0000005 && info->ExceptionRecord->NumberParameters >= 2)
        ? reinterpret_cast<void*>(info->ExceptionRecord->ExceptionInformation[1]) : nullptr;

    wchar_t line[768];
    _snwprintf_s(line, _countof(line), _TRUNCATE,
        L"[Crash] t=%llums code=0x%08lX addr=%p module=%s+0x%llX access=%p thread=%lu\r\n",
        static_cast<unsigned long long>(GetTickCount64()), static_cast<unsigned long>(code), address,
        module ? modulePath : L"(unknown)", offset, access,
        static_cast<unsigned long>(GetCurrentThreadId()));
    Append(line);

    // The faulting address alone cannot tell a bad pointer from a bad call: the
    // loader, the plugin and the game all live in one process, and the useful
    // frame is usually the caller. Frames are printed as module+offset so they
    // resolve against the same map files the build already emits.
    using CaptureFn = USHORT (WINAPI*)(ULONG, ULONG, PVOID*, PULONG);
    const auto capture = reinterpret_cast<CaptureFn>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlCaptureStackBackTrace"));
    if (!capture) return EXCEPTION_CONTINUE_SEARCH;

    void* frames[24]{};
    USHORT count = capture(0, 24, frames, nullptr);
    static wchar_t stack[24 * 220];
    stack[0] = L'\0';
    size_t used = 0;
    for (USHORT index = 0; index < count; ++index) {
        HMODULE frameModule = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(frames[index]), &frameModule);
        wchar_t framePath[MAX_PATH]{};
        if (frameModule) GetModuleFileNameW(frameModule, framePath, MAX_PATH);
        const wchar_t* name = framePath[0] ? wcsrchr(framePath, L'\\') : nullptr;
        name = name ? name + 1 : (framePath[0] ? framePath : L"(unknown)");
        const unsigned long long frameOffset = frameModule
            ? static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(frames[index]) -
                reinterpret_cast<uintptr_t>(frameModule)) : 0;
        const int written = _snwprintf_s(stack + used, _countof(stack) - used, _TRUNCATE,
            L"[Crash] stack[%02u] %s+0x%llX\r\n", index, name, frameOffset);
        if (written <= 0) break;
        used += static_cast<size_t>(written);
    }
    if (used) Append(stack);
    return EXCEPTION_CONTINUE_SEARCH;
}

}

void Arm(const std::filesystem::path& logPath) {
    g_path = logPath.wstring();
    // First-chance so the record exists even if a later handler terminates the
    // process; the return value continues the normal search chain.
    AddVectoredExceptionHandler(1, Handler);
}

}
