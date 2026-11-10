// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

#pragma once
#include "GakuPlugin.h"

namespace GakuLoader::DesktopDialogs {
// Copies the owner-bound table. Requests never invoke a modal synchronously.
GakuFileDialogService ServiceForOwner(void* owner);
// Called only by the desktop STA thread, outside ImGui and plugin callback locks.
void Pump(void* nativeWindow);
void CancelOwner(void* owner);
// Thread-safe cancellation; COM Close is performed on the dialog's own thread.
void Shutdown();
}
