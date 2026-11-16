# nlohmann/json provenance

- Project: `nlohmann/json`.
- Version tag: `v3.11.3`.
- Tag commit: `9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03` (GitHub tag ref resolves directly to this commit).
- Tag ref API: https://api.github.com/repos/nlohmann/json/git/ref/tags/v3.11.3
- Source tree: https://github.com/nlohmann/json/tree/v3.11.3
- Single header URL: https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp
- License URL: https://raw.githubusercontent.com/nlohmann/json/v3.11.3/LICENSE.MIT
- License: MIT, copyright Niels Lohmann, retained in `LICENSE.MIT`.

## Selected vendored files

| Local path | Upstream path at `v3.11.3` | Bytes | SHA256 |
| --- | --- | ---: | --- |
| `deps/nlohmann/json.hpp` | `single_include/nlohmann/json.hpp` | 919975 | `9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6` |
| `deps/nlohmann/LICENSE.MIT` | `LICENSE.MIT` | 1076 | `86b998c792894ccb911a1cb7994f7a9652894e7a094c0b5e45be2f553f45cf14` |

The header's `NLOHMANN_JSON_VERSION_MAJOR/MINOR/PATCH` macros report `3.11.3`.
Only the amalgamated single header and its license are vendored; the full upstream repository,
tests, examples, and build machinery are not included.

## Local changes and observed verification

No local source modifications are recorded. On 2026-11-15, both upstream files were downloaded from
the immutable `v3.11.3` raw URLs into ignored `build/provenance-audit/` and their SHA256 values matched
the vendored files exactly. The GitHub tag ref API resolved `v3.11.3` to the commit recorded above.

The hashes freeze the selected bytes. They are not a signature, independent ownership attestation,
or claim about unvendored upstream files.
