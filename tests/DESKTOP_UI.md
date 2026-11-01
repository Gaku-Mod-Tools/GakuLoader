# Standalone desktop host verification

```powershell
.\tools\test-desktop-ui.ps1
.\tools\test-desktop-ui-cross-dll.ps1
.\tools\test-desktop-dialogs.ps1
```

The first command compiles the production `DesktopUi.cpp` and `DesktopDialogs.cpp`
with a test-only WARP configuration. A hidden synthetic `UnityWndClass` window is
present only to verify that the production desktop host leaves its window
procedure and close lifecycle untouched. The host creates its own top-level HWND,
UI thread, D3D11 device, context, swapchain and render target. There is no DXGI
hook-discovery or detour adapter in this executable.

The smoke reads real pixels from the independent desktop backbuffer, tests exact
ImGui ABI and allocator negotiation, Win32 keyboard input, resizing and render
target recreation, asynchronous show/reopen, close-to-tray (or minimized taskbar
fallback), UI-thread teardown and unavailable bindings outside the selected plugin
callback. It also forces a real window-registration failure to verify queued
dialogs cancel and subsequent requests are rejected. A short show/reopen check
may briefly display the test controls window.
No game, Localify, VRify, GameAssembly or injected plugin is loaded.

Controlled frame-admission denial runs twelve or more production render-loop
iterations without advancing CPU frame, draw or Present counters, then resumes
complete pixel-verified frames. The WARP executable's admission adapter forces
denial; actual mutex contention is verified separately with the real ModLoader
and DLL fixture in `tests/test-host.ps1`. That test holds a UI callback with a
barrier, confirms the waiting actor update executes, rejects a complete desktop
CPU frame while an update owns the gate, and verifies exactly 64 updates for each
of two actors during contention with no overlapping callbacks. Shared actor-entry
participants join the same gate: with a desktop frame holding it, the participant
does not enter until that frame completes, the game original still runs exactly
once, and two later actors each reach the participant in order. This division is
not a claim of real-game GPU or character acceptance.

The clipboard backend is injected in the WARP test so the user's system clipboard
is never overwritten. A delayed fetch must occur outside CPU/plugin frame scopes,
the first paste sees fresh text, unchanged sequences avoid repeated native reads,
and copy writes flush after leaving the frame gate. Native file-dialog/host
contention is exercised separately rather than inferred from this simulated
clipboard owner.

The second command builds an independent MSVC `/MT` DLL with its own static ImGui
core. It binds the MinGW host's context and allocator, creates persistent widgets,
and renders a cyan marker. The production host's real WARP backbuffer must contain
both the native callback's red marker and the foreign DLL's cyan marker. This
verifies the current exact ImGui configuration across separate CRTs, not arbitrary
ImGui versions or compiler layouts.

`tests/test-host.ps1` separately verifies real modern/legacy fixture lifecycle,
selected-panel dispatch, non-blocking copied navigation snapshots, main-thread
queues, service ownership and callback serialization. `tools/test-desktop-dialogs.ps1`
independently tests the production COM service: actual Shell dialogs select
temporary VMD/WAV/.gkcamera paths containing Chinese characters and spaces, verify
owner HWND/UI thread and concurrent simulated game updates, and close real modals
via release/owner retirement/shutdown. Invalid HWND and MTA cases preserve HRESULT
failure evidence. These native fixtures establish Shell/ABI behavior; they do not
establish the installed game's file-import result.

`tools/test-host-dialog-concurrency.ps1` links the actual ModLoader, fixture DLL
and COM service. Three real Shell modals remain open while main dispatch executes
100 updates for each of two actors per modal. It requires all 600 updates,
zero callback overlap, and actual modal closure on release, owner retirement,
and shutdown. A timer callback must not rely on `IFileDialog::Close` merely returning
S_OK: the initial integration case observed success without Show returning.
Cancellation now obtains this COM object's own HWND through IOleWindow and posts
WM_CLOSE on its STA, checking owner/process/thread identity. Chromium documents
the same restriction outside IFileDialogEvents in its
[dialog lifetime implementation](https://chromium.googlesource.com/chromium/src.git/+/refs/tags/136.0.7103.95/ui/shell_dialogs/auto_close_dialog_event_handler_win.cc).

The production window can run with hardware D3D11 or WARP fallback. It currently
stops its desktop presentation on device removal/reset and reports the failure;
it does not restart the device. This leaves game rendering untouched, but desktop
recovery after device loss requires restarting the host process. Control window
close is deliberately distinct from the game's normal exit request. Plugin init
and main-thread queued work still depend on the first/next actor dispatch.

These checks establish native desktop/ABI behavior only. Actual installed GML
window visibility, focus, file selection, game playback/audio/camera and VR must
be recorded as separate runtime or user acceptance evidence.
