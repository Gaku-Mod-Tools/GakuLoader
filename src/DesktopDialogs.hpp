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
