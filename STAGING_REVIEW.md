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

## Paths service refresh (2026-11-02)

Seventeen source, SDK, example, test and developer-guide files are selected byte-for-byte from GakuLoader commit `0426648bd0601486347513af200aa4167a9fa09c`.
`SOURCE_SELECTION.json` records each selected source hash and preserves the earlier snapshot and documentation-restoration provenance.
The three public READMEs and all eleven Wiki files retain their previous bytes; the documentation index adds one link to `docs/PLUGIN_PATHS.md`.
The ordinary vendored dependencies, build entry point and CI adaptations remain in place. The stage validator adds the paths suite to its existing default selection.

The refreshed x64 Release DLL, production paths fixtures, C ABI executable, static example/documentation consistency and Markdown links, manifest hashes, and package export/static-CRT checks passed locally.
The new DLL SHA-256 is `A7E848BCAC23DC33E91350D3522D9D22DFF12C0993BD0523D851A7F698B3E43C`.
Build logs and the validation receipt are in workspace `build/staging_refresh_20261102/Loader`; the earlier ignored `build/version.dll` bytes and timestamp remain unchanged.
The existing full test matrix, Debug, game/deployment checks and remote CI were not rerun for this refresh. Earlier validation records remain historical evidence.

## Manager, presentation extensions and hotkey identity refresh (2026-11-04)

The public delta from GakuLoader `0426648bd0601486347513af200aa4167a9fa09c` through `ed5ad70819907c0a0f5acc25e804b0aea205fa57` is selected into this independent snapshot. The feature branches `7cb03c3cd26a3ebb08a3ead8c0406ce2c23c308c` (five themes and scoped file drops) and `2745faaabc0f399f483af54df3739aea337032cc` (Manager tabs/settings/languages and font rasterization) use their resolved merge at `a619b98a386fb373eb149f65f472e4410c253274`. The final hotkey identity correction is included. Internal DevAgents/deployment records and game automation tools remain excluded.

Selected code, SDK and tests use the fixed source commit's Git blob bytes. Four public SDK headers use canonical LF; existing struct layouts, function signatures and enum values remain unchanged, with Apple theme values appended. The optional file-drop and language services have separate headers. Hotkey persistence retains the exact original plugin/action identity and accepts a legacy dotted alias only when unique; Reset persists a marker following the plugin's current default. Truncated INI sections are rejected as a whole.

Public documentation retains this snapshot's restored README/Wiki navigation, vendored build instructions and uninstall wording while adding the selected functionality. Its wording distinguishes immediate Manager changes from manual configuration edits requiring restart. Vendored dependencies, complete licenses, source-only instructions and CI layout are retained. The default source validator additionally executes the new language/font/appearance headless suites; it still excludes actual desktop windows and game acceptance.

`SOURCE_SELECTION.json` records the source Git objects, selected/staged hashes and adaptations; `SOURCE_MANIFEST.json` freezes the resulting payload. This refresh must be committed before its final local builds. New build/test receipts are recorded outside the source repository under workspace `build/staging_refresh_20261104/Loader`; older validation and deployment records remain historical. No game behavior or remote CI result is inferred from those local records.

## File-drop UIPI and diagnostic refresh (2026-11-05)

Only four existing public source/test files are selected byte-for-byte from fixed GakuLoader snapshot
`6c8938990b2b08cc6079c61f3cefe29b67acd698`, containing repairs `038bd02fe326f767f0afd55b0ee5bc83196ba9fd`
and `6c8938990b2b08cc6079c61f3cefe29b67acd698`. The baseline staged blobs were checked against their
previous selected source. This is a minimal repair selection, not a repository/history merge.

The standalone control HWND permits `WM_DROPFILES` and Shell global-memory transport `0x0049` across
the UIPI boundary, with per-message configuration results. Drop diagnostics report entry coordinates,
file counts, target/owner acceptance or rejection reasons, and successful batch consumption. They do not
print file paths or empty per-frame polls. Mailbox ownership, bounds, limits and one-time consumption
remain intact; the existing Boolean accept method and frozen plugin ABI are preserved.

Synthetic regressions cover the two per-HWND message exceptions and unrelated-window isolation,
Unicode multi-file routing, rejection diagnostics, and preservation of a pending batch. The discarded
third-message `WM_COPYDATA` experiment is excluded; it has no demonstrated causal role in this repair.
The game-side import-queue fix belongs to GakuMotion and is outside this Loader source selection.

`SOURCE_SELECTION.json` preserves the four previous selection records and records the exact fixed
source commits, Git blobs and selected/staged hashes. `SOURCE_MANIFEST.json` re-freezes the resulting
172-file payload. SDK headers, projects, dependencies, licenses, public navigation and independent
staging/build adaptations are unchanged. The source checkpoint is committed before final validation,
as required by `tools/validate-source.ps1`; current observed results are appended to `VALIDATION.md`.
Validation uses only this source repository and synthetic fixtures. No game, real user asset, deployment,
remote write or package ZIP operation is part of this refresh.

## Manager animation and startup cancellation refresh (2026-11-06)

Fourteen public source, SDK, synthetic-test and developer-guide files are selected
byte-for-byte from fixed GakuLoader repair commit `6c4ce365d731f491f129c84f2abb9ccf9c116754`.
Every existing staged file matched that source commit's parent before selection;
only the new independent `include/GakuLifecycle.h` table adds a path.
Internal DevAgents history, unrelated development features and deployed binaries
are excluded. Vendored dependencies, build/CI adaptations, public README/Wiki
bytes and the frozen `GakuPlugin.h` ABI remain unchanged.

The Manager renders copied immutable status while plugin initialization holds the
callback gate. Selected plugin pages keep complete-image admission to avoid
contention flashes. The optional owner-bound `gakuloader.lifecycle` v1 table
lets runtime callbacks observe shutdown without taking the callback gate. Exit
publishes cancellation before waiting for workers, skips subsequent runtime
callbacks, and detaches the runtime worker before installing further host hooks.
The normal plugin order and pre-Vuplex synchronization boundary are unchanged.

`SOURCE_SELECTION.json` retains earlier per-file provenance and records all fixed
Git blobs and hashes for this selection; `SOURCE_MANIFEST.json` freezes the
resulting 173-file payload. The source checkpoint is committed before default
clean-source validation. Additional Manager animation, selected-panel retention,
real host cancellation and cross-DLL WARP checks are synthetic; no game, real
user assets, deployment, remote write or package publication is included.
Observed results are appended to `VALIDATION.md` after execution.
