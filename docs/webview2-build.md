# GakuMotion WebView2 build and local packaging

This guide covers the GakuMotion React/WebView2 plugin panel, the app-local WebView2 SDK loader, and
offline release staging. It does not deploy, start, or control a game. The GakuLoader Manager remains
ImGui. The native host still builds as `version.dll`; the GakuMotion web panel looks for
`WebView2Loader.dll` and `GakuMotion.UI` beside that module.

## Prerequisites

- Visual Studio 2022 MSVC v143 or Visual Studio 2026 MSVC v145 with the C++ desktop workload and a
  Windows SDK. The observed local environment has only v145, so local commands below pass
  `-PlatformToolset v145` explicitly.
- Node.js and npm on `PATH`. The frontend lockfile is `frontend/package-lock.json`.
- Vendored inputs: `deps/webview2/include`, `deps/webview2/x64/WebView2Loader.dll`, and
  `deps/nlohmann/json.hpp`. Their upstream versions, URLs, licenses, and SHA256 values are recorded in
  [WebView2 provenance](../deps/webview2/PROVENANCE.md) and
  [nlohmann/json provenance](../deps/nlohmann/PROVENANCE.md).
- Target machines need the Microsoft Edge WebView2 Evergreen Runtime installed separately. The
  vendored/packaged `WebView2Loader.dll` is the SDK loader, not the browser runtime.

`npm ci` needs access to the npm registry or a populated npm cache. Once dependencies and the frontend
are built, the packaged Motion assets load offline. Packaging itself does not download the WebView2
Evergreen Runtime.

## Frontend development with the mock bridge

From the repository root:

```powershell
.\frontend\build.ps1 -Dependencies Ci
Push-Location .\frontend
npm run dev
Pop-Location
```

Vite serves `http://localhost:5173/`. In a plain browser there is no `window.chrome.webview`, so the
frontend bridge selects the stateful Motion mock bridge. This is useful for UI development and tests;
it is not the native host bridge and does not exercise real plugin lifecycle or game behavior.

## Production frontend build

```powershell
.\frontend\build.ps1 -Dependencies Ci
```

The script runs locked `npm ci --ignore-scripts --no-audit --no-fund`, then `npm run build`
(`tsc -b && vite build`). It writes `frontend/dist`, keeps Vite `base: './'`, and rejects source maps,
`node_modules`, build-info files, and absolute/external HTML asset references. `-Dependencies Skip`
reuses an existing `frontend/node_modules` for a fast local rebuild. `-Dependencies Install` is an
explicit dependency-maintenance escape hatch and may update the lockfile; do not use it for a
reproducible release build.

## Native build

```powershell
.\build.ps1 -Configuration Release -PlatformToolset v145
.\build.ps1 -Configuration Debug -PlatformToolset v145
```

Release writes `build/version.dll`; Debug writes `build/debug/version.dll`. Both copy the vendored
`WebView2Loader.dll` beside the built `version.dll`. The existing optional `-OutputDll` candidate copy
also copies `WebView2Loader.dll` beside that candidate. The script does not locate a game directory or
deploy automatically.

## Local release package

Build both inputs and package them:

```powershell
.\tools\package-release.ps1 -Configuration Release -PlatformToolset v145 -BuildNative -BuildFrontend
```

Or package already-built local outputs:

```powershell
.\tools\package-release.ps1 -Configuration Release -PlatformToolset v145
```

The default output is a fresh directory under `build/release/`. `-OutputDir` may name another new
child directory under `build/release`, but existing directories are never overwritten and output
paths/reparse points that escape the repository staging root are rejected. `-BuildFrontend` forwards
`-FrontendDependencies Ci|Install|Skip` to the frontend script; `Ci` is the default.

The package contains:

```text
<package>/
|-- version.dll
|-- WebView2Loader.dll
|-- GakuMotion.UI/
|   |-- index.html
|   `-- assets/...
|-- INSTALL.txt
|-- package-manifest.json
|-- THIRD_PARTY_NOTICES.md
|-- LICENSE
|-- LICENSES/...
`-- deps/...                  retained license and provenance records only
```

Before writing the manifest, the script checks the native DLL with `tools/test-package.ps1` (x64,
version proxy exports, no dynamic CRT), checks the loader's required dynamic-load exports, verifies
the frontend asset closure, and compares every copied payload file's SHA256 with its source. The
manifest records payload hashes, source HEAD, dirty-worktree state, build invocation flags, resolved
toolset, and input hashes. It excludes PDBs, `node_modules`, source maps, build info, game assets,
plugins, personal configuration, and the Evergreen Runtime. Packaging still requires MSVC/dumpbin for
the native contract checks even when reusing prebuilt DLLs.

`package-manifest.json` is the completion marker. A failed run can leave an incomplete fresh staging
directory; do not install from a directory without a verified manifest. The package is unsigned local
staging output, not a publication or bitwise-reproducibility attestation.

## Manual install layout and fallback

Close the game and independently back up existing host files before any manual replacement. Keep the
two DLLs and the UI directory together beside `gakumas.exe`:

```text
<game root>/
|-- gakumas.exe
|-- version.dll
|-- WebView2Loader.dll
|-- GakuMotion.UI/
|   |-- index.html
|   `-- assets/...
`-- GakuLoader/
    |-- Configs/gakuloader.ini
    |-- Logs/gakuloader.log
    |-- WebView2/              browser user data, created at runtime
    `-- plugins/               existing plugins, not supplied by this package
```

The Manager remains ImGui. Plugin web panels are enabled by default when a plugin provides a
web-panel service and the runtime/assets are available. The optional host setting is:

```ini
[host]
ui=webview2
```

Use `ui=imgui` to disable plugin web panels and keep legacy ImGui plugin panels. If the Evergreen
Runtime, app-local loader, or `GakuMotion.UI/index.html` is unavailable, the host logs a warning and
falls back to the plugin's ImGui panel inside the Manager content area. It never falls back to the
game window. Changing the host UI mode requires a host restart; it is not a hot switch.

The native host maps `GakuMotion.UI` to the local virtual host
`https://gakumotion.invalid/index.html`. Relative Vite assets and the HTML CSP keep Motion asset
loading local; no CDN is required. Browser user data stays under `GakuLoader/WebView2`. DevTools are
off unless `GAKULOADER_WEBVIEW_DEVTOOLS=1` is set for the host process.

## Validation boundaries

The Windows CI workflow has two jobs. `source-validation` runs
`tools/validate-source.ps1 -PlatformToolset v143` on `windows-2022`; its headless suite list includes
`manager-bridge` and `web-panel-host`. `frontend-validation`
uses Node.js 24 with npm caching keyed by `frontend/package-lock.json`, then runs `npm ci`,
`npm run typecheck`, `npm test`, and `npm run build` in `frontend/`.

Useful local checks are:

```powershell
.\frontend\build.ps1 -Dependencies Ci
.\build.ps1 -Configuration Release -PlatformToolset v145
.\tests\build-tests.ps1 -SkipFixtures -PlatformToolset v145
.\tests\run-all.ps1 -Configuration Release -SkipBuild -SkipFixtures -Only manager-bridge
.\tools\test-package.ps1
.\tools\package-release.ps1 -Configuration Release -PlatformToolset v145
.\tools\test-docs.ps1 -StaticOnly
```

Frontend unit/type checks and mock-browser visual checks are separate from native host acceptance.
Hashes, PE/export checks, package closure, and offline asset checks do not prove game loading,
rendered WebView2 behavior, plugin runtime behavior, or user visual acceptance.
