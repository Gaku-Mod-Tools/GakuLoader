// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

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
