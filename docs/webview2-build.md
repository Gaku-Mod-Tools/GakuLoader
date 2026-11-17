# GakuLoader WebView2 host build and local packaging

This guide covers building the native GakuLoader host (`version.dll`), redistributing the app-local
WebView2 SDK loader (`WebView2Loader.dll`), consuming plugin-owned web panels via `include/GakuWebPanel.h`,
and staging an offline host release. It does not deploy, start, or control a game.

The GakuLoader Manager remains ImGui. GakuLoader no longer owns, builds, or bundles GakuMotion's React
frontend or `GakuMotion.UI/`. Each plugin that chooses to render a WebView2 panel owns its own frontend
repository and ships its compiled UI assets inside its plugin package (for example,
`GakuLoader/plugins/GakuMotion.UI/`). `frontend/` in this repository retains only the isolated GakuLoader
Manager Web prototype for standalone UI reference and CI validation; it is not bundled into `version.dll`
release packages.

## Prerequisites

- Visual Studio 2022 MSVC v143 or Visual Studio 2026 MSVC v145 with the C++ desktop workload and a
  Windows SDK. The observed local environment has only v145, so local commands below pass
  `-PlatformToolset v145` explicitly.
- Vendored inputs: `deps/webview2/include`, `deps/webview2/x64/WebView2Loader.dll`, and
  `deps/nlohmann/json.hpp`. Their upstream versions, URLs, licenses, and SHA256 values are recorded in
  [WebView2 provenance](../deps/webview2/PROVENANCE.md) and
  [nlohmann/json provenance](../deps/nlohmann/PROVENANCE.md).
- Target machines need the Microsoft Edge WebView2 Evergreen Runtime installed separately. The
  vendored/packaged `WebView2Loader.dll` is the SDK loader, not the browser runtime.
- Node.js and npm on `PATH` only if you want to run or validate the isolated Manager Web prototype under
  `frontend/`. Packaging the native host does not require Node.js.

## Native build

```powershell
.\build.ps1 -Configuration Release -PlatformToolset v145
.\build.ps1 -Configuration Debug -PlatformToolset v145
```

Release writes `build/version.dll`; Debug writes `build/debug/version.dll`. Both copy the vendored
`WebView2Loader.dll` beside the built `version.dll`. The existing optional `-OutputDll` candidate copy
also copies `WebView2Loader.dll` beside that candidate. The script does not locate a game directory or
deploy automatically.

## Local host release package

Build the native host and package it:

```powershell
.\tools\package-release.ps1 -Configuration Release -PlatformToolset v145 -BuildNative
```

Or package an already-built `version.dll`:

```powershell
.\tools\package-release.ps1 -Configuration Release -PlatformToolset v145
```

The default output is a fresh directory under `build/release/`. `-OutputDir` may name another new
child directory under `build/release`, but existing directories are never overwritten and output
paths/reparse points that escape the repository staging root are rejected.

The package contains only host artifacts:

```text
<package>/
|-- version.dll
|-- WebView2Loader.dll
|-- INSTALL.txt
|-- package-manifest.json
|-- THIRD_PARTY_NOTICES.md
|-- LICENSE
|-- LICENSES/...
`-- deps/...                  retained license and provenance records only
```

Before writing the manifest, the script checks the native DLL with `tools/test-package.ps1` (x64,
version proxy exports, no dynamic CRT), checks `WebView2Loader.dll`'s required dynamic-load exports,
and compares every copied payload file's SHA256 with its source. The manifest records payload hashes,
source HEAD, dirty-worktree state, build invocation flags, resolved toolset, and input hashes. It
excludes plugins, plugin web UI assets (such as `GakuMotion.UI/`), PDBs, `node_modules`, source maps,
game assets, personal configuration, and the Evergreen Runtime. Packaging still requires MSVC/dumpbin for
the native contract checks even when reusing a prebuilt DLL.

`package-manifest.json` is the completion marker. A failed run can leave an incomplete fresh staging
directory; do not install from a directory without a verified manifest. The package is unsigned local
staging output, not a publication or bitwise-reproducibility attestation.

## Plugin web panel contract and manual install layout

GakuLoader discovers plugin web UIs strictly through the additive service contract in `include/GakuWebPanel.h`:

1. A plugin registers `<pluginId>.web-panel/v1` (`GakuWebPanelServiceV1`).
2. When the user selects that plugin in the ImGui Manager, `DesktopUi` calls `getAssets` (`GakuWebPanelAssetsV1`)
   to read the plugin-supplied `assetsDir`, relative `entryPath`, and `virtualHost`.
3. `WebView2Ui` creates a per-panel WebView2 controller scoped to `https://<virtualHost>/` mapped to
   `assetsDir`, and forwards JSON bridge requests/events between `window.chrome.webview` and the plugin's
   `handleMessage` / `setEventSink`.

Close the game and independently back up existing host files before any manual replacement. Keep
`version.dll` and `WebView2Loader.dll` beside `gakumas.exe`; plugins and their web UI directories live
under `GakuLoader/plugins/`:

```text
<game root>/
|-- gakumas.exe
|-- version.dll
|-- WebView2Loader.dll
`-- GakuLoader/
    |-- Configs/gakuloader.ini
    |-- Logs/gakuloader.log
    |-- WebView2/              browser user data, created at runtime
    `-- plugins/               plugin DLLs and plugin-owned UI folders (e.g. GakuMotion.dll + GakuMotion.UI/)
```

The Manager remains ImGui. Plugin web panels are enabled by default when a plugin provides a
`<pluginId>.web-panel/v1` service and the runtime/assets are available. The optional host setting is:

```ini
[host]
ui=webview2
```

Use `ui=imgui` to disable plugin web panels and keep legacy ImGui plugin panels. If the Evergreen
Runtime, app-local `WebView2Loader.dll`, or the plugin-supplied entry document is unavailable, the host
logs a warning and falls back to the plugin's ImGui panel inside the Manager content area. It never falls
back to the game window. Changing the host UI mode requires a host restart; it is not a hot switch.

Browser user data stays under `GakuLoader/WebView2`. DevTools are off unless `GAKULOADER_WEBVIEW_DEVTOOLS=1`
is set for the host process.

## Isolated Manager prototype frontend

`frontend/` keeps only the isolated Loader Manager Web prototype (`src/prototype/LoaderPrototypeApp.tsx`)
with its own mock bridge and unit tests (`npm run typecheck`, `npm test`, `npm run build`). GakuMotion's
production React panel lives in the `GakuMotion` repository.

## Validation boundaries

The Windows CI workflow has two jobs. `source-validation` runs
`tools/validate-source.ps1 -PlatformToolset v143` on `windows-2022`; its headless suite list includes
`manager-bridge` and `web-panel-host`. `prototype-frontend-validation` uses Node.js 24 with npm caching
keyed by `frontend/package-lock.json`, then runs `npm ci`, `npm run typecheck`, `npm test`, and
`npm run build` in `frontend/` to keep the isolated prototype healthy.

Useful local checks are:

```powershell
.\build.ps1 -Configuration Release -PlatformToolset v145
.\tests\build-tests.ps1 -SkipFixtures -PlatformToolset v145
.\tests\run-all.ps1 -Configuration Release -SkipBuild -SkipFixtures -Only manager-bridge,web-panel-host
.\tools\test-package.ps1
.\tools\package-release.ps1 -Configuration Release -PlatformToolset v145
.\tools\test-docs.ps1 -StaticOnly
```

Host contract checks, PE/export checks, and isolated prototype tests do not replace joint runtime
acceptance with a real plugin DLL and the Edge WebView2 Evergreen Runtime, nor real in-game verification.
