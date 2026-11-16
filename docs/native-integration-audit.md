# Native integration audit — WebView2 + React manager UI

Scope: read-only audit of the GakuLoader host in this worktree
(`git-work/GakuLoader/worktrees/webview2-ui`, branch `feat/webview2-manager-ui`,
commit `0d18501f3eb2b157176c07de47f6857bb97e32e8`) for the ImGui → WebView2/React
migration of the F1 manager. Audited 2026-11-15 (Asia/Shanghai). No source,
project, ABI, or persistent-format file was modified; this document is the only
write. No build, game launch, or deployment was performed. Parallel teammate
activity was observed in `deps/webview2/` during the audit (see §6.3); findings
about that directory are a snapshot, not a completion claim.

All paths below are relative to the worktree root unless absolute. Line numbers
refer to the audited commit.

## 0. Executive recommendation

- Create the WebView2 environment/controller on the **existing desktop UI
  worker thread**, which is already an STA (`OleInitialize`, `src/DesktopUi.cpp:1130`)
  with a pumping `PeekMessageW` loop (`src/DesktopUi.cpp:1171-1194`). No new
  thread, no new message pump, no COM model change is required.
- Parent the controller to the **existing top-level control HWND**
  (`GakuModLoader.DesktopControl`, `src/DesktopUi.cpp:40`, created at
  `src/DesktopUi.cpp:1107`). Keep the Win32 window, tray, F1 toggle, file-drop
  transports, and modal-dialog pump exactly as they are.
- Replace **only the manager page** (`gakuloader.manager`, `src/DesktopUi.cpp:42`)
  with React. Plugin panels keep rendering through the existing ImGui
  `onDesktopUi` ABI (`include/GakuPlugin.h:287-290`); `AGENTS.md` forbids ABI
  changes, so plugin panels cannot move to React in this migration.
- Keep the full ImGui/D3D11 path (including WARP fallback,
  `src/DesktopUi.cpp:585-590`) as the **runtime fallback** when the WebView2
  runtime or asset bundle is unavailable.
- Link `WebView2LoaderStatic.lib` from a vendored `deps/webview2/` (NuGet
  `Microsoft.Web.WebView2` 1.0.4314.35, already staged under `build/webview2-sdk/`)
  so the single-`version.dll` package contract and the no-dynamic-CRT gate
  (`tools/test-package.ps1:29-33`) keep passing.
- Serve the React bundle from a **trusted virtual origin**
  (recommended `https://gakuloader.invalid/`) via
  `SetVirtualHostNameToFolderMapping`, fully offline. Two packaging options in §4.
- Typed bridge: JSON request/response + host-push events over
  `postMessage`/`WebMessageReceived`/`PostWebMessageAsJson`, handled on the UI
  thread, with a closed method allowlist mapped onto existing host functions
  (§5.5). No raw hook/service/pointer surface crosses into JS.

## 1. Build system

### 1.1 Entry points and artifact contract

- `build.ps1` is a thin MSBuild shell. Params: `-Configuration Debug|Release`,
  `-OutputDll <candidate copy>`, `-PlatformToolset auto|v143|v145`
  (`build.ps1:13-18`). Release output is `build/version.dll`; Debug is
  `build/debug/version.dll` (`build.ps1:28-31`, `GakuLoader.vcxproj:34-36`).
  It verifies vendored sources exist before building
  (`deps/imgui/imgui.cpp`, `deps/minhook/src/hook.c`, `build.ps1:51-55`) and
  optionally copies DLL+PDB to a candidate path (`build.ps1:86-100`).
- `tools/msbuild-env.ps1` owns toolchain discovery:
  - `Clear-GakuPathDuplicate` normalizes duplicate `Path` casing (MSB6001 guard).
  - `Resolve-GakuBuildTools` (`tools/msbuild-env.ps1:30-72`) runs vswhere,
    prefers `v145` then `v143` on `auto`, matches toolset dirs under
    `MSBuild\Microsoft\VC\*\Platforms\x64\PlatformToolsets\<toolset>` and MSVC
    versions `14.5x` (v145) or `14.3x/14.4x` (v143), and pins
    `/p:VCToolsVersion`.
  - `Resolve-GakuDumpbin` (`tools/msbuild-env.ps1:79-101`) is used by the
    package gate instead of MinGW binutils.
- `GakuLoader.slnx` groups host/deps/tests. Note: the slnx lists only 10 test
  projects; `tests/build-tests.ps1` builds **every** `tests/*.vcxproj` directly
  (`tests/build-tests.ps1:39-42`), so a new test project is picked up by the
  scripts even without an slnx entry (add one anyway for IDE parity).

### 1.2 Compiler/link policy (must be preserved)

`Directory.Build.props` (inherited by every vcxproj, including tests via
`tests/Directory.Build.props`):

- Toolset auto-select: `v145` when VS ≥ 18, else `v143`
  (`Directory.Build.props:17-18`); `WindowsTargetPlatformVersion=10.0` resolves
  any installed SDK (`Directory.Build.props:20`).
- C++20, `/utf-8 /Zc:__cplusplus`, `/W4`, `/EHsc` (`ExceptionHandling=Sync`),
  conformance mode, `NOMINMAX;WIN32;_WINDOWS;UNICODE;_UNICODE;_CRT_SECURE_NO_WARNINGS`
  (`Directory.Build.props:29-41`).
- Static CRT: Release `MultiThreaded` (`/MT`, `Directory.Build.props:55`),
  Debug `MultiThreadedDebug` (`/MTd`, `Directory.Build.props:70`).
- Release LTCG on by default, overridable with `/p:GakuLtcg=false`
  (`Directory.Build.props:13,52,59`); `/OPT:REF,ICF` re-enabled explicitly.
- **No Control Flow Guard** anywhere (AGENTS.md + `wiki/Building.md`): MinHook
  trampolines require it off. Do not let a WebView2-related props import turn
  it on (the NuGet `Microsoft.Web.WebView2.targets` is *not* imported by this
  repo's projects and should not be; wire include/lib paths manually instead).
- Shared include dirs: `include;src;deps/imgui;deps/minhook/include;deps/minhook/src`
  (`Directory.Build.props:41`). Add WebView2 includes **only to
  `GakuLoader.vcxproj`** (and the new test project), not to the shared props,
  so MinGW fixtures and unrelated tests are unaffected.
- Outputs: `build/` root holds only release artifacts; objects under
  `build/obj/<project>/<config>/`; static libs and import lib under
  `build/lib/<config>/` (`Directory.Build.props:23-25`, `GakuLoader.vcxproj:46`).

### 1.3 Projects and vendored dependencies

- `GakuLoader.vcxproj`: `DynamicLibrary`, `TargetName=version`
  (`GakuLoader.vcxproj:24,36`), 14 `ClCompile` TUs (`GakuLoader.vcxproj:55-68`),
  version resource `src/GakuLoader.rc` (`GakuLoader.vcxproj:51`, currently
  1.0.1.0), link deps
  `d3d11;dxgi;d3dcompiler;imm32;dwmapi;gdi32;ole32;uuid;shell32;user32`
  (`GakuLoader.vcxproj:44`). `ole32/uuid/shell32/user32` already cover most
  WebView2 loader needs.
- `ImGui.vcxproj` / `MinHook.vcxproj`: static libs compiled from vendored
  source, warnings off, referenced via `ProjectReference`
  (`GakuLoader.vcxproj:101-106`). ImGui is v1.89.4 with one documented local
  backend fix (`deps/imgui/PROVENANCE.md`); MinHook has local batch/relocation
  extensions (`deps/minhook/PROVENANCE.md`).
- Vendoring convention (AGENTS.md + `THIRD_PARTY_NOTICES.md`): source is
  tracked in-tree with `LICENSE.txt` + `PROVENANCE.md` (upstream
  tag/commit/license/local changes), a row in `THIRD_PARTY_NOTICES.md`, and a
  presence guard in `build.ps1`. No submodules, no prebuilt upstream binaries,
  no silent upstream replacement. A vendored **binary** SDK (WebView2 loader
  lib) is a new category: document it explicitly in `PROVENANCE.md` with the
  NuGet id/version and package hash, and keep the matching `.nupkg` retrievable
  (staged copy currently in ignored `build/webview2-sdk/`; a tracked hash record
  is what makes it reproducible).

### 1.4 Minimal-change recipe for the new dependency

1. `deps/webview2/` with `include/WebView2.h`, `include/WebView2EnvironmentOptions.h`,
   `x64/WebView2LoaderStatic.lib` (preferred) or `x64/WebView2Loader.dll.lib` +
   `WebView2Loader.dll`, `LICENSE.txt`, `NOTICE.txt`, `PROVENANCE.md`.
   (Partially present at audit time — see §6.3.)
2. `GakuLoader.vcxproj` only: add
   `$(GakuDepsDir)webview2\include` to `AdditionalIncludeDirectories`,
   `$(GakuDepsDir)webview2\x64` to `AdditionalLibraryDirectories`, and
   `WebView2LoaderStatic.lib` to `AdditionalDependencies`
   (`GakuLoader.vcxproj:40-47` ItemDefinitionGroup). New TUs
   (`src/WebView2Host.cpp`, `src/ManagerBridge.cpp`) join the `ClCompile` list.
3. `build.ps1`: extend the vendored-source guard loop (`build.ps1:51-55`) with
   `deps\webview2\include\WebView2.h` (and the chosen lib).
4. Do **not** import `Microsoft.Web.WebView2.targets`, do not add a
   `packages.config`/PackageReference, and do not touch `Directory.Build.props`
   include paths — that keeps MinGW fixtures, tests, and the v143 CI build
   unchanged.
5. Static-loader check: after first link, `dumpbin /dependents build/version.dll`
   must still show no `vcruntime/msvcp/ucrtbase` (the package gate enforces it).
   `WebView2LoaderStatic.lib` from the NuGet package is built for static CRT
   consumption; if a future SDK version changes that, fall back to
   `WebView2Loader.dll.lib` + shipping `WebView2Loader.dll` (which changes the
   install contract — see §4.3).

### 1.5 Toolchain actually installed on this machine (observed)

| Item | Value |
| --- | --- |
| VS | Visual Studio Community 2026 Insiders 18.12.12328.396, `D:\Program Files\Microsoft Visual Studio\18\Insiders` (only installation; vswhere) |
| MSVC | `14.52.37025`, `14.53.36919` — both **v145**; toolset props under `MSBuild\Microsoft\VC\v180\Platforms\x64\PlatformToolsets\v145` |
| v143 | **Not installed.** `Resolve-GakuBuildTools -PlatformToolset v143` throws (observed). Local builds must use `auto` or `v145`; CI's v143 runs on GitHub's windows-2022 image and is unaffected |
| Windows SDK | 10.0.26100.0 at `D:\Windows Kits\10` (registry `InstallationFolder`; `Include`/`Lib` with `um\x64` + `ucrt\x64` verified). Not under `C:\Program Files (x86)\Windows Kits` |
| MSBuild | `D:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Current\Bin\MSBuild.exe`; `Resolve-GakuBuildTools auto` → v145 / 14.53.36919 (observed) |
| MinGW | `gcc`/`g++` not on PATH → heterogeneous fixtures must be skipped locally (`-SkipFixtures`) |

## 2. CI and source integrity

### 2.1 Workflow (`.github/workflows/windows-msvc.yml`)

Triggered on push/PR/manual; `windows-2022`; `permissions: contents: read`.
Steps: `actions/checkout@v4` → `./tools/validate-source.ps1 -PlatformToolset v143`
(Windows PowerShell 5.1) → `actions/upload-artifact@v4` (`if: always()`) uploading
`build/validation/`, `build/tests/report.json`,
`build/examples/minimal-plugin/Release/activation.log`.

### 2.2 `tools/validate-source.ps1` — the gate that must keep passing

Writes `build/validation/receipt.json` (schema `gakuloader.observed-source-build/1`)
with per-step log SHA256 and artifact hashes (`tools/validate-source.ps1:9-25,43-48`).
Steps in order (`tools/validate-source.ps1:29-36`):

1. `release-build` — `build.ps1 -Configuration Release`
2. `debug-build` — `build.ps1 -Configuration Debug`
3. `msvc-test-build` — `tests/build-tests.ps1 -SkipFixtures`
4. `headless-suites` — `tests/run-all.ps1 -SkipBuild -SkipFixtures -Only language-settings,desktop-fonts,desktop-appearance,docs,paths,frame-pump`
5. `c-abi` — `build/tests/Release/c-abi-test.exe`
6. `minhook-isolation` — `build/tests/Release/minhook-isolation.exe`
7. `minimal-plugin` — `examples/minimal-plugin/verify.ps1` (builds the example
   with MSVC, drives real `LoadLibrary`/`GakuPlugin_Query`/activate/shutdown in
   a synthetic host, checks `GakuPlugin_Query` export via dumpbin)
8. `package` — `tools/test-package.ps1`

Explicitly excluded from this gate: MinGW fixtures, windowed UI suites, game
loading/acceptance, remote CI (`tools/validate-source.ps1:16`).

### 2.3 Package gate details (`tools/test-package.ps1`)

- PE parse: must be x64 DLL (`tools/test-package.ps1:10-17`).
- Export scan: regex over `src/Proxy.cpp` for
  `__declspec(dllexport) <type> WINAPI <name>(` — currently 17 version-proxy
  exports — each must appear in `dumpbin /exports`
  (`tools/test-package.ps1:20-28`). Keep new exports out of `Proxy.cpp`; the
  loader adds no new exports for WebView2.
- Dependency scan: any `vcruntime*/msvcp*/ucrtbase*` import fails the /MT
  contract (`tools/test-package.ps1:29-33`). This is why the static WebView2
  loader is the low-friction choice.

### 2.4 Docs gate (`tools/test-docs.ps1`) — applies to this file

- `docs/PLUGIN_DEVELOPMENT.md` + `docs/QUICKSTART.md` must not mention retired
  GML symbols and must match `examples/minimal-plugin/MinimalPlugin.cpp`.
- **Every** root/`docs/`/`wiki/` Markdown file is scanned for relative link
  targets that must exist (code fences stripped). This audit deliberately uses
  backticked paths, no Markdown links, so it cannot break that check.
- Onboarding docs must not contain `build.ps1 -SkipDeploy` or the old solution
  name.

### 2.5 Hash manifests

`VALIDATION.md` and `DOCS_RESTORATION.md` reference a frozen
`SOURCE_MANIFEST.json`, but **no manifest file is tracked at this commit**
(`git ls-files` shows none) and no script enforces it. It is historical
evidence, not a live gate. The live integrity mechanisms are: vendored
`PROVENANCE.md` records, `THIRD_PARTY_NOTICES.md`, the validation receipt, and
test-report evidence hashes. New WebView2 vendor files should follow the
provenance convention; there is no manifest to update.

## 3. Tests

### 3.1 Layout and compilation model

- Host tests are **MSVC executables that compile production TUs directly**:
  `tests/HostTests.vcxproj` compiles `PluginHost.cpp`, `ActorEntry.cpp`,
  `Trace.cpp`, `CrashTrace.cpp`, `Hotkeys.cpp` with `GAKULOADER_TEST_SEAMS`
  (seams declared in `src/Host.hpp:105-120`); `tests/DesktopUiSmoke.vcxproj`
  compiles `DesktopUi.cpp`, `DesktopDialogs.cpp`, `Hotkeys.cpp` with
  `GAKULOADER_DESKTOP_UI_TEST` and links ImGui + MinHook. Under that define the
  hardware D3D path is compiled out and **WARP is forced**
  (`src/DesktopUi.cpp:585-590`); `TestHooks` (`src/DesktopUi.hpp:36-50`) expose
  `startHidden`, frame inspection, CPU-frame/presented counters, clipboard
  injection, and `inspectUiItem` widget rectangles.
- `tests/ManagerUiTests.cpp` drives the *production* manager with synthetic
  ImGui input on the hidden WARP window and asserts via widget rectangles and
  backbuffer screenshots — the model to copy for UI-level bridge tests.
- `tests/build-tests.ps1`: builds `GakuLoader.vcxproj` + all `tests/*.vcxproj`
  (except `PathsFixture.vcxproj`), then three MSVC paths fixtures
  (`tests/build-tests.ps1:56-69`), asserts a **hardcoded expected-binary list**
  (`tests/build-tests.ps1:71-82`), and writes `build-stamp.txt`
  (`tests/build-tests.ps1:85-87`). MinGW fixtures are optional
  (`tests/build-fixtures.ps1`, needs `gcc`/`g++` via `-MinGwBin`,
  `GAKU_MINGW_BIN`, or PATH).
- `tests/run-all.ps1`: 15 named suites (`tests/run-all.ps1:31-47`), per-suite
  logs + `build/tests/report.json` with evidence SHA256; fixture-dependent
  suites (`host`, `diagnostic-boundary`, `host-dialog-concurrency`,
  `desktop-ui-cross-dll`) become `SKIP_DEPENDENCY` under `-SkipFixtures`
  (`tests/run-all.ps1:70,90-95`).
- `tests/test-common.ps1`: runners never compile; `Get-GakuTestBinary` refuses
  binaries older than the newest source across
  `src, tests, include, deps/imgui, deps/minhook`
  (`tests/test-common.ps1:31`) vs the build stamp. **Gap to close:** add
  `deps/webview2` to that root list when the SDK is vendored, otherwise SDK
  header/lib edits will not invalidate test binaries.
- `tests/Directory.Build.props` undefines `NDEBUG` for tests so `assert()`
  sequencing survives Release builds.

### 3.2 How a bridge test suite fits

1. **Pure protocol tests (headless, always run):** keep the bridge core
   (`src/ManagerBridge.cpp`) transport-agnostic — JSON envelope parse/validate,
   method allowlist, origin checks, error mapping — with no WebView2 includes.
   New `tests/BridgeTests.vcxproj` compiles it plus the host TUs it needs
   (pattern: `HostTests.vcxproj`), output `build/tests/Release/bridge-tests.exe`.
   Add the binary to the expected list (`tests/build-tests.ps1:71`) and a suite
   entry + runner script to `tests/run-all.ps1:31`. This can join the CI
   headless list in `tools/validate-source.ps1:32` safely.
2. **WebView2 smoke (runtime-gated):** `tests/WebView2Smoke.vcxproj` compiles
   `src/WebView2Host.cpp` + `DesktopUi.cpp` with the existing test define plus a
   new `GAKULOADER_WEBVIEW2_TEST` seam (hidden start, controller inspection).
   At startup call `GetAvailableCoreWebView2BrowserVersionString(nullptr, ...)`;
   when no runtime is present exit with a distinct code and have the runner
   record `SKIP_DEPENDENCY` (same mechanism as MinGW fixtures). Do not add it
   to the default CI headless list until runtime presence on windows-2022 is
   verified; add it as a separate optional workflow step instead.
3. **Frontend tests:** `npm ci && npm test && npm run build` in the new web
   project; keep them out of `validate-source.ps1` initially (Node is not a CI
   dependency today) or add a separate workflow job.

### 3.3 Commands that work on this machine

```powershell
# from the worktree root; v145 is the only installed toolset (auto also works)
.\build.ps1 -Configuration Release -PlatformToolset v145
.\build.ps1 -Configuration Debug   -PlatformToolset v145
.\tests\build-tests.ps1 -SkipFixtures -PlatformToolset v145
.\tests\run-all.ps1 -SkipBuild -SkipFixtures            # all non-fixture suites
.\tests\run-all.ps1 -SkipBuild -SkipFixtures -Only language-settings,desktop-fonts,desktop-appearance,docs,paths,frame-pump
.\tools\validate-source.ps1 -PlatformToolset v145       # full local gate (CI uses v143)
```

These were **not executed** during this audit (read-only mandate; parallel
vendoring in progress). Toolchain resolution, npm/nuget reachability, and hash
comparisons were executed — see §8.

## 4. Packaging and offline frontend assets

### 4.1 Current contract

The release artifact is exactly one file: `build/version.dll`, copied beside
`gakumas.exe` (`wiki/Installation.md`); data root `<gameRoot>/GakuLoader/`
(`docs/ROOT_LAYOUT.md`). There is no zip/release script in-repo;
`tools/test-package.ps1` is the artifact gate (§2.3). Any asset strategy must
either preserve the single-file contract or explicitly extend installation and
the package gate.

### 4.2 Recommended: virtual-origin folder mapping, two delivery modes

Trusted origin: map a reserved, never-resolving host name to a local folder
with `ICoreWebView2_3::SetVirtualHostNameToFolderMapping`, e.g.
`https://gakuloader.invalid/` → asset folder, `COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL`.
All React/JS/CSS/fonts are served from that folder; **no runtime network**.
Hardening: cancel any `NavigationStarting` outside the trusted origin, handle
`NewWindowRequested` (suppress or route through the same origin), disable
default context menus and optionally devtools via settings, and validate
`WebMessageReceived` `get_Source` against the trusted origin before dispatch.

- **Dev/iteration mode:** assets live at `<gameRoot>/GakuLoader/webui/`
  (created by the host next to `plugins/Configs/Logs`,
  `src/PluginHost.cpp:880-887`). Missing folder → ImGui fallback. Deployment
  docs and the copy step gain one folder; `test-package.ps1` can stay
  DLL-only, with an optional asset-hash manifest check added later.
- **Release single-file mode:** embed the built `dist/` as RCDATA resources
  (new generated `.rc` alongside `src/GakuLoader.rc`, produced by a
  `tools/build-webui.ps1` from `npm run build` output), extract at startup to
  `<localRoot>/Cache/gakuloader.manager/webui/` with a content-hash check, then
  map the virtual host. The single-DLL contract and every existing gate remain
  unchanged; UI updates require a DLL rebuild (acceptable for release).

Recommendation: start with the folder mode behind the fallback, add the
embedded mode before release if the single-file contract must hold.

### 4.3 Loader DLL alternative (avoid if possible)

Using `WebView2Loader.dll.lib` + shipping `WebView2Loader.dll` beside
`version.dll` also works and passes the CRT gate, but breaks the one-file
install story and adds a DLL-search-path concern inside the game process.
Prefer `WebView2LoaderStatic.lib`.

### 4.4 WebView2 runtime distribution

The Evergreen WebView2 Runtime **is installed on this machine** (157.0.4341.46,
§6.1). Detect at runtime with `GetAvailableCoreWebView2BrowserVersionString`;
when absent, log once and stay on ImGui (the manager must never hard-fail
because a browser runtime is missing). Do not bundle a Fixed Version runtime
in this migration (large payload, separate servicing story); if offline
machines without the runtime must be supported later, that is a follow-up
decision. The game's own `VuplexWebViewWindows.dll` (referenced at
`src/GameHook.cpp:186`) is unrelated — never reuse or depend on it.

### 4.5 WebView2 user-data folder

Pass an explicit `userDataFolder` to
`CreateCoreWebView2EnvironmentWithOptions`:
`<localRoot>/Cache/gakuloader.manager/webview2/`. Never default it into the
game root or next to the DLL. This is host-owned cache, consistent with the
paths-service layout (`include/GakuPaths.h:16-32`); no ABI involvement.

## 5. Native lifecycle and the integration point

### 5.1 Thread map (observed)

| Thread | Created at | Role |
| --- | --- | --- |
| Game main / Unity window thread | game | `UnityWndClass` WndProc is subclassed by QuitGuard; frame pump + plugin ticks run here |
| Loader startup | `src/Main.cpp:90` (`DllMain` → `LoaderStartupThread`, `src/Main.cpp:31`) | pins the module (`src/Main.cpp:34-36`), optional console (`src/Main.cpp:43-59`), `Initialize` → `DesktopUi::Initialize` → `ActivatePlugins` → `StartHookThread` → `StartQuitGuard` (`src/Main.cpp:60-77`) |
| **Desktop UI worker (STA)** | `src/DesktopUi.cpp:1219` (`Initialize` → `Worker` `src/DesktopUi.cpp:1201` → `WorkerBody` `src/DesktopUi.cpp:1094`) | owns the control HWND, ImGui, D3D11, OLE drop target, tray, modal dialogs; **the WebView2 thread** |
| Hook/runtime worker | `src/GameHook.cpp:482` (`StartHookThread` `src/GameHook.cpp:463`) | IL2CPP resolution, detour install, then 10 ms frame-pump wake posts (`src/GameHook.cpp:422-431`) |
| Hotkey poller | `src/PluginHost.cpp:736` (`StartHotkeyPoller` `src/PluginHost.cpp:729`, body `src/PluginHost.cpp:696-725`) | 12 ms `GetAsyncKeyState` edges; F1 → `DesktopUi::ToggleManager` |
| QuitGuard poller | `src/QuitGuard.cpp:330` (`StartQuitGuard` `src/QuitGuard.cpp:326`, body `src/QuitGuard.cpp:282`) | finds Unity window, installs `GuardedWndProc` (`src/QuitGuard.cpp:151-167`) |
| Exit watchdog | `src/QuitGuard.cpp:317` (`ExitWatchdog` `src/QuitGuard.cpp:178`) | budgets: 15 s teardown / 8 s post-Quit / 40 s ceiling, then `TerminateProcess` (`src/QuitGuard.cpp:255`) |

`DllMain` (`src/Main.cpp:83-110`): on attach only `InitProxy` + thread creation;
on detach in the game process it deliberately does **nothing**
(`src/Main.cpp:95-107`, loader-lock hazard). All orderly teardown happens via
UnityQuit/QuitGuard, never in `DLL_PROCESS_DETACH`. WebView2 cleanup must obey
the same rule.

### 5.2 The UI thread is already WebView2-ready

- Window: top-level `WS_OVERLAPPEDWINDOW | WS_EX_APPWINDOW`, class
  `GakuModLoader.DesktopControl`, no owner (`src/DesktopUi.cpp:1107-1108`);
  min track 720×480 (`src/DesktopUi.cpp:459-461`); `WM_CLOSE` hides to tray
  (`src/DesktopUi.cpp:474-480`); F1 toggle via `WM_APP+84`
  (`src/DesktopUi.cpp:544-556`); DPI via `WM_DPICHANGED`
  (`src/DesktopUi.cpp:462-470`).
- COM: `OleInitialize(nullptr)` on this thread (`src/DesktopUi.cpp:1130`) →
  **STA**, which is what WebView2 requires. `OleUninitialize` is last in
  `DestroyPresentation` (`src/DesktopUi.cpp:649`).
- Pump: `PeekMessageW` drain + `RenderFrame` + `DesktopDialogs::Pump` +
  `MsgWaitForMultipleObjectsEx(…, 16/100 ms, QS_ALLINPUT, MWMO_INPUTAVAILABLE)`
  (`src/DesktopUi.cpp:1171-1194`). Async WebView2 completion callbacks arrive
  through this pump; no `GetMessage`-only or modal-blocking restructure needed.
- Rendering: hardware D3D11 with WARP fallback, 2-buffer
  `DXGI_SWAP_EFFECT_DISCARD` swapchain, vsync `Present(1, 0)`
  (`src/DesktopUi.cpp:579-619,1088`). In web mode the ImGui manager draw can be
  skipped (saves CPU); keep the device alive for plugin panels and fallback.
- Modal file dialogs run on this same thread via `DesktopDialogs::Pump`
  (`src/DesktopUi.cpp:1192`), each `Show` opening a nested STA apartment scope
  (`src/DesktopDialogs.cpp:141-153`) with a cancel timer on the owner HWND
  (`src/DesktopDialogs.cpp:171`). WebView2 must be created on the **outer**
  apartment (WorkerBody), never inside dialog scope; the dialog's own modal
  loop keeps pumping, so the controller survives a modal.

### 5.3 Concrete insertion points (minimal diff)

New files: `src/WebView2Host.hpp/.cpp` (env/controller/view lifetime) and
`src/ManagerBridge.hpp/.cpp` (typed protocol; transport-agnostic core).
`src/DesktopUi.cpp` stays the window/pump owner and calls into them:

1. **Create:** in `WorkerBody`, after the OLE block and before `ShowWindow`
   (`src/DesktopUi.cpp:1148-1168`), start async
   `CreateCoreWebView2EnvironmentWithOptions(nullptr /*evergreen*/, userDataFolder, options, cb)`;
   in the completion callback (UI thread) create the controller with
   `parent = window`, `put_Bounds(client rect)`, then navigate to the trusted
   origin. If anything fails or `g_stop` is already set, close what was
   created and stay on ImGui. The window can show ImGui first and switch when
   the controller is ready (progressive, no startup stall).
2. **Resize:** `WM_SIZE` (`src/DesktopUi.cpp:444-449`) currently only records
   swapchain resize dims — add `controller->put_Bounds` there; on
   `WM_DPICHANGED`/move call `NotifyParentWindowPositionChanged`.
3. **Mode switch:** when `g_selectedPlugin == kManagerPanel`
   (`src/DesktopUi.cpp:966-967`) show the WebView2 child and skip
   `DrawManagerPage`; when a plugin panel is selected, hide the child
   (`put_IsVisible(FALSE)`) and render the existing ImGui shell unchanged.
   Selection changes flow through the existing `g_selectedPlugin` /
   `PostShow` logic (`src/DesktopUi.cpp:363-376,530-543`).
4. **Shutdown:** call `WebView2Host::Shutdown()` at the top of
   `DestroyPresentation` (`src/DesktopUi.cpp:620`), i.e. controller `Close()`
   and release of view/controller/env **before** ImGui/D3D teardown,
   `DestroyWindow`, and `OleUninitialize` (`src/DesktopUi.cpp:629-649`). Also
   cover the exception path in `Worker` (`src/DesktopUi.cpp:1201-1213`).
   `DesktopUi::Shutdown` already bounds the join at 5 s
   (`src/DesktopUi.cpp:1223-1234`); a hung browser process degrades to the
   existing "resident resources retained" warning, and the QuitGuard watchdog
   remains the final backstop.
5. **Keyboard/text-input:** the hotkey poller suppresses plugin hotkeys while
   `DesktopUi::WantsTextInput()` is true (`src/PluginHost.cpp:714-717`,
   `src/DesktopUi.cpp:927,1312`), which today reflects **ImGui** focus only.
   With React focused this must be fed from the web side (bridge event on
   `focusin`/`focusout` of editable elements, or controller focus events),
   otherwise typing in a React input fires plugin hotkeys. Also handle
   `add_AcceleratorKeyPressed` so F1/Escape behave, and consider
   `AreBrowserAcceleratorKeysEnabled(FALSE)` to keep browser shortcuts from
   shadowing app keys. **This is a mandatory correctness item, not polish.**
6. **File drop:** the shell transports (`WM_DROPFILES` + OLE `IDropTarget`,
   integrity-gated, `src/DesktopUi.cpp:1115-1164`) feed the plugin file-drop
   mailbox. A WebView2 child covering the client area would swallow shell
   drops, and browser `File` objects cannot expose full paths, so the native
   transports must stay live: set
   `ICoreWebView2Controller4::put_AllowExternalDrop(FALSE)` (present in SDK
   1.0.4314.35 / runtime 157) so OLE falls through to the host's registered
   top-level drop target, and keep `DragAcceptFiles`/UIPI filters as-is.
   Verify with the existing `desktop-file-drop` suite plus a new web-mode case.

### 5.4 Shutdown sequencing constraints (observed order)

Game close → `GuardedWndProc` swallows `WM_CLOSE` (`src/QuitGuard.cpp:159-165`)
→ `RequestUnityQuit` posts `WM_APP+0x5171` (`src/UnityQuit.cpp:97-121`) →
`IssueQuitOnMainThread` on the Unity thread (`src/UnityQuit.cpp:55-93`) →
`DisableAllHooksForExit` (`src/GameHook.cpp:486-506`):

1. `RequestShutdown()` (publish cancellation first)
2. `DesktopUi::Shutdown()` — drain render callbacks, join UI worker ≤5 s
   (**WebView2 Close happens inside this window**)
3. `ShutdownHooks()` — join hook worker ≤2 s, IL2CPP detach
4. `BeginExitTeardown()` + `Shutdown()` — plugin `shutdown` callbacks in
   reverse activation order on the Unity main thread
   (`src/PluginHost.cpp:751-789,1149-1166`), `CloseLog()`
5. `DisableOwnedHooksForExit()` — batched MinHook disable
   (`src/PluginHost.cpp:832-860`); process-lifetime diagnostic hooks stay armed
6. `Application.Quit(0)`; watchdog force-exits if budgets expire
   (`src/QuitGuard.cpp:178-256`)

Constraints for WebView2:

- Never touch WebView2 in `DLL_PROCESS_DETACH` (loader lock;
  `src/Main.cpp:95-107`).
- Controller/view/env release must precede `OleUninitialize` and window
  destruction, on the UI thread only.
- Environment creation is async: guard every completion callback against
  `g_stop`/shutdown-race (create-then-immediately-close is fine; leaking a
  controller past `OleUninitialize` is not).
- On watchdog `TerminateProcess`, `msedgewebview2.exe` children are not
  explicitly killed by the host; Chromium child processes normally exit when
  the parent pipe breaks, but treat orphan cleanup as a verification item
  (QuitGuard already kills `UnityCrashHandler` orphans,
  `src/QuitGuard.cpp:106-136`, if a similar sweep is ever needed).
- `Shutdown()` from non-main threads only requests cancellation
  (`src/PluginHost.cpp:1149-1157`); bridge handlers must never call plugin
  shutdown paths directly.

### 5.5 Typed bridge design (trusted origin, closed surface)

Transport: `window.chrome.webview.postMessage(JSON)` →
`add_WebMessageReceived` (fires on the UI thread) →
`PostWebMessageAsJson` for responses/events. Envelope:
`{v:1, id, method, params}` → `{v:1, id, ok, result|error}`; host-push events
`{v:1, event, data}`. Validate `get_Source` == trusted origin on every message;
reject unknown methods; bound payload sizes (mirror the host's existing
bounded-copy discipline, e.g. `src/HostContracts.hpp:15-25`).

Allowlist mapped to existing host functions (no new host capabilities):

| Method | Backing implementation |
| --- | --- |
| `host.snapshot` | `SnapshotDesktopHost` (`src/PluginHost.cpp:1127-1133`) — already a non-blocking copy |
| `host.logTail` | read last N lines of `LogPath()` (`src/PluginHost.cpp:813-816`) without holding `g_logMutex` over I/O |
| `config.get` / `config.set` | `ReadConfigFlag`/`ReadConfigValue` (`src/HostConfig.hpp:29-40`) + `WritePrivateProfileStringW`, restricted to the existing keys in §7; writes follow the manager-action pattern outside the callback gate (`src/DesktopUi.cpp:1071`) |
| `plugins.setEnabled` | `[plugins] <filename>` write, same as `src/DesktopUi.cpp:905-910` |
| `hotkeys.list` / `set` / `reset` | `Hotkeys::Snapshot` / `SetOverride` / `ResetOverride` (`src/Hotkeys.hpp:52-61`, `src/Hotkeys.cpp:396-433`) |
| `appearance.get` / `set` | `DesktopAppearance::LoadAppearanceSettings` / `SaveAppearanceSettings` (`src/DesktopAppearance.hpp:346-386`) + published atomics (`src/DesktopUi.cpp:398-407`) |
| `language.set` | `LanguageSettings::Save` (`src/LanguageSettings.hpp:33-38`) |
| `panel.select` / `window.toggle` | existing selection logic / `ToggleManager` (`src/DesktopUi.cpp:1307-1311`) |
| `ui.textInputFocus` | feeds `g_wantsTextInput` (§5.3 item 5) |
| `fileDialog.open` | `DesktopDialogs` ticket API (`src/DesktopDialogs.hpp:12-19`) if React needs native pickers; poll via bridge, never modal-in-handler |

State push: hook `PublishDesktopHostSnapshot` (`src/PluginHost.cpp:62`) to
coalesce a `host.snapshot` event to the webview (rate-limit; the snapshot is
already an atomic shared_ptr copy). Log push is optional; prefer `logTail`
polling first to avoid coupling the log mutex to the UI.

Threading rules: handlers run on the UI thread; use only non-blocking host
APIs (`TryDispatchDesktopFrame` semantics, `src/PluginHost.cpp:1107-1115`);
never take `g_callbacks` synchronously from a bridge handler; file writes are
short `WritePrivateProfileStringW` calls exactly where the ImGui manager does
them today. No plugin ABI type, pointer, hook, or service table is exposed.

### 5.6 What does not change

- `include/GakuPlugin.h`, `include/GakuLifecycle.h`, all service tables, the
  version-proxy exports, QuitGuard/UnityQuit/DiagnosticBoundary behavior,
  hotkey registry semantics, file-drop ABI, and `gakuloader.ini` format.
- `DiagnosticBoundary` (`src/DiagnosticBoundary.cpp:87-115`) hooks Unity
  logging only; it has no UI interaction and needs no change.
- The ImGui context, plugin panel dispatch, and callback gate remain for
  plugin panels; React is a manager-page renderer, not a new plugin ABI.

## 6. WebView2 SDK / runtime / tooling availability (observed)

### 6.1 Machine state

| Check | Result |
| --- | --- |
| WebView2 Runtime | **Installed**: `HKLM:\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}` `pv=157.0.4341.46` (HKLM native + HKCU keys absent) |
| Edge browser | `C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe` 157.0.4341.46 |
| NuGet cache | No `microsoft.web.webview2*` under `C:\Users\Exios\.nuget\packages` |
| Node | v24.14.0 (`D:\Program Files\nodejs\node.exe`) |
| npm | 11.11.0; registry `https://registry.npmjs.org/`; `npm ping` → PONG 2540 ms (reachable) |
| dotnet | 11.0.100-rc.3.26522.148 (present; not needed for the C++ build) |
| nuget.exe | Not on PATH |
| nuget.org | `https://api.nuget.org/v3/index.json` → HTTP 200 |
| Sibling repos | No `WebView2.h` / loader libs / WebView2 nupkg found under `git-work/GakuMotion` or the main `git-work/GakuLoader` checkout |

### 6.2 Staged SDK package (ignored build area)

`build/webview2-sdk/microsoft.web.webview2.1.0.4314.35.nupkg`
(9,818,548 bytes) is present in this worktree:

- SHA256 `90778deb1b77e36292828b4b5833c9255c27533f40e0ddd55891ae718944edc4`
- SHA512 (base64) `cOaiVqP3BmQHMnxGpZzANM22tUXgycrisyJHjIGNzIaMsm8iXj7Kx+ceYg+RbYwJtzT009qfcFTocvVLY+ekMw==`
- `.nuspec` confirms `Microsoft.Web.WebView2` 1.0.4314.35 (Microsoft, native
  package). Contains `build/native/include/WebView2.h`,
  `WebView2EnvironmentOptions.h`, and x64/x86/arm64
  `WebView2LoaderStatic.lib` + `WebView2Loader.dll(.lib)`.
- Version `1.0.4314.35` exists in the nuget.org flat-container index
  (observed). The `.nupkg.sha512` endpoint returned 404 twice, so byte-level
  CDN verification via that route was **not** possible; the internal
  header/lib consistency checks below passed. Re-verify the package hash
  against the catalog at integration time if a stronger provenance claim is
  needed.

### 6.3 In-flight vendoring by a parallel teammate (snapshot)

At audit end, untracked `deps/webview2/` contains: `LICENSE.txt`, `NOTICE.txt`,
`include/WebView2.h`, `include/WebView2EnvironmentOptions.h`,
`x64/WebView2Loader.dll` (FileVersion 1.0.4314.35).
`deps/webview2/include/WebView2.h` SHA256
`4e47763725f507235e8a12f35b9f41d7e348586153414c39fc1e9108875c181f` **matches the
nupkg entry byte-for-byte** (observed). Still missing at snapshot time:
`x64/WebView2LoaderStatic.lib` and `PROVENANCE.md`. Coordinate with that work
before wiring the project; this audit did not modify it.

### 6.4 Acquisition recommendation

No download is required to proceed: extract the staged nupkg (it is a zip;
`Expand-Archive` after renaming, or any zip tool) into `deps/webview2/`,
keeping only `include/`, `x64/WebView2LoaderStatic.lib` (+ the dynamic loader
files only if the static route fails), `LICENSE.txt`, `NOTICE.txt`, and write
`PROVENANCE.md` with the NuGet id/version and the hashes above. Runtime: rely
on the installed Evergreen runtime with detection + ImGui fallback (§4.4).

## 7. Settings persistence (reuse as-is)

### 7.1 Path resolution

`<gameRoot>/GakuLoader/Configs/gakuloader.ini` — derived in
`src/Main.cpp:40-42` and `src/PluginHost.cpp:880-888`; exposed as
`ConfigPath()` (`src/PluginHost.cpp:817-820`). Reads use
`GetPrivateProfileStringW` via `ReadConfigValue`/`ReadConfigFlag` with
trim + case-insensitive `true/1/yes` vs `false/0/no`, unknown → caller default
(`src/HostConfig.hpp:18-40`). Writes use `WritePrivateProfileStringW` and, in
the manager, always run outside the callback gate through `g_managerAction`
(`src/DesktopUi.cpp:1071`). The React UI must go through bridge methods that
call these same helpers — no second config store, no format change.

### 7.2 Existing sections/keys (the complete bridge-relevant surface)

| Section | Key | Values / semantics | Read / write sites |
| --- | --- | --- | --- |
| `[host]` | `console` | bool, allocates console at startup | `src/Main.cpp:43`; `src/DesktopUi.cpp:870,876` |
| `[diagnostics]` | `trace` | bool, arms Trace + CrashTrace | `src/PluginHost.cpp:895-901`; `src/DesktopUi.cpp:871,881` |
| `[log]` | `level` | `debug/info/warn/error`, default info | `src/PluginHost.cpp:893-894`; `src/LogLevel.hpp` |
| `[plugins]` | `<filename.dll>` | bool enable, default true; checked before LoadLibrary | `src/PluginHost.cpp:946`; `src/DesktopUi.cpp:896,907` |
| `[appearance]` | `theme` | `Dark/Sapphire/PaperAndInk/AppleLight/AppleDark` (legacy spaced aliases accepted on read) | `src/DesktopAppearance.hpp:346-386` |
| `[appearance]` | `scale` | integer percent 50–300 | `src/DesktopAppearance.hpp:361-367,380-385` |
| `[appearance]` | `language` | `en/zh-Hans/zh-Hant/ja`, else OS default | `src/LanguageSettings.hpp:27-38` |
| `[hotkeys]` | `v2:<hex pluginId>:<hex actionId>` | combo string, empty = unbind, `<default>` = mask | `src/Hotkeys.cpp:309-344,396-433`; `src/Hotkeys.hpp:44-47` |

Notes:

- Theme/scale/language writes today also update live atomics and rebuild fonts
  (`src/DesktopUi.cpp:1044-1053`); a React appearance change must route through
  the same apply path so plugin panels (still ImGui) stay in sync, and the
  published appearance service (`src/DesktopUi.cpp:398-407`) keeps serving
  plugins.
- Window size/position and selected panel are **not persisted** today. Do not
  invent new INI keys in this migration (persistent-format change is out of
  scope); keep such UI state session-only or propose it separately.
- Plugin-owned data roots come from the paths service
  (`src/PluginPaths.hpp:57-118`, `include/GakuPaths.h`):
  `localRoot/<UserData|Configs|PluginData|Cache|Logs|Temp>/<plugin id>`.
  The webview profile belongs to the host (§4.5), not to a plugin namespace.

## 8. What was actually executed (read-only probes)

- `git status/branch/log/rev-parse/ls-files/remote` in the worktree (clean at
  start; `?? deps/webview2/` appeared mid-audit from parallel work; commit
  `0d18501f3eb2b157176c07de47f6857bb97e32e8`).
- Full reads of the build/CI/test/packaging scripts and the lifecycle sources
  cited above; line numbers verified against numbered output.
- `Resolve-GakuBuildTools auto` → v145/14.53.36919; `-PlatformToolset v143` →
  throws (no v143 installed). vswhere JSON; MSVC/SDK directory enumeration;
  SDK registry `InstallationFolder` = `D:\Windows Kits\10\` (10.0.26100).
- WebView2 runtime/Edge registry + file version queries (§6.1).
- `node --version`, `npm --version`, `npm config get registry`, `npm ping`
  (PONG 2540 ms), `dotnet --version`, `where.exe` probes.
- `Invoke-WebRequest` nuget.org service index (200), flat-container version
  index (contains 1.0.4314.35), `.nupkg.sha512` (404 ×2), search query
  (returned only latest prerelease; no hash).
- Zip inspection of the staged nupkg (nuspec + native entries); SHA256/SHA512
  of the nupkg; SHA256 comparison of vendored `WebView2.h` vs nupkg entry
  (match); `WebView2Loader.dll` version resource.
- **Skipped:** any MSBuild build/test execution, any game/UI launch, any
  deployment, any write other than this document, MinGW fixtures (no gcc).
  Hashes and probes certify files and tooling only — not runtime behavior in
  the game.

## 9. Blockers, risks, and open verification items

1. **No v143 locally** — local validation runs v145 while CI runs v143. Both
   are supported by the props, but keep an eye on v145-only `/W4` warnings and
   run the CI-equivalent gate before release. Not a hard blocker.
2. **Vendoring incomplete at snapshot** — `deps/webview2` lacks the static
   loader lib and `PROVENANCE.md` (§6.3); coordinate with the parallel agent.
   The nupkg's CDN hash endpoint 404 means provenance currently rests on the
   recorded local hashes + nuspec + version-index presence.
3. **Plugin panels stay ImGui** — ABI freeze (AGENTS.md). React scope is the
   manager page; the mode switch in §5.3 item 3 is the architectural hinge.
4. **Text-input focus gap** (§5.3 item 5) — must be closed or typing in React
   dispatches plugin hotkeys. Add a regression test (bridge event → poller
   suppression) to the new suite.
5. **File drop over the webview child** (§5.3 item 6) — `AllowExternalDrop(FALSE)`
   is the designed escape hatch but needs a live verification with the
   existing drop suites; elevated-host UIPI behavior must be re-checked in web
   mode.
6. **Shutdown races** — async env/controller creation vs `g_stop`, controller
   Close vs the 5 s UI join, orphaned browser processes on watchdog
   `TerminateProcess` (§5.4). All bounded by existing watchdog budgets; add
   explicit tests for create-during-shutdown.
7. **Stale-binary stamp roots** — add `deps/webview2` to
   `tests/test-common.ps1:31` when vendored (§3.1).
8. **CI runtime availability** — do not assume the windows-2022 runner has the
   WebView2 runtime; gate the smoke suite (§3.2) until verified.
9. **Docs gate** — any future edit to this file must keep relative Markdown
   links resolvable (currently none) and avoid retired-symbol strings in the
   two onboarding guides (§2.4).
10. **Single-file contract decision** — folder-mode assets change installation
    (§4.2); pick embedded mode before release if the contract must hold, and
    extend `wiki/Installation.md` accordingly (documentation change, not a
    format change).
