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
