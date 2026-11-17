# GakuLoader Manager bridge API reference (protocol v1)

> **Scope note (superseded for production Manager)**: Following the repository boundary correction, the production GakuLoader Manager stays native ImGui and WebView2 hosts only plugin-owned panels via `<pluginId>.web-panel/v1` ([include/GakuWebPanel.h](../include/GakuWebPanel.h)). `frontend/` now holds only the isolated Manager prototype (`src/prototype/LoaderPrototypeApp.tsx`). See [webview2-build.md](webview2-build.md) and [webview2-migration-notes.md](webview2-migration-notes.md). The framing/forwarding specification (`ManagerBridge::ForwardRequest`) remains shared with plugin web panels.

Scope: the typed request/response + event bridge between the WebView2/React Manager
frontend and the native loader host. This document describes the **implemented**
surface in this worktree (`feat/webview2-manager-ui`, base `0d18501`), verified by
full source reads on 2026-11-15. It is a reference, not an acceptance report: no
in-game or native-build validation is claimed here (see
[webview2-migration-notes.md](webview2-migration-notes.md) for verification boundaries
and [ui-migration-matrix.md](ui-migration-matrix.md) for per-feature status).

Authoritative sources:

- Core framing/lifecycle: `src/ManagerBridge.hpp`, `src/ManagerBridge.cpp`
- Backend allow-list + events: `src/ManagerBackend.hpp`, `src/ManagerBackend.cpp`
- WebView2 transport/host: `src/WebView2Ui.hpp`, `src/WebView2Ui.cpp`
- Host wiring: `src/DesktopUi.cpp` (`StartWebView`, `ResolveUiMode`, `WorkerBody` loop, `ServiceAppearance`, `WantsTextInput`, `DestroyPresentation`)
- TypeScript mirror: `frontend/src/bridge/protocol.ts`; client: `frontend/src/bridge/client.ts`; selector: `frontend/src/bridge/index.ts`

Everything below runs on the desktop UI thread. Handlers are synchronous; the native
backend stays authoritative and pushes change events instead of being polled.

## 1. Transport

- React → native: `window.chrome.webview.postMessage(<request object>)`. WebView2
  delivers it as a JSON web message; the host reads it with `get_WebMessageAsJson`
  (`src/WebView2Ui.cpp:128-143`), converts UTF-16 → UTF-8, and calls
  `ManagerBridge::Bridge::HandleMessage`.
- Native → React: `ICoreWebView2::PostWebMessageAsJson` via
  `WebView2Ui::PostMessageJson` (`src/WebView2Ui.cpp:252-258`), wired as the bridge
  event sink and as the response path in `StartWebView`'s `onMessage` callback
  (`src/DesktopUi.cpp`). The frontend listens on `window.chrome.webview`
  `message` events.
- The host ignores messages while `g_stop` is set or the backend is destroyed.
- There is no HTTP server, no WebSocket, and no remote content. The only origin is
  the virtual host below.

## 2. Wire protocol

UTF-8 JSON, one object per web message. `v` is always `1`
(`ManagerBridge::kProtocolVersion`).

### 2.1 Request (React → native)

```json
{ "v": 1, "id": 7, "type": "request", "method": "appearance.setTheme", "params": { "theme": 2 } }
```

- `id`: unsigned integer, unique per pending request (the TS client starts at 1 and
  increments). Negative or non-integer ids are rejected.
- `method`: non-empty string from the fixed allow-list (section 5). There is no
  generic command, eval, or filesystem passthrough.
- `params`: optional object. When omitted, handlers receive `{}`. When present it
  must be an object — `null` is rejected (`src/ManagerBridge.cpp:113-118`).

### 2.2 Response (native → React)

```json
{ "v": 1, "id": 7, "type": "response", "ok": true, "result": { "theme": 2 } }
```

```json
{ "v": 1, "id": 7, "type": "response", "ok": false,
  "error": { "code": "invalid_theme", "message": "Theme must be 0..4." } }
```

- Exactly one response per valid request, correlated by `id`.
- `ok: true` carries `result` (a null handler result is normalized to `{}`).
- `ok: false` carries `error { code, message, details? }`; `details` is present only
  when the handler supplied it (e.g. `hotkeys.setOverride` save failures include
  `{ "status": <GakuStatus integer> }`).

### 2.3 Event (native → React, unsolicited)

```json
{ "v": 1, "type": "event", "name": "host.snapshotChanged",
  "payload": { "available": true, "plugins": [], "pluginFiles": [],
               "runtimeReady": false, "shuttingDown": false, "activated": false },
  "seq": 3, "sid": 1 }
```

- `seq`: per-session monotonic counter, first event after a reset is `1`.
- `sid`: session id (section 3). A null payload is normalized to `{}`.
- Events are dropped while the bridge is closed or no sink is set; sink exceptions
  are swallowed (`src/ManagerBridge.cpp:131-142`).

### 2.4 Request validation order and framing errors

`HandleMessage` (`src/ManagerBridge.cpp:80-121`) validates in this order; framing
errors before the id is known respond with `id: 0`:

| Check | Failure code | Response id |
|-------|--------------|-------------|
| Message parses as JSON | `invalid_json` | 0 |
| Top level is an object | `invalid_request` | 0 |
| `type` present and a string | `invalid_request` | 0 |
| `type != "request"` | *(silently ignored, no response)* | — |
| `id` present | `invalid_request` | 0 |
| `id` integer and non-negative | `invalid_request` | 0 |
| `v` present and integer | `invalid_request` | request id |
| `v == 1` | `unsupported_protocol` | request id |
| `method` present, string, non-empty | `invalid_request` | request id |
| `params` absent or object | `invalid_request` | request id |

Dispatch-level errors (`src/ManagerBridge.cpp:62-78`):

| Condition | Code |
|-----------|------|
| Bridge closed (after `Invalidate`) | `shutting_down` |
| Method not registered | `unknown_method` |
| Handler threw `std::exception` | `internal_error` (message = `what()`) |
| Handler threw anything else | `internal_error` ("Unknown handler exception.") |

**Parameter errors surface as `internal_error`.** The backend's `RequireString` /
`RequireBool` / `RequireInt` helpers throw `std::runtime_error`
(`src/ManagerBackend.cpp:43-59`), so e.g. `shell.selectPanel` without `id` returns
`internal_error` with message `Missing string parameter: id` — not `invalid_request`.
Typed range checks return their own codes (`invalid_theme`, `invalid_scale`,
`invalid_language`, `invalid_combo`).

## 3. Sessions, sequencing, lifecycle

### 3.1 Session id (`sid`)

- `Bridge::Reset()` increments `sessionId` (first session = 1), resets `seq` to 0,
  reopens the bridge, and re-registers the two core methods. Backend handlers
  registered by `ManagerBackend` persist across resets (`src/ManagerBridge.cpp:37-46`).
- `Reset()` is called by `Backend::OnDocumentReady()`, which fires on every
  successful `NavigationCompleted` of the entry document
  (`src/WebView2Ui.cpp:157-164`, `src/DesktopUi.cpp` `onDocumentReady`). A document
  reload therefore starts a fresh session; stale responses/events from the previous
  document carry an old `sid` and are dropped by the TS client.

### 3.2 Handshake and initial state

`OnDocumentReady` (`src/ManagerBackend.cpp:434-446`) emits, in order:

1. `shell.ready` `{ mode, managerPanelId }`
2. `host.snapshotChanged` (forced; skipped only if the snapshot callback fails)
3. `appearance.changed` (forced; skipped only if no appearance callback)
4. `shell.panelSelected` (forced)
5. `shell.saveFailed` (forced)
6. `language.changed`
7. `strings.changed`
8. `hotkeys.rowsChanged`
9. `settings.changed`

The frontend does not rely on this push alone: `store.start()` awaits
`bridge.hello`, then issues seven parallel queries (`host.snapshot`,
`appearance.get`, `language.get`, `strings.get`, `hotkeys.rows`, `settings.get`,
`shell.state`) and subscribes to change events (`frontend/src/state/store.ts`).

### 3.3 Per-loop maintenance (`Tick`)

`Backend::Tick()` runs on every desktop UI loop pass in **both** hybrid branches
(web manager active and ImGui fallback) (`src/DesktopUi.cpp` `WorkerBody`):

1. `PollCapture()` — native hotkey capture scan (section 8).
2. `EmitSnapshot(false)` — emits `host.snapshotChanged` only when a signature
   changes: `runtimeReady`/`shuttingDown`/`activated` flags + per-plugin
   `id:name:version:activated/runtimeReady/failed/hasPanel` + the `pluginFiles`
   list (`src/ManagerBackend.cpp:382-398`).
3. `EmitAppearance(false)` — emits `appearance.changed` when `theme`, `uiScale`
   (epsilon 0.001) or `scaleLimited` change. **`dpiScale` is not compared** — a
   DPI-only change emits nothing (deviation D1; `src/ManagerBackend.cpp:400-409`).
4. `EmitSelection(false)` — emits `shell.panelSelected` when the selected id changes.
5. `EmitSaveFailed(false)` — emits `shell.saveFailed` when the banner flag changes.

### 3.4 Shutdown

`Backend::Shutdown()` (`src/ManagerBackend.cpp:448-451`), called from
`DestroyPresentation` before the WebView2 controller and ImGui/D3D teardown:

1. `EndCapture(true)` — if capturing: `hotkeys.captureStateChanged {capturing:false}`
   then `hotkeys.captureCancelled {}`; releases the native capture latch.
2. `Bridge::Invalidate("host_shutdown")` — emits `bridge.closed { reason: "host_shutdown" }`
   (the last event), marks the bridge closed; every later request returns
   `shutting_down`, and further `EmitEvent` calls are no-ops. `Invalidate` is idempotent.

## 4. Security model

- **Trusted origin only.** Assets are served from the virtual host
  `https://gakuloader.invalid/` (`WebView2Ui::kVirtualHost`), mapped to the local
  asset folder with `COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW`
  (`src/WebView2Ui.cpp:122-126`). Entry: `https://gakuloader.invalid/index.html`.
- **Web messages** are accepted only when `WebMessageReceived`'s `Source` starts
  with `https://gakuloader.invalid/` (case-insensitive prefix compare,
  `src/WebView2Ui.cpp:80-86,128-143`). Anything else is dropped before parsing.
- **Navigation** to any URI outside that prefix is cancelled in `NavigationStarting`
  (`src/WebView2Ui.cpp:145-155`). No remote content can load.
- **Fixed allow-list.** `Bridge::Register` is the only way to add a method; the
  backend registers exactly the 22 methods in section 5 plus the 2 core methods.
  No plugin pointers, hook leases, live plugin vector, or raw filesystem access is
  exposed; host state is read only through the non-blocking `SnapshotDesktopHost`
  copy and the existing native owners (`Hotkeys`, `LanguageSettings`, INI helpers).
- **Hardened settings** (`src/WebView2Ui.cpp:95-106`): default context menus off,
  status bar off, zoom control off, browser accelerator keys off; DevTools enabled
  only when the environment variable `GAKULOADER_WEBVIEW_DEVTOOLS=1` is set at init
  (non-test builds). `AllowExternalDrop` is FALSE so web content cannot swallow
  Explorer drops (`src/WebView2Ui.cpp:116-120`).
- **User data** lives in `<gameRoot>/GakuLoader/WebView2`; the loader DLL
  (`WebView2Loader.dll`) is resolved beside the proxy `version.dll`, not via the
  default DLL search order (`src/WebView2Ui.cpp:54-78`).

## 5. Method reference (24)

Conventions: "params" lists required fields unless marked optional. Missing
required fields fail with `internal_error` (section 2.4). "Events" lists what the
handler emits synchronously; additional change events may follow on the next
`Tick`. All methods return `shutting_down` once the bridge is closed.

### 5.1 Core (registered by `Bridge::Reset`)

#### `bridge.hello`
- Params: none. Result: `{ "protocol": 1, "sessionId": <current sid> }`.
- Handshake; the TS client adopts `sessionId` from this response.

#### `bridge.ping`
- Params: none. Result: `{ "pong": true }`. Liveness probe.

### 5.2 Queries

#### `host.snapshot`
- Params: none. Result: `HostSnapshot` (section 7).
- Reads `SnapshotDesktopHost` (atomic shared_ptr copy). Plugin `version` is
  pre-formatted by the native `ManagerStrings::FormatVersion` (`v` prefix iff the
  first char is a digit; versions containing `+` verbatim).
- If the snapshot callback is absent/fails:
  `{ available: false, plugins: [], pluginFiles: [], runtimeReady: false,
  shuttingDown: <live atomic or false>, activated: false }`
  (`src/ManagerBackend.cpp:172-179`).

#### `hotkeys.rows`
- Params: none. Result: `{ "rows": HotkeyRow[] }` from `Hotkeys::Snapshot()`
  (locked registry copy). `combo` is `""` when unbound; `overridden` is included
  but not rendered by the current UI.

#### `settings.get`
- Params: none. Result: `Settings`. Reads the INI live: `[host] console` (default
  false), `[diagnostics] trace` (default false), and one
  `{ filename, enabled }` per `snapshot.pluginFiles` entry from `[plugins] <filename>`
  (default true). No snapshot → `plugins: []`.

#### `appearance.get`
- Params: none. Result: `Appearance` (`theme`, `uiScale`, `scaleLimited`, `dpiScale`).
  No appearance callback → `{ theme: 0, uiScale: 1.0, scaleLimited: false, dpiScale: 1.0 }`.

#### `language.get`
- Params: none. Result: `Language` (`language` index 0-3, `tag`, `name`, `names[4]`).

#### `strings.get`
- Params: none. Result: `{ "language": <index>, "strings": { <key>: <localized text> } }`.
  Exactly **65 keys** — `kStringKeys` covers the full `ManagerStrings::Key` enum
  (`src/ManagerBackend.cpp:61-95`), including the four dormant keys (`ConfigFile`,
  `Saved`, `DefaultBinding`, `CustomBinding`).

#### `config.path`
- Params: none. Result: `{ "path": <UTF-8 config file path> }`
  (`<gameRoot>/GakuLoader/Configs/gakuloader.ini`).

#### `shell.state`
- Params: none. Result: `ShellState`:
  `{ selectedPanel, managerPanelId, mode, webviewReady, saveFailed }`.
  `mode` is the resolved configuration mode (`"webview2"` or `"imgui"`), not the
  effective surface; `webviewReady` reports whether the view actually initialized.
  With the webview unavailable, `mode` can read `"webview2"` while `webviewReady`
  is false and ImGui is rendering.

### 5.3 Shell commands

#### `shell.hide`
- Params: none. Result: `{}`.
- Posts `WM_CLOSE` to the control window (`ctx.hideWindow`), i.e. the exact native
  hide-to-tray / minimize-fallback path (WD-03). Always ok, even without a callback.

#### `shell.selectPanel`
- Params: `{ "id": string }`. Result: `{ "selectedPanel": <id> }`.
- Calls `SelectPanelInternal`: empty id → manager panel; clears the file-drop
  mailbox; closes the ImGui hotkey editor when moving to a plugin panel. Emits
  `shell.panelSelected` (forced).
- **No validation** of activated/hasPanel/failed (deviation D6); the original
  plugin-ABI `RequestShowPanel` validation is unchanged and still applies to
  plugin-initiated show requests. Does not end backend capture (deviation D2).

#### `shell.setTextInputActive`
- Params: `{ "active": boolean }`. Result: `{}`.
- Sets `g_webWantsTextInput`. While the web manager owns the client area,
  `DesktopUi::WantsTextInput()` returns this flag, so the hotkey poller suppresses
  all plugin hotkeys and F1 while a React editable element has focus
  (`frontend/src/lib/appHooks.ts` reports focusin/focusout).

### 5.4 Appearance commands

#### `appearance.setTheme`
- Params: `{ "theme": integer 0..4 }` (Dark, Sapphire, Paper & Ink, Apple Light, Apple Dark).
- Result: `{ "theme": <theme> }`. Errors: `invalid_theme` (out of range),
  `apply_failed` (apply hook rejected; the production hook always accepts).
- Marks appearance dirty + queues the INI save; the shared `ServiceAppearance`
  applies it (keeping the ImGui plugin surface and `GakuAppearanceService` in sync)
  and performs the deferred save. `appearance.changed` follows on the next `Tick`;
  a failed save surfaces via `shell.saveFailed`.

#### `appearance.setScale`
- Params: `{ "percent": integer 50..300 }`. Result: `{ "percent": <percent> }`.
  Errors: `invalid_scale`, `apply_failed`.
- Sets `uiScale = percent/100`, resets the DPI component to the window DPI, marks
  fonts + appearance dirty, queues the save. The atlas-limit revert still applies
  to the ImGui plugin surface: a rejected font build reverts to the last applied
  scale and sets `scaleLimited`, pushed via `appearance.changed`. The React side
  applies `uiScale` as CSS `zoom` (DPI is handled by WebView2 rasterization).

### 5.5 Language command

#### `language.set`
- Params: `{ "language": integer 0..3 }` (en, zh-Hans, zh-Hant, ja).
- Result: `Language`. Errors: `invalid_language` (out of range), `save_failed`
  (INI write failed; language state unchanged).
- Calls `LanguageSettings::Save` (write-then-publish) and refreshes the tray tooltip
  on success. Always emits `shell.saveFailed { failed: !saved }`; on success also
  `language.changed` and `strings.changed`. Polling plugins see the new language
  immediately through the unchanged `gakuloader.language` ABI.

### 5.6 Hotkey commands

All hotkey writes go through the native owners (`Hotkeys::SetOverride` /
`ResetOverride`), which write the INI **first** and only then mutate the live
registry; failure leaves live state untouched. Every write emits
`shell.saveFailed { failed }` and `hotkeys.rowsChanged` (even on failure).

#### `hotkeys.setOverride`
- Params: `{ "pluginId": string, "actionId": string }` plus **either**
  `"combo": string` **or** `"virtualKey": integer` (+ optional `"modifiers": integer`,
  default 0). `combo` wins when both are present.
  - `combo: ""` → unbind (vk 0, mods 0).
  - Non-empty combo is parsed by the native `Hotkeys::ParseCombo` (grammar in
    [ui-migration-matrix.md](ui-migration-matrix.md) §10.5); parse failure or F1 →
    `invalid_combo`.
  - `virtualKey` path: F1 → `invalid_combo`; other validation (range, modifier-only
    keys) happens inside `SetOverride` and surfaces as `save_failed` with a status.
    A non-integer `modifiers` value fails as `internal_error` (JSON type error).
  - Neither combo nor virtualKey → `invalid_request` ("Provide combo or virtualKey.").
- Result: `{ "virtualKey": <vk>, "modifiers": <mods> }`.
- Errors: `invalid_combo`; `save_failed` with `details: { "status": <GakuStatus> }`
  where status is `GAKU_E_INVALID_ARGUMENT` (-1), `GAKU_E_CONFLICT` (-5),
  `GAKU_E_UNAVAILABLE` (-4, unknown row), or `GAKU_E_INTERNAL` (-8, INI write failed)
  (`src/Hotkeys.cpp:396-418`, `include/GakuPlugin.h:54-62`).
- Successful writes latch the edited binding (`down = true`) so the captured press
  cannot dispatch.

#### `hotkeys.unbind`
- Params: `{ "pluginId": string, "actionId": string }`. Result: `{}`.
- `SetOverride(vk 0, mods 0)` — writes an empty INI value. Errors: `save_failed`
  (no details).

#### `hotkeys.resetOverride`
- Params: `{ "pluginId": string, "actionId": string }`. Result: `{}`.
- `Hotkeys::ResetOverride` — writes `<default>` for the exact v2 identity key
  (masks legacy aliases, preserves old INI entries), restores the plugin default.
  Errors: `save_failed` (no details; underlying statuses: F1 default → conflict,
  invalid default → invalid-argument, unknown row → unavailable, write failure →
  internal).

#### `hotkeys.beginCapture`
- Params: `{ "pluginId": string, "actionId": string, "combo"?: string }`
  (current field text, used as the prefill; a non-string `combo` → `internal_error`).
- Result: `{ "capturing": true }`.
- Seeds all 256 `capDown_` entries from `GetAsyncKeyState` (already-held keys are
  not treated as new presses), calls `Hotkeys::SetCaptureActive(true)` (suppresses
  all plugin dispatch + F1), emits `hotkeys.captureStateChanged { capturing: true,
  pluginId, actionId }`. Calling it again while capturing restarts/re-seeds.

#### `hotkeys.endCapture`
- Params: none. Result: `{}`.
- `EndCapture(true)`: releases the capture latch (every binding latched down so the
  captured press never dispatches), emits `hotkeys.captureStateChanged
  { capturing: false, ... }` and `hotkeys.captureCancelled {}`. No-op when not
  capturing.

### 5.7 Settings commands

All three write the INI directly (`WritePrivateProfileStringW`) and then call
`PushSettingsResult(ok, key)`, which emits **in order**:
`shell.saveFailed { failed: !ok }`, `settings.saveResult { ok, key }`,
`settings.changed` (a fresh `SettingsJson` re-read, so the frontend always gets the
authoritative value — this is what corrects optimistic checkbox state after a
failed write). Effects are next-game-start only.

#### `settings.setConsole`
- Params: `{ "enabled": boolean }`. Result: `{ "console": <enabled> }`.
  Writes `[host] console`. Errors: `save_failed`. `settings.saveResult.key = "console"`.

#### `settings.setTrace`
- Params: `{ "enabled": boolean }`. Result: `{ "trace": <enabled> }`.
  Writes `[diagnostics] trace`. Errors: `save_failed`. `key = "trace"`.

#### `settings.setPluginEnabled`
- Params: `{ "filename": string, "enabled": boolean }`.
  Result: `{ "filename": <filename>, "enabled": <enabled> }`.
  Writes `[plugins] <filename>`. Errors: `save_failed`. `key = <filename>`.
  No validation that the filename was discovered (the INI accepts any key; a typo
  simply has no effect, matching the native default-true semantics).

## 6. Event reference (14)

| Event | Payload | Emitted when |
|-------|---------|--------------|
| `shell.ready` | `{ mode, managerPanelId }` | `OnDocumentReady` only (fresh session). The store currently ignores it (D7). |
| `shell.panelSelected` | `{ id }` | Selection change (`Tick` diff), forced after `shell.selectPanel`, forced at document ready. |
| `host.snapshotChanged` | `HostSnapshot` | Signature change in `Tick` (lifecycle flags, plugin entries, discovered files); forced at document ready. |
| `appearance.changed` | `Appearance` | Theme/scale/scaleLimited change in `Tick` (D1: not dpiScale-only); forced at document ready. |
| `language.changed` | `Language` | `language.set` success; document ready. |
| `strings.changed` | `Strings` | `language.set` success; document ready. |
| `hotkeys.rowsChanged` | `{ rows: HotkeyRow[] }` | Every `setOverride`/`unbind`/`resetOverride` (success or failure); document ready. |
| `hotkeys.captureStateChanged` | `{ capturing, pluginId, actionId }` | Capture begin, end (any path), and capture completion. |
| `hotkeys.captured` | `{ combo, invalid }` | A new non-modifier key edge was captured; `invalid` is true for reserved F1. |
| `hotkeys.captureCancelled` | `{}` | Capture ended without a result: `endCapture`, Esc, foreground loss, shutdown. |
| `settings.changed` | `Settings` | Every settings write (`PushSettingsResult`); document ready. Not emitted on discovery changes (D10). |
| `settings.saveResult` | `{ ok, key }` | Settings writes only (console/trace/plugin filename). Not emitted for hotkey/language writes. |
| `shell.saveFailed` | `{ failed }` | Any write result (settings/hotkeys/language), plus `Tick` diff of the banner flag; forced at document ready. Sticky until a later success clears it. |
| `bridge.closed` | `{ reason }` | `Invalidate`; production reason `"host_shutdown"`. Last event of a session. |

## 7. Shared data shapes

Mirrored 1:1 by `frontend/src/bridge/protocol.ts`.

```ts
interface PluginPanel {
  id: string; name: string; version: string;   // version pre-formatted (FormatVersion)
  activated: boolean; runtimeReady: boolean; failed: boolean; hasPanel: boolean;
}
interface HostSnapshot {
  available: boolean; plugins: PluginPanel[]; pluginFiles: string[];
  runtimeReady: boolean; shuttingDown: boolean; activated: boolean;
}
interface Appearance { theme: number; uiScale: number; scaleLimited: boolean; dpiScale: number }
interface Language { language: number; tag: string; name: string; names: string[] }
interface Strings { language: number; strings: Record<string, string> }  // 65 keys
interface HotkeyRow {
  pluginId: string; actionId: string; displayName: string;
  combo: string;            // "" when unbound; canonical "Ctrl+Shift+Alt+Win+<KEY>"
  virtualKey: number; modifiers: number;   // GAKU_HOTKEY_MOD_CTRL=1 SHIFT=2 ALT=4 WIN=8
  conflicted: boolean; unbound: boolean; overridden: boolean;
}
interface Settings { console: boolean; trace: boolean; plugins: { filename: string; enabled: boolean }[] }
interface ShellState {
  selectedPanel: string; managerPanelId: string; mode: string;
  webviewReady: boolean; saveFailed: boolean;
}
interface CaptureState { capturing: boolean; pluginId: string; actionId: string }
```

Manager panel id: `gakuloader.manager`. Theme indices: 0 Dark, 1 Sapphire,
2 Paper & Ink, 3 Apple Light, 4 Apple Dark. Language indices: 0 en, 1 zh-Hans,
2 zh-Hant, 3 ja.

## 8. Native capture subsystem

Capture stays native because DOM key events cannot reproduce global async key
state or the dispatch-suppression latch (`src/ManagerBackend.cpp:328-380`):

- `BeginCapture` seeds held keys and asserts `Hotkeys::SetCaptureActive(true)`.
- `PollCapture` runs every UI loop pass while capturing:
  - Foreground window ≠ control HWND → `EndCapture(true)` (cancel).
  - Re-asserts `SetCaptureActive(true)` each pass so a focus message that cleared
    capture self-heals (mirrors the ImGui modal behavior).
  - Reads live Ctrl/Shift/Alt/Win modifiers; scans VK 8..255 for a new-down edge,
    skipping modifier-only keys (including the `VK_LSHIFT..VK_RMENU` range and
    LWIN/RWIN).
  - Esc → cancel. Otherwise formats via `Hotkeys::ComboName`, round-trips through
    `ParseCombo` (unparseable names keep capturing), stops capture, releases the
    latch, and emits `captureStateChanged {capturing:false}` + `captured
    {combo, invalid: key==F1}`.
- The captured combo is **not** written automatically; the editor applies it via
  `hotkeys.setOverride`. F1 arrives with `invalid: true` and the editor shows the
  invalid-combo message.
- Known gap (D2): selecting a plugin panel does not end backend capture; it keeps
  scanning while the host is foreground even though the webview is hidden.

## 9. Frontend client behavior

`frontend/src/bridge/client.ts` (`WebView2BridgeClient`):

- Correlates responses by `id`; unknown ids are ignored.
- Default timeout **15 s** per request (`timeoutMs` override) → `BridgeError`
  code `timeout`. `AbortSignal` → code `cancelled`; late responses for a
  cancelled/timed-out id resolve nothing.
- Session handling: adopts `sid` from `bridge.hello` (or the first event); events
  with an older `sid` are dropped; a newer `sid` rejects all pending requests with
  code `reset` and adopts the new session.
- `bridge.closed` → rejects pending with the event's reason, marks the client
  closed; further requests reject with code `closed`. `pagehide` closes the client.
- Client-side error codes: `no_webview`, `closed`, `timeout`, `cancelled`, `reset`,
  plus whatever the native `error.code` carries (`unknown` fallback).

`frontend/src/bridge/index.ts`: `getBridge()` returns the WebView2 client when
`window.chrome.webview` exists, otherwise a stateful `MockBridgeClient`
(`frontend/src/bridge/mock.ts`, 3 seeded plugins / 5 hotkeys / English strings) so
the Vite dev server and tests stay fully interactive. `setBridge()` injects a
client for tests. The mock is never used inside the game: the production host
always provides `window.chrome.webview`.

## 10. Wire examples

Handshake (session 1):

```json
→ { "v": 1, "id": 1, "type": "request", "method": "bridge.hello", "params": {} }
← { "v": 1, "id": 1, "type": "response", "ok": true, "result": { "protocol": 1, "sessionId": 1 } }
```

Theme change (response, then the change event on the next Tick):

```json
→ { "v": 1, "id": 4, "type": "request", "method": "appearance.setTheme", "params": { "theme": 1 } }
← { "v": 1, "id": 4, "type": "response", "ok": true, "result": { "theme": 1 } }
← { "v": 1, "type": "event", "name": "appearance.changed",
    "payload": { "theme": 1, "uiScale": 1.0, "scaleLimited": false, "dpiScale": 1.0 },
    "seq": 12, "sid": 1 }
```

Hotkey override by combo text, and the reserved-F1 rejection:

```json
→ { "v": 1, "id": 9, "type": "request", "method": "hotkeys.setOverride",
    "params": { "pluginId": "gakumotion", "actionId": "play-pause", "combo": "Ctrl+P" } }
← { "v": 1, "id": 9, "type": "response", "ok": true, "result": { "virtualKey": 80, "modifiers": 1 } }
← { "v": 1, "type": "event", "name": "shell.saveFailed", "payload": { "failed": false }, "seq": 18, "sid": 1 }
← { "v": 1, "type": "event", "name": "hotkeys.rowsChanged", "payload": { "rows": [ "..." ] }, "seq": 19, "sid": 1 }

→ { "v": 1, "id": 10, "type": "request", "method": "hotkeys.setOverride",
    "params": { "pluginId": "gakumotion", "actionId": "play-pause", "combo": "F1" } }
← { "v": 1, "id": 10, "type": "response", "ok": false,
    "error": { "code": "invalid_combo", "message": "Invalid combo or reserved F1." } }
```

Settings write failure (authoritative `settings.changed` corrects the optimistic
checkbox; banner sticks):

```json
→ { "v": 1, "id": 12, "type": "request", "method": "settings.setConsole", "params": { "enabled": true } }
← { "v": 1, "type": "event", "name": "shell.saveFailed", "payload": { "failed": true }, "seq": 24, "sid": 1 }
← { "v": 1, "type": "event", "name": "settings.saveResult", "payload": { "ok": false, "key": "console" }, "seq": 25, "sid": 1 }
← { "v": 1, "type": "event", "name": "settings.changed",
    "payload": { "console": false, "trace": false, "plugins": [ "..." ] }, "seq": 26, "sid": 1 }
← { "v": 1, "id": 12, "type": "response", "ok": false,
    "error": { "code": "save_failed", "message": "Could not save the configuration." } }
```

(Events are emitted inside the handler before the response is posted, so they
arrive first — the client correlates the response by id regardless of order.)

TypeScript usage (matches `frontend/src/bridge/protocol.ts`):

```ts
import { getBridge } from '@/bridge'

const bridge = getBridge()
const { sessionId } = await bridge.request('bridge.hello')
const off = bridge.on('hotkeys.rowsChanged', (p) => renderRows(p.rows))
try {
  await bridge.request('hotkeys.setOverride', {
    pluginId: 'gakumotion', actionId: 'play-pause', combo: 'Ctrl+P',
  })
} catch (error) {
  // error is a BridgeError with .code ('invalid_combo' | 'save_failed' | ...)
} finally {
  off()
}
```

## 11. Known limitations

Deviations D1-D12 are indexed in [ui-migration-matrix.md](ui-migration-matrix.md)
("Implementation status & evidence") and detailed with file:line in
[webview2-migration-notes.md](webview2-migration-notes.md). The ones most relevant
to bridge consumers: D1 (no DPI-only `appearance.changed`), D2 (capture survives
panel switch), D6 (`shell.selectPanel` unvalidated), D7 (no reconnect after
`bridge.closed`), D10 (`settings.changed` not emitted on discovery changes).

Surfaces proposed in the original inventory but **not** implemented:
`shell.showPanel`, `shell.activationChanged`, `shell.dpiChanged`,
`shell.renderFailed`, `appearance.scaleLimited`, `fileDrop.hostFlags`,
`panel.busyChanged`, `fileDrop.dragChanged`, `hotkeys.openEditor`,
`log.tail` / `log.recordsAppended`. Do not code against them.
