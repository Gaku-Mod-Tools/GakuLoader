// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

#pragma once
#include <cstdint>

namespace GakuLoader::Trace {

// Off by default. The acceptance run enables it from gakuloader.ini:
//
//   [diagnostics]
//   trace=true
//
// It exists to answer one question with process-local evidence: in what order
// did the shared actor entry, the game original, the frame tick and the actor
// tick actually run. The sequence counter is monotonic across both producer
// threads, so log line order is not the only proof.
void Configure(bool enabled);
bool Enabled();

// Monotonic ordering token. Consumed by Event() itself; exposed so a caller
// can pair a begin/end measurement with the same ordinal.
uint64_t Next();

// No-op unless enabled. Emits "[Trace] seq=<n> t=<process uptime ms> tid=<id>".
void Event(const char* format, ...);

// Milliseconds since process creation; 0 when unavailable.
unsigned long long UptimeMs();

}
