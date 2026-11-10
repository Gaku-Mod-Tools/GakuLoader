// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

// windows.h pulls in winver.h unconditionally, and that header already declares the
// version.dll entry points. MSVC treats a later __declspec(dllexport) definition of an
// already-declared function as a different linkage (C2375/C2733); GCC merges them.
// The proxy owns these definitions and needs none of those prototypes, so the winver.h
// include guard is pre-set to skip it. VER_H is its real guard.
#if defined(_MSC_VER)
#define VER_H
#endif

#include "Proxy.hpp"
#include <windows.h>

namespace {
HMODULE g_realVersion = nullptr;

void EnsureRealVersion() {
    if (!g_realVersion) {
        wchar_t sysDir[MAX_PATH];
        GetSystemDirectoryW(sysDir, MAX_PATH);
        lstrcatW(sysDir, L"\\version.dll");
        g_realVersion = LoadLibraryW(sysDir);
    }
}

template<typename T>
T GetRealProc(const char* name) {
    EnsureRealVersion();
    return reinterpret_cast<T>(GetProcAddress(g_realVersion, name));
}
} // namespace

namespace GakuLoader {
bool InitProxy() {
    EnsureRealVersion();
    return g_realVersion != nullptr;
}

void ShutdownProxy() {
    if (g_realVersion) {
        FreeLibrary(g_realVersion);
        g_realVersion = nullptr;
    }
}
} // namespace GakuLoader

extern "C" {

__declspec(dllexport) BOOL WINAPI GetFileVersionInfoA(LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    static auto fn = GetRealProc<decltype(&GetFileVersionInfoA)>("GetFileVersionInfoA");
    return fn ? fn(lptstrFilename, dwHandle, dwLen, lpData) : FALSE;
}

__declspec(dllexport) int WINAPI GetFileVersionInfoByHandle(int a, HANDLE b, LPVOID c, DWORD* d) {
    static auto fn = GetRealProc<int(WINAPI*)(int, HANDLE, LPVOID, DWORD*)>("GetFileVersionInfoByHandle");
    return fn ? fn(a, b, c, d) : 0;
}

__declspec(dllexport) BOOL WINAPI GetFileVersionInfoExA(DWORD dwFlags, LPCSTR lpwstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    static auto fn = GetRealProc<decltype(&GetFileVersionInfoExA)>("GetFileVersionInfoExA");
    return fn ? fn(dwFlags, lpwstrFilename, dwHandle, dwLen, lpData) : FALSE;
}

__declspec(dllexport) BOOL WINAPI GetFileVersionInfoExW(DWORD dwFlags, LPCWSTR lpwstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    static auto fn = GetRealProc<decltype(&GetFileVersionInfoExW)>("GetFileVersionInfoExW");
    return fn ? fn(dwFlags, lpwstrFilename, dwHandle, dwLen, lpData) : FALSE;
}

__declspec(dllexport) DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR lptstrFilename, LPDWORD lpdwHandle) {
    static auto fn = GetRealProc<decltype(&GetFileVersionInfoSizeA)>("GetFileVersionInfoSizeA");
    return fn ? fn(lptstrFilename, lpdwHandle) : 0;
}

__declspec(dllexport) DWORD WINAPI GetFileVersionInfoSizeExA(DWORD dwFlags, LPCSTR lpwstrFilename, LPDWORD lpdwHandle) {
    static auto fn = GetRealProc<decltype(&GetFileVersionInfoSizeExA)>("GetFileVersionInfoSizeExA");
    return fn ? fn(dwFlags, lpwstrFilename, lpdwHandle) : 0;
}

__declspec(dllexport) DWORD WINAPI GetFileVersionInfoSizeExW(DWORD dwFlags, LPCWSTR lpwstrFilename, LPDWORD lpdwHandle) {
    static auto fn = GetRealProc<decltype(&GetFileVersionInfoSizeExW)>("GetFileVersionInfoSizeExW");
    return fn ? fn(dwFlags, lpwstrFilename, lpdwHandle) : 0;
}

__declspec(dllexport) DWORD WINAPI GetFileVersionInfoSizeW(LPCWSTR lptstrFilename, LPDWORD lpdwHandle) {
    static auto fn = GetRealProc<decltype(&GetFileVersionInfoSizeW)>("GetFileVersionInfoSizeW");
    return fn ? fn(lptstrFilename, lpdwHandle) : 0;
}

__declspec(dllexport) BOOL WINAPI GetFileVersionInfoW(LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    static auto fn = GetRealProc<decltype(&GetFileVersionInfoW)>("GetFileVersionInfoW");
    return fn ? fn(lptstrFilename, dwHandle, dwLen, lpData) : FALSE;
}

__declspec(dllexport) DWORD WINAPI VerFindFileA(DWORD uFlags, LPSTR szFileName, LPSTR szWinDir, LPSTR szAppDir, LPSTR szCurDir, PUINT lpuCurDirLen, LPSTR szDestDir, PUINT lpuDestDirLen) {
    static auto fn = GetRealProc<decltype(&VerFindFileA)>("VerFindFileA");
    return fn ? fn(uFlags, szFileName, szWinDir, szAppDir, szCurDir, lpuCurDirLen, szDestDir, lpuDestDirLen) : 0;
}

__declspec(dllexport) DWORD WINAPI VerFindFileW(DWORD uFlags, LPWSTR szFileName, LPWSTR szWinDir, LPWSTR szAppDir, LPWSTR szCurDir, PUINT lpuCurDirLen, LPWSTR szDestDir, PUINT lpuDestDirLen) {
    static auto fn = GetRealProc<decltype(&VerFindFileW)>("VerFindFileW");
    return fn ? fn(uFlags, szFileName, szWinDir, szAppDir, szCurDir, lpuCurDirLen, szDestDir, lpuDestDirLen) : 0;
}

__declspec(dllexport) DWORD WINAPI VerInstallFileA(DWORD uFlags, LPSTR szSrcFileName, LPSTR szDestFileName, LPSTR szSrcDir, LPSTR szDestDir, LPSTR szCurDir, LPSTR szTmpFile, PUINT lpuTmpFileLen) {
    static auto fn = GetRealProc<decltype(&VerInstallFileA)>("VerInstallFileA");
    return fn ? fn(uFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir, szCurDir, szTmpFile, lpuTmpFileLen) : 0;
}

__declspec(dllexport) DWORD WINAPI VerInstallFileW(DWORD uFlags, LPWSTR szSrcFileName, LPWSTR szDestFileName, LPWSTR szSrcDir, LPWSTR szDestDir, LPWSTR szCurDir, LPWSTR szTmpFile, PUINT lpuTmpFileLen) {
    static auto fn = GetRealProc<decltype(&VerInstallFileW)>("VerInstallFileW");
    return fn ? fn(uFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir, szCurDir, szTmpFile, lpuTmpFileLen) : 0;
}

__declspec(dllexport) DWORD WINAPI VerLanguageNameA(DWORD wLang, LPSTR szLang, DWORD nSize) {
    static auto fn = GetRealProc<decltype(&VerLanguageNameA)>("VerLanguageNameA");
    return fn ? fn(wLang, szLang, nSize) : 0;
}

__declspec(dllexport) DWORD WINAPI VerLanguageNameW(DWORD wLang, LPWSTR szLang, DWORD nSize) {
    static auto fn = GetRealProc<decltype(&VerLanguageNameW)>("VerLanguageNameW");
    return fn ? fn(wLang, szLang, nSize) : 0;
}

__declspec(dllexport) BOOL WINAPI VerQueryValueA(LPCVOID pBlock, LPCSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen) {
    static auto fn = GetRealProc<decltype(&VerQueryValueA)>("VerQueryValueA");
    return fn ? fn(pBlock, lpSubBlock, lplpBuffer, puLen) : FALSE;
}

__declspec(dllexport) BOOL WINAPI VerQueryValueW(LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen) {
    static auto fn = GetRealProc<decltype(&VerQueryValueW)>("VerQueryValueW");
    return fn ? fn(pBlock, lpSubBlock, lplpBuffer, puLen) : FALSE;
}

} // extern "C"
