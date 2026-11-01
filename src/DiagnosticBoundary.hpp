#pragma once
#include <stdint.h>

namespace GakuLoader {
// A narrow replacement for Localify's Unity diagnostic interception. It never
// calls the official Unity log sink, invokes managed Exception.ToString, or
// changes network/security/reporting subsystems.
bool InstallDiagnosticBoundary();
// Successful isolation is process lifetime; failed startup rolls back privately.
struct DiagnosticBoundaryCounts { uint64_t messages; uint64_t exceptions; };
DiagnosticBoundaryCounts GetDiagnosticBoundaryCounts();
}
