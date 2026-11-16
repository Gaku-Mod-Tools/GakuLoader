# Third-party source and licenses

First-party code selected from source revision `6db0f56440b183d37b57c467d68b452d13eb3d47` retains the
repository's [MIT license](LICENSE). This records the supplied licensing declaration, not an independent ownership or clean-room certification.

| Component | Declared upstream base | License and retained notices | Local changes |
| --- | --- | --- | --- |
| Dear ImGui | `ocornut/imgui`, `f3f6295d532b98e322691afb8ef418209d963786` (`v1.89.4`) | [MIT license](deps/imgui/LICENSE.txt); embedded stb notices remain in source headers | Existing D3D11 geometry-shader class-instance release correction; see [provenance](deps/imgui/PROVENANCE.md) |
| MinHook | `TsudaKageyu/minhook`, `8af6b4acae5a9388fd742b56fa79ece89d96f823` | [Full BSD-2-Clause / HDE32 / HDE64 license](deps/minhook/LICENSE.txt), Tsuda Kageyu and Vyacheslav Patkov | Existing per-target batch hook/relocation extensions; see [provenance](deps/minhook/PROVENANCE.md) |
| Microsoft Edge WebView2 SDK | `Microsoft.Web.WebView2` NuGet package `1.0.4314.35` | [Microsoft SDK BSD-3-Clause-style license](deps/webview2/LICENSE.txt) and [third-party notices](deps/webview2/NOTICE.txt) | None; selected headers, notices, and x64 `WebView2Loader.dll` match the verified NuGet archive; see [provenance](deps/webview2/PROVENANCE.md) |
| nlohmann/json | `nlohmann/json`, `9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03` (`v3.11.3`) | [MIT license](deps/nlohmann/LICENSE.MIT), Niels Lohmann | None; the vendored single header and license match upstream; see [provenance](deps/nlohmann/PROVENANCE.md) |

Only the core/source/header and Win32/D3D11 backend files required by the shipped projects and synthetic tests are vendored,
plus the nlohmann/json single header and the explicitly recorded x64 `WebView2Loader.dll`.
No other upstream build outputs, example binaries, game assets, private models, audio, keys or personal configurations are included.
The WebView2 SDK loader is redistributed under its retained license and notices; the separate Edge/WebView2 Evergreen Runtime is not bundled.
Vendor directories are ordinary tracked files; no submodule initialization or neighboring checkout is required.
The ImGui/MinHook recorded bases come from source-project gitlinks; the supplied local directories were patched copies, not separately authenticated upstream checkouts.
The WebView2 and nlohmann/json provenance records instead bind the selected bytes to immutable upstream package/raw-file URLs and SHA256 comparisons.
Per-file hashes freeze the actual selected source and the one selected binary. No independent ownership attestation is implied.
