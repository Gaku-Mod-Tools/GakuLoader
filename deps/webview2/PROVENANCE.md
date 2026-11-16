# Microsoft Edge WebView2 SDK provenance

- Package: `Microsoft.Web.WebView2`, version `1.0.4314.35`.
- Publisher: Microsoft Corporation.
- Package page: https://www.nuget.org/packages/Microsoft.Web.WebView2/1.0.4314.35
- Immutable package URL: https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.4314.35/microsoft.web.webview2.1.0.4314.35.nupkg
- NuGet catalog leaf: https://api.nuget.org/v3/catalog0/data/2026.10.23.06.50.10/microsoft.web.webview2.1.0.4314.35.json
- Catalog hash algorithm: SHA512.
- Catalog `packageHash` (Base64): `cOaiVqP3BmQHMnxGpZzANM22tUXgycrisyJHjIGNzIaMsm8iXj7Kx+ceYg+RbYwJtzT009qfcFTocvVLY+ekMw==`.
- Local archive: `build/webview2-sdk/microsoft.web.webview2.1.0.4314.35.nupkg` (ignored build evidence).
- Local archive SHA256: `90778deb1b77e36292828b4b5833c9255c27533f40e0ddd55891ae718944edc4`.
- Local archive SHA512 (Base64) matches the catalog `packageHash` exactly.

## Selected vendored files

Only the Win32 C++ headers, the x64 dynamic loader, and the package license/notice files are retained.
The local paths below are relative to the repository root. SHA256 values are lowercase hex.

| Local path | NuGet archive entry | Bytes | SHA256 |
| --- | --- | ---: | --- |
| `deps/webview2/include/WebView2.h` | `build/native/include/WebView2.h` | 2951155 | `4e47763725f507235e8a12f35b9f41d7e348586153414c39fc1e9108875c181f` |
| `deps/webview2/include/WebView2EnvironmentOptions.h` | `build/native/include/WebView2EnvironmentOptions.h` | 19500 | `25a2de86c15fdc2fbb0e5d9cb01f17cadb8accc7ed4a5a41e875a5e7b866c991` |
| `deps/webview2/x64/WebView2Loader.dll` | `build/native/x64/WebView2Loader.dll` | 194912 | `d3fce90bdeecb08596e377dea6a316354dd8949e32ff09b89d51be00c14e21b6` |
| `deps/webview2/LICENSE.txt` | `LICENSE.txt` | 1487 | `0af8f1b807512aae39c2ac1aa4d0cae65cabecb6fd554b8439a5162a0d6eca55` |
| `deps/webview2/NOTICE.txt` | `NOTICE.txt` | 3894 | `106423785c5b7eba0a8e61d1837f2132e9c828e20ad530f565d981c1df60dd90` |

## Local changes and redistribution boundary

No local source or binary modifications are recorded. Each selected file above has the same SHA256 as
the corresponding entry in the verified NuGet archive. The vendored loader is the official x64
`WebView2Loader.dll`; its file and product version resources report `1.0.4314.35`.

The host does not link the loader import library. `src/WebView2Ui.cpp` dynamically loads
`WebView2Loader.dll` from the directory containing `version.dll` and resolves
`GetAvailableCoreWebView2BrowserVersionString` and `CreateCoreWebView2EnvironmentWithOptions`.
The SDK loader is not the Edge/WebView2 Evergreen Runtime. The target machine must have the Evergreen
Runtime installed separately; it is not bundled here.

The license is the Microsoft SDK BSD-3-Clause-style text in `LICENSE.txt`. `NOTICE.txt` retains the
package's third-party notices, including Antlr3.Runtime and StringTemplate4 BSD-3-Clause notices.
Both files are copied into release packages together with `THIRD_PARTY_NOTICES.md`.

No x86/ARM64 loaders, static loader libraries, managed assemblies, examples, or other NuGet payload
files are vendored. `.gitignore` explicitly keeps `deps/webview2/x64/WebView2Loader.dll` tracked while
continuing to ignore ordinary build output directories.

## Observed verification

On 2026-11-15, the local NuGet archive SHA512 was compared with the official catalog leaf and matched.
The five selected archive entries were hashed and compared with the vendored files. Windows
`Get-AuthenticodeSignature` reported `Valid` for the vendored loader, signed by
`CN=Microsoft Corporation` (certificate thumbprint `4028CAD637509D4744B17EC5B42AED8D7A31E6AF`).
That is a local signature-chain observation, not an independent legal ownership or clean-room
certification. The hashes freeze the selected bytes; they do not attest future upstream changes.
