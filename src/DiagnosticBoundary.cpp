// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

#include "DiagnosticBoundary.hpp"
#include "GameHook.hpp"
#include "Host.hpp"
#include <windows.h>
#include <atomic>
#include <cstring>

namespace GakuLoader {
namespace {
std::atomic<uint64_t> messages{0}, exceptions{0};
void* targets[2]{};
using StringLength = int32_t (*)(void*);
using StringChars = const wchar_t* (*)(void*);
StringLength stringLength = nullptr;
StringChars stringChars = nullptr;

// Signatures include IL2CPP's hidden MethodInfo argument, as in current Unity 6
// Localify. Deliberately do not invoke the original official diagnostic sink.
void CaptureLog(int type, int, void* text, void*, void*) {
    const auto count = messages.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count > 64) return; // bounded private detail, counts continue
    char utf8[4096]{};
    if (text && stringLength && stringChars) {
        const auto length = stringLength(text);
        const auto* chars = stringChars(text);
        if (chars && length > 0) {
            const int bounded = length > 1024 ? 1024 : length;
            const int written = WideCharToMultiByte(CP_UTF8, 0, chars, bounded,
                utf8, sizeof(utf8) - 1, nullptr, nullptr);
            if (written > 0) utf8[written] = 0;
        }
    }
    LogInfo("[UnityDiagnostic/local-only] type=%d %s", type, utf8);
}
void CaptureException(void*, void*, void*) {
    const auto count = exceptions.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count <= 8) LogWarn("[UnityDiagnostic/local-only] exception captured; official forwarding suppressed (no managed formatting)");
}

bool ExactMethod(void* assembly, void* klass, const char* name, int count,
                 const char* const* parameters, void** target) {
    using Find = void* (*)(void*, const char*, int);
    using Param = const void* (*)(const void*, uint32_t);
    using TypeName = char* (*)(const void*);
    using Result = const void* (*)(const void*);
    using Flags = uint32_t (*)(const void*, uint32_t*);
    using Free = void (*)(void*);
    auto find = reinterpret_cast<Find>(GetProcAddress((HMODULE)assembly, "il2cpp_class_get_method_from_name"));
    auto param = reinterpret_cast<Param>(GetProcAddress((HMODULE)assembly, "il2cpp_method_get_param"));
    auto typeName = reinterpret_cast<TypeName>(GetProcAddress((HMODULE)assembly, "il2cpp_type_get_name"));
    auto result = reinterpret_cast<Result>(GetProcAddress((HMODULE)assembly, "il2cpp_method_get_return_type"));
    auto flags = reinterpret_cast<Flags>(GetProcAddress((HMODULE)assembly, "il2cpp_method_get_flags"));
    auto free = reinterpret_cast<Free>(GetProcAddress((HMODULE)assembly, "il2cpp_free"));
    if (!find || !param || !typeName || !result || !flags || !free || !klass) return false;
    const void* method = find(klass, name, count);
    uint32_t implementationFlags = 0;
    if (!method || !(flags(method, &implementationFlags) & 0x10u)) return false; // static
    auto matches = [&](const void* type, const char* expected) {
        char* actual = type ? typeName(type) : nullptr;
        const bool equal = actual && std::strcmp(actual, expected) == 0;
        if (actual) free(actual);
        return equal;
    };
    if (!matches(result(method), "System.Void")) return false;
    for (int i = 0; i < count; ++i)
        if (!matches(param(method, i), parameters[i])) return false;
    *target = *static_cast<void* const*>(method);
    return *target != nullptr;
}
void RollbackDiagnosticBoundary() {
    for (auto& target : targets) if (target) {
        // Rollback is private to failed startup. Successful isolation has no
        // teardown entry point that could reopen reporting during game exit.
        if (ReleaseHostHook(target) == GAKU_OK) target = nullptr;
    }
}
}

bool InstallDiagnosticBoundary() {
    if (targets[0] && targets[1]) return true;
    const auto assembly = GetModuleHandleW(L"GameAssembly.dll");
    auto klass = ResolveIl2cppClass("UnityEngine.CoreModule.dll", "UnityEngine", "DebugLogHandler");
    const char* logParams[] = {"UnityEngine.LogType", "UnityEngine.LogOption", "System.String", "UnityEngine.Object"};
    const char* exceptionParams[] = {"System.Exception", "UnityEngine.Object"};
    void* candidates[2]{};
    if (!ExactMethod(assembly, klass, "Internal_Log", 4, logParams, &candidates[0]) ||
        !ExactMethod(assembly, klass, "Internal_LogException", 2, exceptionParams, &candidates[1]) ||
        candidates[0] == candidates[1]) {
        LogWarn("[DiagnosticBoundary] exact Unity diagnostic ABI unavailable; continuing without the local-only log boundary");
        return false;
    }
    stringLength = reinterpret_cast<StringLength>(GetProcAddress(assembly, "il2cpp_string_length"));
    stringChars = reinterpret_cast<StringChars>(GetProcAddress(assembly, "il2cpp_string_chars"));
    void* detours[] = {(void*)CaptureLog, (void*)CaptureException};
    for (int i = 0; i < 2; ++i) {
        void* unusedOriginal = nullptr;
        const auto status = AcquireProcessLifetimeHostHook(candidates[i], detours[i], &unusedOriginal);
        if (status != GAKU_OK) {
            RollbackDiagnosticBoundary();
            LogWarn("[DiagnosticBoundary] diagnostic hook acquisition failed (status=%d); continuing without the local-only log boundary", status);
            return false;
        }
        targets[i] = candidates[i];
    }
    LogInfo("[DiagnosticBoundary] active: Unity Internal_Log + Internal_LogException local-only; no official forwarding; detail limits=64/8");
    return true;
}

DiagnosticBoundaryCounts GetDiagnosticBoundaryCounts() {
    return {messages.load(std::memory_order_relaxed), exceptions.load(std::memory_order_relaxed)};
}
}
