# User documentation restoration

Baseline: `775107a5d78daa87ee9dc1ae78be2a0df0235255`.
Mature documentation source: GakuLoader `6db0f56440b183d37b57c467d68b452d13eb3d47`.
Historical cross-check: GakuModLoader `2183b0676728a22516eb1ed582b0212dc8b3e61b`.

## Cause and repair

The initial source import replaced the mature product READMEs with short audit/source-only entry points. Chinese README shrank from 106 to 34 lines and English/Japanese from 108 to 20; their complete installation/configuration/Wiki navigation was lost.
The eleven `wiki/*.md` files were not omitted: all existed in the baseline, including Home and _Sidebar. Those two files matched the mature sources byte-for-byte. Building and Installation had additionally acquired source-audit framing. The Home/sidebar also retained independent-Wiki `[[label|Page]]` syntax, which was not a working ordinary repository Markdown navigation scheme.

The repair restores the mature three-language product homepage and Quickstart, removes audit/authorization front matter from user-facing Installation/Quickstart, retains correct MSVC build parameters and vendor layout, and makes all eleven Wiki files directly reachable from every README. Wiki pages use standard relative Markdown links and return navigation. A task-oriented `docs/README.md` index was added.
The Quickstart uninstall wording was aligned with Installation: removing Loader stops its loading; an earlier third-party proxy requires restoring its backup, not a blanket promise of restoring an official installation.

This repair only confirms repository documentation. It does not claim that a separate GitHub Wiki repository/service has been migrated, published or recovered.

## Provenance

`SOURCE_SELECTION.json` contains a `documentation_restoration` record for every restored file, with the full source commit, original source path and actual source SHA256. The new docs index and this restoration record are identified as new documentation, not invented upstream files or ownership approvals.
`SOURCE_MANIFEST.json` was refreshed for the actual documentation/metadata bytes. Runtime code, public headers, vendor, projects, scripts, tests and workflow remain unchanged from the baseline. Earlier build/clone receipts remain historical evidence for their recorded source commits; this document does not relabel them as new builds.

## Checks actually performed

- Existing `tools/test-docs.ps1` completed successfully, including real MSVC builds of the independent minimal plugin and verifier, ABI rejection/output-preservation and synthetic activate/ready/tick/shutdown logs.
- Static documentation checks, all local links and documented supported build parameters passed. A separate read-only reviewer checked 134 rendered local Markdown targets and all eleven Wiki pages, with no missing targets or audit/authorization front matter.
- Every language README directly links to all eleven Wiki files. Independent-Wiki bracket syntax was converted to ordinary Markdown in the repository pages.
- Protected code/project/script/test/workflow diff against the baseline is empty.
- No whole-host C++ test matrix was repeated for this documentation-only change. No game, deployment, binary release, visibility or remote operation occurred.

Local verification evidence is retained outside the public source payload in workspace `build/staging_validation/GakuLoader_docs_validation/docs-and-minimal-sdk.log`. This record is not a game-acceptance claim.
