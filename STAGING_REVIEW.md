# Frozen source selection and local validation review

Source revision: `6db0f56440b183d37b57c467d68b452d13eb3d47`. The source checkout was clean when selected.
This snapshot is an explicit file selection, not a repository or history clone. [SOURCE_SELECTION.json](SOURCE_SELECTION.json)
records original selected bytes; [SOURCE_MANIFEST.json](SOURCE_MANIFEST.json) freezes the staged payload after the adaptations below.

## Included scope

- Host/API: `src/`, `include/`, shared MSBuild configuration, host/ImGui/MinHook projects and the solution.
- Build and checks: host build, MSVC discovery, synthetic test projects and runners, independent minimal plugin, local docs/package checks, and a source-only validation entry.
- Public material: three READMEs, public `docs/` and `wiki/`, MIT declaration and third-party notices.
- Vendor: all 29 supplied ImGui core/Win32/D3D11 and MinHook/HDE source/header/license/provenance files required by the projects; additionally the full upstream MinHook/HDE license was retained in this snapshot.

## Deliberately excluded

Agent memory/history, research evidence, game/UI automation tools, personal configuration, real fixtures/materials, Git metadata/remotes,
submodule registrations, caches and prebuilt artifacts are excluded. The original `AGENTS.md` was replaced with source-only instructions;
no historical deployment, remote-write or history-deletion authority carries into this repository.

## Staging adaptations

- Vendor files are ordinary tracked source; no adjacent checkout or submodule download is needed. The build missing-file message and local CI checkout match that layout.
- READMEs and public entry points identify this as source, keep default validation headless, and distinguish future manual binary installation from this task's scope.
- The MinHook notice is corrected to BSD-2-Clause and the full MinHook/HDE32/HDE64 text is retained. Existing source copyright headers are unchanged.
- New source-selection/hash checks and `tools/validate-source.ps1` record validation against the clean local source commit and manifest hash.
- `.gitattributes` preserves exact source bytes; `build/` and its generated binaries/logs/receipts are ignored.

## Provenance and license limits

The first-party MIT declaration is retained; selecting a file is not an independent ownership/clean-room attestation.
ImGui base `f3f6295d532b98e322691afb8ef418209d963786` and MinHook base `8af6b4acae5a9388fd742b56fa79ece89d96f823`
are source-project gitlink declarations. The supplied dependency directories had no independent Git metadata and contain documented local patches.
They are not described as pristine upstream checkouts. The actual supplied bytes are hash-frozen, including those patches.

The official fixed-revision license pages were read through the available web tool. MinHook text was normalized to UTF-8/LF and includes all HDE terms.
The GitHub tree API was unavailable through that tool; direct shell HTTP was denied by the environment. No upstream-object/blob authentication is claimed.
This boundary does not replace the complete retained license texts or the explicit local hashes.

## Validation scope

Run `tools/validate-source.ps1` after the initial clean source commit. It builds Release/Debug plus existing MSVC test projects and executes
only docs/frame-pump, C ABI, MinHook isolation, independent minimal plugin and local package checks. It records logs and a build receipt under ignored `build/validation/`.
A built test project is not reported as a test execution. MinGW, actual desktop windows, game loading/acceptance and remote CI are excluded.
Results, if subsequently recorded, are in `VALIDATION.md`; this selection review alone does not assert that a build succeeded.

## Documentation restoration after the initial snapshot

The public READMEs and user guides were restored from mature GakuLoader documentation at `6db0f56440b183d37b57c467d68b452d13eb3d47`.
The initial snapshot incorrectly replaced those product entry points with internal audit/source-validation framing.
All eleven repository Wiki files had remained present; navigation and ordinary Markdown compatibility were repaired without claiming a separate GitHub Wiki migration.
See `DOCS_RESTORATION.md` for exact source commits, path/hash records, checks and the documentation-only boundary.
This internal selection review is not the public product homepage.
