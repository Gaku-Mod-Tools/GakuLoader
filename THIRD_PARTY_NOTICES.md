# Third-party source and licenses

First-party code selected from source revision `6db0f56440b183d37b57c467d68b452d13eb3d47` retains the
repository's [MIT license](LICENSE). This records the supplied licensing declaration, not an independent ownership or clean-room certification.

| Component | Declared upstream base | License and retained notices | Local changes |
| --- | --- | --- | --- |
| Dear ImGui | `ocornut/imgui`, `f3f6295d532b98e322691afb8ef418209d963786` (`v1.89.4`) | [MIT license](deps/imgui/LICENSE.txt); embedded stb notices remain in source headers | Existing D3D11 geometry-shader class-instance release correction; see [provenance](deps/imgui/PROVENANCE.md) |
| MinHook | `TsudaKageyu/minhook`, `8af6b4acae5a9388fd742b56fa79ece89d96f823` | [Full BSD-2-Clause / HDE32 / HDE64 license](deps/minhook/LICENSE.txt), Tsuda Kageyu and Vyacheslav Patkov | Existing per-target batch hook/relocation extensions; see [provenance](deps/minhook/PROVENANCE.md) |

Only the core/source/header and Win32/D3D11 backend files required by the shipped projects and synthetic tests are vendored.
No upstream build outputs, example binaries, game assets, private models, audio, keys or personal configurations are included.
Vendor directories are ordinary tracked files; no submodule initialization or neighboring checkout is required.
The recorded bases come from source-project gitlinks; the supplied local directories were patched copies, not separately authenticated upstream checkouts.
Per-file hashes freeze the actual selected source. No signature or ownership attestation is implied.
