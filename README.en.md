# GakuLoader

Windows x64 native plugin host source for the PC DMM client of Gakuen Idolmaster. The host provides version proxying, lifecycle, main-thread tasks, hook ownership, hotkeys and an F1 desktop manager.

This is an independent frozen source snapshot, with no prebuilt installation package. The default scope is local builds and synthetic headless validation only. It does not install into or control a game.

Install Visual Studio Desktop development with C++ (MSVC v143/v145) and a Windows SDK. Required third-party source is vendored; no submodule or adjacent repository is needed.

```powershell
.\tools\validate-source.ps1
```

The entry builds Release/Debug and MSVC test projects, then runs docs, frame pump, C ABI, MinHook isolation, the standalone plugin and package checks. Outputs and logs stay in ignored `build/`.
MinGW cross-compiler, interactive desktop and game acceptance checks are excluded by default.

[Build](wiki/Building.md) · [Plugin development](docs/PLUGIN_DEVELOPMENT.md) · [Review](STAGING_REVIEW.md) · [File hashes](SOURCE_MANIFEST.json) · [Licenses](THIRD_PARTY_NOTICES.md)

Business plugins such as GakuMotion.dll and GakuLocalify.dll are not included. First-party source retains the supplied MIT declaration; vendored components retain their own licenses and local patches.

[简体中文](README.md) · [日本語](README.ja.md)
