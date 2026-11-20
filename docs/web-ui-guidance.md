# GakuLoader Unified Web Panel UI Guidance

GakuLoader hosts plugin-owned WebView2 panels (`<pluginId>.web-panel/v1`, see [GakuWebPanel.h](../include/GakuWebPanel.h)) edge-to-edge inside the right-hand `##PluginContent` container with zero inner padding or border inset (`--gaku-radius-panel: 0px`).

At `100%` scale, GakuLoader's default window (`1220x820` outer, `200px` navigation sidebar) and WebView2 typography guidance are modeled on desktop NetEase Cloud Music proportions so plugin sidebars, track/asset tables, transport bars, and dialogs are crisp and readable without requiring `125%` zoom.

## 1. Host-Injected CSS Custom Properties

Before any plugin document script executes (via `ICoreWebView2::AddScriptToExecuteOnDocumentCreated`) and whenever host `uiScale`, `dpiScale`, or `theme` changes (via `ExecuteScript`), GakuLoader sets the following CSS custom properties on `document.documentElement.style`:

| Property | Value | Role |
|---|---|---|
| `--gaku-font-sans` | `'Microsoft YaHei UI', 'PingFang SC', 'Segoe UI', system-ui, -apple-system, sans-serif` | Primary UI sans stack |
| `--gaku-font-mono` | `'Cascadia Mono', 'Consolas', 'JetBrains Mono', monospace` | Monospace / timecode / path stack |
| `--gaku-ui-scale` | Stringified host `uiScale` (e.g. `1` at 100%, `1.25` at 125%) | Current user UI scale multiplier |
| `--gaku-text-micro` | `11px` | Micro badges / dense metadata |
| `--gaku-text-xs` | `12px` | Secondary captions / table sublabels |
| `--gaku-text-sm` | `13px` | Compact labels / secondary buttons |
| `--gaku-text-base` | `14px` | Default body & table row text |
| `--gaku-text-md` | `15px` | Emphasized labels / section headers |
| `--gaku-text-lg` | `18px` | Panel subheadings |
| `--gaku-text-title` | `22px` | Primary page/view titles |
| `--gaku-leading-tight` | `1.35` | Compact single-line / heading line-height |
| `--gaku-leading-base` | `1.5` | Body / multi-line line-height |
| `--gaku-control-height` | `34px` | Standard buttons & inputs |
| `--gaku-control-height-sm` | `28px` | Compact toolbar buttons & inputs |
| `--gaku-row-height` | `56px` | Standard media/track table row height |
| `--gaku-radius-panel` | `0px` | Outer edge-to-edge panel corner radius |

`document.documentElement.dataset.gakuUiScale` and `document.documentElement.dataset.gakuTheme` (`dark`, `sapphire`, `paper`, `apple-light`, `apple-dark`) are also kept in sync.

## 2. JavaScript Runtime Contract (`version: 1`)

GakuLoader also populates:

- `window.__GAKU_UI_GUIDANCE__`: `{ version: 1, uiScale, dpiScale, theme, themeName, typography: { ... }, metrics: { ... } }`
- `window.getGakuUiGuidance()`: returns `window.__GAKU_UI_GUIDANCE__`
- `window` event `'gaku:ui-guidance-changed'` (`CustomEvent` with `detail` equal to the updated `window.__GAKU_UI_GUIDANCE__` object)
