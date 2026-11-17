# WebView2 migration notes (superseded)

The earlier Manager-wide WebView2/React migration notes are superseded by
[webview2-scope-correction-audit-20261115.md](webview2-scope-correction-audit-20261115.md).
The corrected Loader host keeps the Manager, sidebar, settings and status bar in
ImGui, embeds WebView2 only inside the selected plugin's `##PluginContent`
rectangle when that plugin provides `<id>.web-panel/v1`, and consumes the plugin-supplied
assets entry (`GakuWebPanelAssetsV1`) directly without owning or packaging plugin UI assets.
GakuMotion's React frontend and `GakuMotion.UI/` packaging live in the `GakuMotion` repository;
`frontend/` here retains only the isolated Manager prototype. See [webview2-build.md](webview2-build.md).

## Where the referenced material lives

- Architecture and deviation analysis: the scope-correction audit, sections 2, 6 and 7.
- Deviation index D1-D12: [ui-migration-matrix.md](ui-migration-matrix.md),
  "Implementation status & evidence".
- Bridge reference: [native-bridge-api.md](native-bridge-api.md).
- Verification boundaries: scope-correction audit section 11 and the local
  `build/tests/report.json` produced by `tests/run-all.ps1`.
- Manual in-game checklist: scope-correction audit section 9.2. No game launch,
  deployment or visual acceptance is claimed by this note.
