import {
  BridgeError,
  type Appearance,
  type BridgeClient,
  type BridgeEvents,
  type BridgeMethods,
  type EventName,
  type HostSnapshot,
  type HotkeyRows,
  type Language,
  type MethodName,
  type RequestOptions,
  type Settings,
  type ShellState,
  type Strings,
} from './protocol'

const ENGLISH: Record<string, string> = {
  Manager: 'Manager', PluginList: 'Plugin List', Appearance: 'Appearance', Hotkeys: 'Hotkeys',
  Settings: 'Settings', Plugin: 'Plugin', Version: 'Version', State: 'State', Panel: 'Panel',
  Ready: 'runtime ready', Activated: 'activated', Inactive: 'not activated', Failed: 'failed',
  Yes: 'yes', No: 'no', Waiting: 'waiting for IL2CPP', Running: 'running', Stopping: 'shutting down',
  Theme: 'Theme', Scale: 'UI Scale', Languages: 'Languages', Dark: 'Dark', Sapphire: 'Sapphire',
  Paper: 'Paper & Ink',
  LanguageHelp: 'Applies immediately to the loader and plugins supporting the language service.',
  NoHotkeys: 'No plugin hotkeys registered.', Action: 'Action', Name: 'Name', Combo: 'Combo',
  Active: 'active', Conflict: 'conflict', Unbound: 'unbound', Edit: 'Edit', Reset: 'Reset',
  Unbind: 'Unbind', Cancel: 'Cancel', Apply: 'Apply', Capture: 'Rebind hotkey',
  CaptureHelp: 'Type a combo (Ctrl+M), or capture keys. F1 is reserved. Esc cancels capture.',
  CaptureButton: 'Capture keys', InvalidCombo: 'Invalid combo or reserved F1.',
  SaveFailed: 'Could not save the configuration.',
  HotkeyHelp: 'Changes apply immediately and are saved. Conflicting bindings stop dispatching until reassigned. F1 opens or hides the manager.',
  Console: 'Show console', Trace: 'Diagnostic trace', NextStart: 'Takes effect on the next game start.',
  PluginEnable: 'Enabled plugins', PluginEnableHelp: 'Enable or disable discovered DLLs for the next game start.',
  File: 'File', Hide: 'Hide controls', View: 'View', Plugins: 'Plugins',
  SelectPanel: 'Select the manager or a plugin from the navigation.',
  FailedHelp: 'This plugin failed. Consult the loader log.', NoPanel: 'This plugin has no desktop control panel.',
  Count: 'Plugins', Runtime: 'Runtime', SummaryHelp: 'Registered plugins and their current lifecycle state.',
  ConfigFile: 'Config file', Saved: 'Saved', DefaultBinding: 'default', CustomBinding: 'custom',
  ScaleLimit: 'This UI scale and DPI combination exceeds the font atlas limit. The previous scale is retained.',
  AppleLight: 'Apple Light', AppleDark: 'Apple Dark',
}

const LANGUAGE_NAMES = ['English', '简体中文', '繁體中文', '日本語']
const LANGUAGE_TAGS = ['en', 'zh-Hans', 'zh-Hant', 'ja']

// A believable seeded host so `npm run dev` and component tests are populated and
// fully interactive without the game. Mutations emit the same events as native.
export class MockBridgeClient implements BridgeClient {
  readonly kind = 'mock' as const
  private readonly handlers = new Map<EventName, Set<(payload: never) => void>>()
  private closed = false
  private sessionId = 1
  private captureTimer: ReturnType<typeof setTimeout> | null = null

  private snapshot: HostSnapshot = {
    available: true,
    runtimeReady: true,
    shuttingDown: false,
    activated: true,
    pluginFiles: ['GakuMotion.dll', 'GakuLocalify.dll', 'SamplePlugin.dll', 'LegacyTool.dll'],
    plugins: [
      { id: 'gakumotion', name: 'GakuMotion', version: 'v3.6.2', activated: true, runtimeReady: true, failed: false, hasPanel: true },
      { id: 'gakulocalify', name: 'GakuLocalify', version: 'v1.4.0', activated: true, runtimeReady: true, failed: false, hasPanel: true },
      { id: 'sample', name: 'Sample Plugin', version: 'v0.2.1', activated: true, runtimeReady: false, failed: false, hasPanel: false },
    ],
  }
  private appearance: Appearance = { theme: 0, uiScale: 1, scaleLimited: false, dpiScale: 1 }
  private languageIndex = 0
  private settings: Settings = {
    console: false,
    trace: false,
    plugins: [
      { filename: 'GakuMotion.dll', enabled: true },
      { filename: 'GakuLocalify.dll', enabled: true },
      { filename: 'SamplePlugin.dll', enabled: true },
      { filename: 'LegacyTool.dll', enabled: false },
    ],
  }
  private hotkeys: HotkeyRows = {
    rows: [
      { pluginId: 'gakumotion', actionId: 'show-gui', displayName: 'Show motion GUI', combo: 'Ctrl+Shift+M', virtualKey: 77, modifiers: 3, conflicted: false, unbound: false, overridden: true },
      { pluginId: 'gakumotion', actionId: 'play-pause', displayName: 'Play / pause', combo: 'Space', virtualKey: 32, modifiers: 0, conflicted: false, unbound: false, overridden: false },
      { pluginId: 'gakumotion', actionId: 'next-frame', displayName: 'Next frame', combo: '', virtualKey: 0, modifiers: 0, conflicted: false, unbound: true, overridden: false },
      { pluginId: 'gakulocalify', actionId: 'toggle-overlay', displayName: 'Toggle overlay', combo: 'Ctrl+L', virtualKey: 76, modifiers: 1, conflicted: false, unbound: false, overridden: false },
      { pluginId: 'sample', actionId: 'reload', displayName: 'Reload config', combo: 'Ctrl+R', virtualKey: 82, modifiers: 1, conflicted: true, unbound: false, overridden: false },
    ],
  }
  private shell: ShellState = {
    selectedPanel: 'gakuloader.manager',
    managerPanelId: 'gakuloader.manager',
    mode: 'webview2',
    webviewReady: true,
    saveFailed: false,
  }

  private language(): Language {
    return {
      language: this.languageIndex,
      tag: LANGUAGE_TAGS[this.languageIndex],
      name: LANGUAGE_NAMES[this.languageIndex],
      names: [...LANGUAGE_NAMES],
    }
  }
  private strings(): Strings {
    return { language: this.languageIndex, strings: { ...ENGLISH } }
  }

  private emit<E extends EventName>(event: E, payload: BridgeEvents[E]): void {
    const set = this.handlers.get(event)
    if (set) for (const handler of set) (handler as (p: unknown) => void)(payload)
  }

  // Test/dev helper to push a native-style event.
  push<E extends EventName>(event: E, payload: BridgeEvents[E]): void {
    this.emit(event, payload)
  }

  async request<M extends MethodName>(
    method: M,
    params?: BridgeMethods[M]['params'],
    _options?: RequestOptions,
  ): Promise<BridgeMethods[M]['result']> {
    if (this.closed) throw new BridgeError('closed', 'The bridge is closed')
    await new Promise((resolve) => setTimeout(resolve, 8))
    const p = (params ?? {}) as Record<string, unknown>
    switch (method) {
      case 'bridge.hello':
        return { protocol: 1, sessionId: this.sessionId } as BridgeMethods[M]['result']
      case 'bridge.ping':
        return { pong: true } as BridgeMethods[M]['result']
      case 'host.snapshot':
        return { ...this.snapshot } as BridgeMethods[M]['result']
      case 'hotkeys.rows':
        return { rows: this.hotkeys.rows.map((r) => ({ ...r })) } as BridgeMethods[M]['result']
      case 'settings.get':
        return { ...this.settings, plugins: this.settings.plugins.map((x) => ({ ...x })) } as BridgeMethods[M]['result']
      case 'appearance.get':
        return { ...this.appearance } as BridgeMethods[M]['result']
      case 'language.get':
        return this.language() as BridgeMethods[M]['result']
      case 'strings.get':
        return this.strings() as BridgeMethods[M]['result']
      case 'config.path':
        return { path: 'GakuLoader/Configs/gakuloader.ini' } as BridgeMethods[M]['result']
      case 'shell.state':
        return { ...this.shell } as BridgeMethods[M]['result']
      case 'shell.hide':
        return {} as BridgeMethods[M]['result']
      case 'shell.selectPanel': {
        this.shell = { ...this.shell, selectedPanel: String(p.id) }
        this.emit('shell.panelSelected', { id: String(p.id) })
        return { selectedPanel: String(p.id) } as BridgeMethods[M]['result']
      }
      case 'shell.setTextInputActive':
        return {} as BridgeMethods[M]['result']
      case 'appearance.setTheme': {
        this.appearance = { ...this.appearance, theme: Number(p.theme) }
        this.emit('appearance.changed', { ...this.appearance })
        return { theme: Number(p.theme) } as BridgeMethods[M]['result']
      }
      case 'appearance.setScale': {
        this.appearance = { ...this.appearance, uiScale: Number(p.percent) / 100 }
        this.emit('appearance.changed', { ...this.appearance })
        return { percent: Number(p.percent) } as BridgeMethods[M]['result']
      }
      case 'language.set': {
        this.languageIndex = Number(p.language)
        const lang = this.language()
        this.emit('language.changed', lang)
        this.emit('strings.changed', this.strings())
        return lang as BridgeMethods[M]['result']
      }
      case 'hotkeys.setOverride': {
        const combo = String(p.combo ?? '')
        this.hotkeys = {
          rows: this.hotkeys.rows.map((r) =>
            r.pluginId === p.pluginId && r.actionId === p.actionId
              ? { ...r, combo, unbound: combo === '', overridden: true, conflicted: false }
              : r,
          ),
        }
        this.emit('hotkeys.rowsChanged', { rows: this.hotkeys.rows.map((r) => ({ ...r })) })
        return { virtualKey: Number(p.virtualKey ?? 0), modifiers: Number(p.modifiers ?? 0) } as BridgeMethods[M]['result']
      }
      case 'hotkeys.unbind': {
        this.hotkeys = {
          rows: this.hotkeys.rows.map((r) =>
            r.pluginId === p.pluginId && r.actionId === p.actionId ? { ...r, combo: '', unbound: true } : r,
          ),
        }
        this.emit('hotkeys.rowsChanged', { rows: this.hotkeys.rows.map((r) => ({ ...r })) })
        return {} as BridgeMethods[M]['result']
      }
      case 'hotkeys.resetOverride': {
        this.hotkeys = {
          rows: this.hotkeys.rows.map((r) =>
            r.pluginId === p.pluginId && r.actionId === p.actionId ? { ...r, overridden: false } : r,
          ),
        }
        this.emit('hotkeys.rowsChanged', { rows: this.hotkeys.rows.map((r) => ({ ...r })) })
        return {} as BridgeMethods[M]['result']
      }
      case 'hotkeys.setEditorActive':
        return {} as BridgeMethods[M]['result']
      case 'hotkeys.beginCapture': {
        this.emit('hotkeys.captureStateChanged', { capturing: true, pluginId: String(p.pluginId), actionId: String(p.actionId) })
        this.captureTimer = setTimeout(() => {
          this.emit('hotkeys.captureStateChanged', { capturing: false, pluginId: String(p.pluginId), actionId: String(p.actionId) })
          this.emit('hotkeys.captured', { combo: 'Ctrl+K', invalid: false })
        }, 1200)
        return { capturing: true } as BridgeMethods[M]['result']
      }
      case 'hotkeys.endCapture': {
        if (this.captureTimer) clearTimeout(this.captureTimer)
        this.emit('hotkeys.captureStateChanged', { capturing: false, pluginId: '', actionId: '' })
        this.emit('hotkeys.captureCancelled', {})
        return {} as BridgeMethods[M]['result']
      }
      case 'settings.setConsole': {
        this.settings = { ...this.settings, console: Boolean(p.enabled) }
        this.emit('settings.changed', { ...this.settings })
        this.emit('settings.saveResult', { ok: true, key: 'console' })
        return { console: Boolean(p.enabled) } as BridgeMethods[M]['result']
      }
      case 'settings.setTrace': {
        this.settings = { ...this.settings, trace: Boolean(p.enabled) }
        this.emit('settings.changed', { ...this.settings })
        this.emit('settings.saveResult', { ok: true, key: 'trace' })
        return { trace: Boolean(p.enabled) } as BridgeMethods[M]['result']
      }
      case 'settings.setPluginEnabled': {
        this.settings = {
          ...this.settings,
          plugins: this.settings.plugins.map((x) => (x.filename === p.filename ? { ...x, enabled: Boolean(p.enabled) } : x)),
        }
        this.emit('settings.changed', { ...this.settings })
        this.emit('settings.saveResult', { ok: true, key: String(p.filename) })
        return { filename: String(p.filename), enabled: Boolean(p.enabled) } as BridgeMethods[M]['result']
      }
      default:
        throw new BridgeError('unknown_method', `Unknown bridge method: ${method}`)
    }
  }

  on<E extends EventName>(event: E, handler: (payload: BridgeEvents[E]) => void): () => void {
    let set = this.handlers.get(event)
    if (!set) {
      set = new Set()
      this.handlers.set(event, set)
    }
    set.add(handler as (payload: never) => void)
    return () => {
      set?.delete(handler as (payload: never) => void)
    }
  }

  close(): void {
    this.closed = true
    if (this.captureTimer) clearTimeout(this.captureTimer)
  }
}
