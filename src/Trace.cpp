// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

#include "Trace.hpp"
#include "Host.hpp"

#include <windows.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>

namespace GakuLoader::Trace {

namespace {

std::atomic_bool g_enabled{false};
std::atomic<uint64_t> g_sequence{0};

}

unsigned long long UptimeMs() {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0;
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER start{}, current{};
    start.LowPart = created.dwLowDateTime;
    start.HighPart = created.dwHighDateTime;
    current.LowPart = now.dwLowDateTime;
    current.HighPart = now.dwHighDateTime;
    if (current.QuadPart <= start.QuadPart) return 0;
    return (current.QuadPart - start.QuadPart) / 10000ULL;
}

void Configure(bool enabled) { g_enabled.store(enabled, std::memory_order_release); }
bool Enabled() { return g_enabled.load(std::memory_order_acquire); }
uint64_t Next() { return g_sequence.fetch_add(1, std::memory_order_relaxed) + 1; }

void Event(const char* format, ...) {
    if (!Enabled() || !format) return;
    char message[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    LogInfo("[Trace] seq=%llu t=%llums tid=%lu %s",
        static_cast<unsigned long long>(Next()), UptimeMs(),
        static_cast<unsigned long>(GetCurrentThreadId()), message);
}

}
