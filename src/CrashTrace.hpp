// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

#pragma once
#include <filesystem>

namespace GakuLoader::CrashTrace {

// Installs a first-chance vectored handler that appends one line per fatal
// exception (access violation, stack overflow, fast fail, heap corruption) to
// the given file. Only armed when [diagnostics] trace=true: it is a locator for
// acceptance runs, not a production safety net. The handler never terminates or
// swallows anything; it records the faulting module and offset, then lets the
// normal exception path continue.
void Arm(const std::filesystem::path& logPath);

}
