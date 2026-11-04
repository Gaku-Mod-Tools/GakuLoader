[简体中文](README.md) | **English** | [日本語](README.ja.md)

# GakuLoader

Documentation: [Wiki home](wiki/Home.md) · [Quickstart](docs/QUICKSTART.md) · [All chapters](wiki/_Sidebar.md).

A lightweight native DLL loader for *Gakuen Idolmaster* (学園アイドルマスター, DMM PC version, Unity 6000.0.x IL2CPP x64). It injects into the game process by acting as a proxy `version.dll` in the game root directory, providing plugin loading, hotkey dispatching, Unity main-thread task scheduling, exclusive detour hook leases, and a standalone F1 debug window.

The host stays minimal to arbitrate shared resources across plugins; mod-specific logic is implemented independently by plugins.

> **Note on Documentation**: The project Wiki is currently written and maintained in **Simplified Chinese**. Translated versions of the Wiki will be provided once the project specifications and interfaces stabilize.

## Installation

1. Exit the game completely.
2. Place the built `build/version.dll` into the game root directory (alongside `gakumas.exe`).
3. Place plugin DLLs into the `gakumas-local/plugins/` directory.

```text
<Game Root Directory>/
├── gakumas.exe
├── version.dll                    GakuLoader binary
├── gakuloader.ini                 Optional configuration
└── gakumas-local/
    ├── gakuloader.log             Runtime log
    └── plugins/                   Plugin directory
        ├── GakuMotion.dll
        └── GakuLocalify.dll
```

After launching the game, press **F1** to toggle the GakuLoader control window. Closing the window via the top-right button minimizes it to the system tray; right-click the tray icon to reopen it.

## Configuration

`gakuloader.ini` in the game root directory configures plugins, hotkeys, and diagnostic options. If the file is omitted, default values are used:

```ini
[plugins]
; Disable specific plugins (case-insensitive filename; defaults to true)
SomePlugin.dll=false

[hotkeys]
; Legacy format: <pluginId>.<actionId>=<KeyCombo> (applies only to a unique match)
gakumotion.show-gui=Ctrl+Shift+M
; Leave empty to unbind
someplugin.action=

[host]
; Set to true to allocate a separate console window showing real-time combined logs
console=false

[diagnostics]
; Set to true to record crash stack traces to crash.log
trace=false
```

Use **Manager → Hotkeys** to rebind keys. Its saved entries distinguish dots and case in IDs and take precedence over the legacy format; Reset follows the plugin's current default. Legacy entries are preserved; ambiguous matches are ignored and logged. See [Hotkeys](wiki/Hotkeys.md).

Logs are written to `gakumas-local/gakuloader.log`. Restart after manually editing configuration or updating plugins. Manager settings marked as immediate apply directly; plugins do not support hot-reloading.

## Supported Plugins

- **GakuMotion**: Supported (motion, camera, and bone animation driver).
- **GakuLocalify**: Supported (text localization and UI replacement).
- **Custom plugins**: Any x64 DLL exporting `GakuPlugin_Query` adhering to `include/GakuPlugin.h`.

Legacy DLLs built against retired interfaces (`GmlPlugin_Query` / `GakumasModPlugin.h`) will not be loaded. Please rebuild them with the current header file.

## Developing a Plugin

Use the complete [minimal plugin source](examples/minimal-plugin/MinimalPlugin.cpp). Its standalone build needs MSVC and the current header, without MinGW test fixtures:

```powershell
.\examples\minimal-plugin\build.ps1
.\examples\minimal-plugin\verify.ps1
```

The verifier checks query, activation and lifecycle logs in a synthetic host without launching the game. This is an offline integration check.

For lifecycle callbacks, threading models, and a complete example, see [Writing a Plugin](wiki/Writing-a-Plugin.md) on the Wiki.

## Building

Install Desktop development with C++ and a Windows SDK. Supported toolsets are VS 2022 / MSVC v143 and VS 2026 / MSVC v145. Scripts select an installed toolset automatically; use `-PlatformToolset v143` to select one explicitly:

```powershell
# Build Release binary (output to build/version.dll)
.\build.ps1

# Check documentation, ABI and the standalone minimal plugin
.\tools\test-docs.ps1
```

> The host strictly requires `/MT` (static CRT) and Control Flow Guard (`/guard:cf`) must be disabled, as MinHook dynamic trampolines trigger CFG aborts. Building the host with MinGW is not supported.

The build only produces local artifacts. `-OutputDll` copies to an explicit candidate path; it does not discover a game directory or deploy automatically. See [Building](wiki/Building.md) for the full test matrix and optional MinGW fixtures.

Required ImGui and MinHook source is included; no submodule initialization is needed. Install Loader and business plugins using matching product bundles.

## Documentation (Wiki)

Detailed implementation contracts, API references, and troubleshooting guides are available on the Wiki (*currently in Simplified Chinese*):

- [Installation](wiki/Installation.md) - Installation, backup, removal, and deployment responsibilities
- [Configuration](wiki/Configuration.md) - Configuration sections and supported key syntax
- [Writing a Plugin](wiki/Writing-a-Plugin.md) - Minimal plugin implementation and compiler constraints
- [Plugin API](wiki/Plugin-API.md) - Lifecycle callbacks and host-provided services
- [Hooks](wiki/Hooks.md) - Exclusive MinHook leases and shared LateUpdate participants
- [UI](wiki/UI.md) - Standalone D3D11 window and ImGui rendering
- [Hotkeys](wiki/Hotkeys.md) - Physical key polling, focus checks, and conflict resolution
- [Building](wiki/Building.md) - Compiler flags, artifact isolation, and test suites
- [Troubleshooting](wiki/Troubleshooting.md) - Diagnostic logging and exit watchdog behavior
