# GakuLoader source snapshot instructions

This independent source repository defaults to local source validation only.

- Read [STAGING_REVIEW.md](STAGING_REVIEW.md), [SOURCE_SELECTION.json](SOURCE_SELECTION.json), and [SOURCE_MANIFEST.json](SOURCE_MANIFEST.json) before changing the frozen snapshot.
- Preserve the C ABI in `include/GakuPlugin.h`; do not change existing field offsets, enum values or function signatures.
- Use MSVC v143/v145, C++20, UTF-8 and static CRT. Do not enable Control Flow Guard for the host.
- Dependencies are vendored source, with their own licenses and documented local changes. Do not silently replace them with upstream copies.
- Default validation is `tools/validate-source.ps1`; it only builds and runs synthetic headless checks. Build products and logs belong under ignored `build/`.
- Do not install, deploy, start or control a game; do not access real user assets or personal configurations under this default scope.
- A source change, local commit, remote modification, publication or deployment requires the current task's authorization; this file grants none of those actions automatically.
- Preserve existing work and history. Do not reset, overwrite, delete or rewrite unrelated files or commits.
- Report actual commands, results and skipped checks. Source hashes and local build records do not certify game behavior or legal ownership.
