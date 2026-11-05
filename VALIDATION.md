# Observed standalone source validation

Status: **PASS** for the selected local source-only scope.

- Built clean source commit: `035aacb5ebfa694f1a5526bd44c167f3e3ede2a9`.
- Source manifest SHA256: `f663fd4e89ff058eac1ee11cdf11d50928dc0e9706fc89c3eac4c88316b24f67`.
- Receipt: `build/validation/receipt.json` (ignored local evidence, not distributed source or a signature).
- Exact entry: `tools/validate-source.ps1`.
- Build artifacts and all logs remain under ignored `build/`; no prebuilt DLL is tracked.

| Actual step | Result | Preserved local evidence |
| --- | --- | --- |
| Release host build | PASS | `build/validation/release-build.log` |
| Debug host build | PASS | `build/validation/debug-build.log` |
| MSVC test build, `-SkipFixtures` | All 8 existing test projects built; this is not 8 executed suites | `build/validation/msvc-test-build.log` |
| `run-all.ps1 -SkipBuild -SkipFixtures -Only docs,frame-pump` | Two suites executed, both PASS | `build/tests/report.json`, `build/validation/headless-suites.log` |
| C ABI executable | Exit code 0; intentionally silent log | `build/validation/c-abi.log`, receipt |
| MinHook isolation executable | PASS | `build/validation/minhook-isolation.log` |
| Independent minimal plugin | Real DLL query/rejection, activation, ready, tick and shutdown PASS in synthetic host | `build/validation/minimal-plugin.log`, `build/examples/minimal-plugin/Release/activation.log` |
| Local DLL package check | x64, 17 version proxy exports and no dynamic CRT dependency PASS | `build/validation/package.log` |
| Frozen source check, before and after | All 140 listed source/doc/vendor files match; no extra payload | `SOURCE_MANIFEST.json` |

Observed artifact hashes:

| Artifact (ignored local output) | SHA256 |
| --- | --- |
| `build/version.dll` | `2033c0d4dacc6922d34eb7d614d3335f4580291bbce7cc8bba535e20f6fbf112` |
| `build/debug/version.dll` | `f72c8948ca1369f1105a9401e89bb1ad9b079da5aaeb74edcea00eebc7dc1207` |
| `build/examples/minimal-plugin/Release/MinimalPlugin.dll` | `aef8ab74b10d10aeec386a8fdc48704c5580d9e8d0ee0735cab928ff70542b8f` |

## Limits and retained review notes

- The run used the approved ordinary local process environment because the MSVC PDB subsystem had previously failed in the restricted process environment. No execution policy, installed tool, credential or persistent permission was changed.
- MinGW heterogeneous fixtures, actual desktop UI/window suites, game loading/acceptance and remote CI were excluded. Local v143 was unavailable in the earlier environment check; the configured v143 CI has not been run here.
- Existing compiler warnings remain. This is not a zero-warning or all-platform certification.
- The initial import retained CRLF bytes. Its first Git whitespace check flagged CR as end-of-line whitespace; the source attributes were corrected to recognize CRLF without changing copied vendor/source bytes. The preparation/final diffs passed afterward.
- The first clean-source run at `89ac574a02d5effb8660c0a1385150a7c9104131` passed and is retained under `build/validation-initial-89ac574/`. The full MinHook license was then normalized to the documented UTF-8/LF form, its provenance boundary clarified, and the manifest re-frozen. The final incremental run above binds the final source commit and manifest.
- Third-party base revisions are declarations plus retained local variants; upstream pristine parity and independent ownership are not certified. Complete licenses and per-file selected/staged hashes are included.
- This is an unsigned observed local build, not a reproducibility, publication or game-behavior attestation. No game, real material, neighboring checkout or remote service was used by validation.
- This results document is explicitly outside the source manifest payload; adding it does not alter the 140 hash-listed source files or their built inputs.

## File-drop refresh observed validation (2026-11-05)

Status: **PASS** for the selected local source and synthetic file-drop scope.
The earlier results above remain historical; the refreshed manifest contains 172 files.

- Clean source checkpoint: `8ff4b53ad784e67b165658b37686aa6346d84c07`.
- Selected source fixes: `038bd02fe326f767f0afd55b0ee5bc83196ba9fd` and `6c8938990b2b08cc6079c61f3cefe29b67acd698`.
- Source manifest SHA256: `21e421081f17f38e37ce17fe00880ab34062b5d070b2073ece7bc5bb58da1c60`.
- Exact default entry: `tools/validate-source.ps1`; its receipt is `build/validation/receipt-file-drop-20261105.json`.
- Additional exact entry: `tests/run-all.ps1 -Configuration Release -SkipBuild -SkipFixtures -Only desktop-file-drop,desktop-ui`.
- Independent blob/hash review confirmed that only the four selected source/test files and the associated selection/review metadata changed. ABI headers, projects and vendored dependencies retained their existing bytes.

| Actual check | Observed result | Local evidence |
| --- | --- | --- |
| Release and Debug host builds | Both PASS with MSVC | `build/validation/release-build.log`, `build/validation/debug-build.log` |
| MSVC test project build, `-SkipFixtures` | PASS; compilation is distinct from suite execution | `build/validation/msvc-test-build.log` |
| Default language, font, appearance, docs, paths and frame-pump suites | 6 PASS, 0 skipped | `build/tests/report-source-validation-file-drop-20261105.json` |
| C ABI, MinHook isolation, minimal plugin and DLL package checks | All PASS | `build/validation/receipt-file-drop-20261105.json` and its per-step logs |
| File-drop broker contracts | 9 groups PASS, including rejection reasons and pending-batch preservation | `build/desktop-file-drop-tests/desktop-file-drop-tests.log` |
| Standalone hidden WARP desktop smoke and startup-failure cancellation | PASS; two scoped UIPI exceptions, other-window isolation, Unicode batch routing, outside-target rejection and exactly-once consumption observed | `build/desktop-ui-smoke.log` |
| Additional file-drop suite report | 2 PASS, 0 skipped | `build/tests/report-file-drop-refresh-20261105.json` |
| Frozen source check before and after default validation | 172 files PASS, no unreviewed payload | `tools/check-source-manifest.ps1` |

| Artifact (ignored local output) | Bytes | SHA256 |
| --- | ---: | --- |
| `build/version.dll` | 932864 | `09d7526770af8b9743b01a89c24245ee094bc87c8db09e2a4fdbe72c3cab7151` |
| `build/debug/version.dll` | 5652480 | `2d7be934fcd4891c4375a2786ef8e5406cc8183e915d28132fdab7a94d0abd46` |

The default source validator remained headless; the separately requested file-drop smoke used only a
synthetic hidden standalone window and generated path strings, with no game or real user asset.
Actual Explorer dragging, game loading, deployment, MinGW heterogeneous fixtures and remote CI were
not executed by this refresh. The unadopted third-message `WM_COPYDATA` experiment is excluded.
These are unsigned local build/test observations, not reproducibility or game-acceptance claims.
Previous default validation evidence was retained under `build/validation-before-file-drop-20261105/`.
