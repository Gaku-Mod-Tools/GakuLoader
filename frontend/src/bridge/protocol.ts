// Typed mirror of the native Manager bridge (src/ManagerBackend.cpp). The native
// backend is authoritative; these types describe the fixed allow-list of
// commands/queries and the change events it pushes. See docs/native-bridge-api.md.

export interface PluginDependency {
  kind: number
  pluginId: string
  displayName?: string
  versionRange?: string
  targetState: number
  actualVersion?: string
  satisfied: boolean
  cycleDegraded?: boolean
  diagnostic?: string
}

export interface PluginPanel {
  id: string
  name: string
  version: string
  activated: boolean
  runtimeReady: boolean
  failed: boolean
  hasPanel: boolean
  loadState?: number
  dllFilename?: string
  statusReason?: string
  hasMetadata?: boolean
  originalAuthor?: string
  maintainers?: string[]
  subVersion?: string
  releaseChannel?: string
  summary?: string
  localizedSummaries?: Record<string, string>
  repositoryUrl?: string
  releaseUrl?: string
  issuesUrl?: string
  license?: string
  minLoaderVersion?: string
  dependencies?: PluginDependency[]
}

export interface HostSnapshot {
  available: boolean
  plugins: PluginPanel[]
  pluginFiles: string[]
  runtimeReady: boolean
  shuttingDown: boolean
  activated: boolean
}

export interface Appearance {
  theme: number
  uiScale: number
  scaleLimited: boolean
  dpiScale: number
}

export interface Language {
  language: number
  tag: string
  name: string
  names: string[]
}

export interface Strings {
  language: number
  strings: Record<string, string>
}

export interface HotkeyRow {
  pluginId: string
  actionId: string
  displayName: string
  combo: string
  virtualKey: number
  modifiers: number
  conflicted: boolean
  unbound: boolean
  overridden: boolean
}

export interface HotkeyRows {
  rows: HotkeyRow[]
}

export interface PluginSetting {
  filename: string
  enabled: boolean
}

export interface Settings {
  console: boolean
  trace: boolean
  plugins: PluginSetting[]
}

export interface ShellState {
  selectedPanel: string
  managerPanelId: string
  mode: string
  webviewReady: boolean
  saveFailed: boolean
}

export interface CaptureState {
  capturing: boolean
  pluginId: string
  actionId: string
}

export interface BridgeMethods {
  'bridge.hello': { params: void; result: { protocol: number; sessionId: number } }
  'bridge.ping': { params: void; result: { pong: boolean } }
  'host.snapshot': { params: void; result: HostSnapshot }
  'hotkeys.rows': { params: void; result: HotkeyRows }
  'settings.get': { params: void; result: Settings }
  'appearance.get': { params: void; result: Appearance }
  'language.get': { params: void; result: Language }
  'strings.get': { params: void; result: Strings }
  'config.path': { params: void; result: { path: string } }
  'shell.state': { params: void; result: ShellState }
  'shell.hide': { params: void; result: Record<string, never> }
  'shell.selectPanel': { params: { id: string }; result: { selectedPanel: string } }
  'shell.setTextInputActive': { params: { active: boolean }; result: Record<string, never> }
  'appearance.setTheme': { params: { theme: number }; result: { theme: number } }
  'appearance.setScale': { params: { percent: number }; result: { percent: number } }
  'language.set': { params: { language: number }; result: Language }
  'hotkeys.setOverride': {
    params: { pluginId: string; actionId: string; combo?: string; virtualKey?: number; modifiers?: number }
    result: { virtualKey: number; modifiers: number }
  }
  'hotkeys.unbind': { params: { pluginId: string; actionId: string }; result: Record<string, never> }
  'hotkeys.resetOverride': { params: { pluginId: string; actionId: string }; result: Record<string, never> }
  'hotkeys.setEditorActive': { params: { active: boolean }; result: Record<string, never> }
  'hotkeys.beginCapture': {
    params: { pluginId: string; actionId: string; combo?: string }
    result: { capturing: boolean }
  }
  'hotkeys.endCapture': { params: void; result: Record<string, never> }
  'settings.setConsole': { params: { enabled: boolean }; result: { console: boolean } }
  'settings.setTrace': { params: { enabled: boolean }; result: { trace: boolean } }
  'settings.setPluginEnabled': {
    params: { filename: string; enabled: boolean }
    result: { filename: string; enabled: boolean }
  }
}

export interface BridgeEvents {
  'shell.ready': { mode: string; managerPanelId: string }
  'shell.panelSelected': { id: string }
  'host.snapshotChanged': HostSnapshot
  'appearance.changed': Appearance
  'language.changed': Language
  'strings.changed': Strings
  'hotkeys.rowsChanged': HotkeyRows
  'hotkeys.captureStateChanged': CaptureState
  'hotkeys.captured': { combo: string; invalid: boolean }
  'hotkeys.captureCancelled': Record<string, never>
  'settings.changed': Settings
  'settings.saveResult': { ok: boolean; key: string }
  'shell.saveFailed': { failed: boolean }
  'bridge.closed': { reason: string }
}

export type MethodName = keyof BridgeMethods
export type EventName = keyof BridgeEvents

export const PROTOCOL_VERSION = 1

export class BridgeError extends Error {
  readonly code: string
  readonly details?: unknown
  constructor(code: string, message: string, details?: unknown) {
    super(message)
    this.name = 'BridgeError'
    this.code = code
    this.details = details
  }
}

export interface RequestOptions {
  signal?: AbortSignal
  timeoutMs?: number
}

export interface BridgeClient {
  readonly kind: 'webview2' | 'mock'
  request<M extends MethodName>(
    method: M,
    params?: BridgeMethods[M]['params'],
    options?: RequestOptions,
  ): Promise<BridgeMethods[M]['result']>
  on<E extends EventName>(event: E, handler: (payload: BridgeEvents[E]) => void): () => void
  close(): void
}
