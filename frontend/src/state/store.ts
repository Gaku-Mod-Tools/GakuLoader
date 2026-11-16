import { useSyncExternalStore } from 'react'
import { getBridge } from '@/bridge'
import type {
  Appearance,
  BridgeClient,
  HostSnapshot,
  HotkeyRow,
  Language,
  Settings,
  ShellState,
} from '@/bridge/protocol'

export type ManagerTab = 'plugin-list' | 'appearance' | 'hotkeys' | 'settings'

export interface AppState {
  ready: boolean
  connected: boolean
  bridgeKind: 'webview2' | 'mock'
  snapshot: HostSnapshot | null
  appearance: Appearance | null
  language: Language | null
  strings: Record<string, string>
  hotkeys: HotkeyRow[]
  settings: Settings | null
  shell: ShellState | null
  selectedPanel: string
  activeTab: ManagerTab
  saveFailed: boolean
  scaleLimited: boolean
  error: string | null
}

const initialState: AppState = {
  ready: false,
  connected: false,
  bridgeKind: 'mock',
  snapshot: null,
  appearance: null,
  language: null,
  strings: {},
  hotkeys: [],
  settings: null,
  shell: null,
  selectedPanel: 'gakuloader.manager',
  activeTab: 'plugin-list',
  saveFailed: false,
  scaleLimited: false,
  error: null,
}

type Listener = () => void

export class ManagerStore {
  private state: AppState = initialState
  private readonly listeners = new Set<Listener>()
  private readonly bridge: BridgeClient
  private started = false

  constructor(bridge: BridgeClient = getBridge()) {
    this.bridge = bridge
  }

  getState = (): AppState => this.state

  subscribe = (listener: Listener): (() => void) => {
    this.listeners.add(listener)
    return () => this.listeners.delete(listener)
  }

  private set(partial: Partial<AppState>): void {
    this.state = { ...this.state, ...partial }
    for (const listener of this.listeners) listener()
  }

  t = (key: string): string => this.state.strings[key] ?? key

  async start(): Promise<void> {
    if (this.started) return
    this.started = true
    const b = this.bridge
    b.on('host.snapshotChanged', (p) => this.set({ snapshot: p }))
    b.on('appearance.changed', (p) => this.set({ appearance: p, scaleLimited: p.scaleLimited }))
    b.on('language.changed', (p) => this.set({ language: p }))
    b.on('strings.changed', (p) => this.set({ strings: p.strings }))
    b.on('hotkeys.rowsChanged', (p) => this.set({ hotkeys: p.rows }))
    b.on('settings.changed', (p) => this.set({ settings: p }))
    b.on('shell.panelSelected', (p) => this.set({ selectedPanel: p.id }))
    b.on('shell.saveFailed', (p) => this.set({ saveFailed: p.failed }))
    b.on('bridge.closed', () => this.set({ connected: false, error: 'The native bridge closed' }))
    try {
      await b.request('bridge.hello')
      const [snapshot, appearance, language, strings, hotkeys, settings, shell] = await Promise.all([
        b.request('host.snapshot'),
        b.request('appearance.get'),
        b.request('language.get'),
        b.request('strings.get'),
        b.request('hotkeys.rows'),
        b.request('settings.get'),
        b.request('shell.state'),
      ])
      this.set({
        ready: true,
        connected: true,
        bridgeKind: b.kind,
        snapshot,
        appearance,
        scaleLimited: appearance.scaleLimited,
        language,
        strings: strings.strings,
        hotkeys: hotkeys.rows,
        settings,
        shell,
        selectedPanel: shell.selectedPanel,
        saveFailed: shell.saveFailed,
        error: null,
      })
    } catch (error) {
      this.set({ ready: true, connected: false, error: error instanceof Error ? error.message : String(error) })
    }
  }

  setActiveTab = (tab: ManagerTab): void => this.set({ activeTab: tab })

  selectPanel = async (id: string): Promise<void> => {
    const previous = this.state.selectedPanel
    this.set({ selectedPanel: id })
    try {
      await this.bridge.request('shell.selectPanel', { id })
    } catch {
      this.set({ selectedPanel: previous })
      try {
        const shell = await this.bridge.request('shell.state')
        this.set({ shell, selectedPanel: shell.selectedPanel, saveFailed: shell.saveFailed })
      } catch {
      }
    }
  }

  hideWindow = async (): Promise<void> => {
    await this.bridge.request('shell.hide')
  }

  setTheme = async (theme: number): Promise<void> => {
    const previous = this.state.appearance
    if (previous) this.set({ appearance: { ...previous, theme } })
    try {
      await this.bridge.request('appearance.setTheme', { theme })
    } catch {
      if (previous) this.set({ appearance: previous, scaleLimited: previous.scaleLimited })
      try {
        const appearance = await this.bridge.request('appearance.get')
        this.set({ appearance, scaleLimited: appearance.scaleLimited })
      } catch {
      }
    }
  }

  setScale = async (percent: number): Promise<void> => {
    const previous = this.state.appearance
    if (previous) this.set({ appearance: { ...previous, uiScale: percent / 100 } })
    try {
      await this.bridge.request('appearance.setScale', { percent })
    } catch {
      if (previous) this.set({ appearance: previous, scaleLimited: previous.scaleLimited })
      try {
        const appearance = await this.bridge.request('appearance.get')
        this.set({ appearance, scaleLimited: appearance.scaleLimited })
      } catch {
      }
    }
  }

  setLanguage = async (language: number): Promise<void> => {
    try {
      await this.bridge.request('language.set', { language })
    } catch {
      try {
        const [nextLanguage, strings] = await Promise.all([
          this.bridge.request('language.get'),
          this.bridge.request('strings.get'),
        ])
        this.set({ language: nextLanguage, strings: strings.strings })
      } catch {
      }
    }
  }

  setConsole = async (enabled: boolean): Promise<void> => {
    const previous = this.state.settings
    if (previous) this.set({ settings: { ...previous, console: enabled } })
    try {
      await this.bridge.request('settings.setConsole', { enabled })
    } catch {
      if (previous) this.set({ settings: previous })
      try {
        this.set({ settings: await this.bridge.request('settings.get') })
      } catch {
      }
    }
  }

  setTrace = async (enabled: boolean): Promise<void> => {
    const previous = this.state.settings
    if (previous) this.set({ settings: { ...previous, trace: enabled } })
    try {
      await this.bridge.request('settings.setTrace', { enabled })
    } catch {
      if (previous) this.set({ settings: previous })
      try {
        this.set({ settings: await this.bridge.request('settings.get') })
      } catch {
      }
    }
  }

  setPluginEnabled = async (filename: string, enabled: boolean): Promise<void> => {
    const previous = this.state.settings
    if (previous) {
      this.set({
        settings: {
          ...previous,
          plugins: previous.plugins.map((p) => (p.filename === filename ? { ...p, enabled } : p)),
        },
      })
    }
    try {
      await this.bridge.request('settings.setPluginEnabled', { filename, enabled })
    } catch {
      if (previous) this.set({ settings: previous })
      try {
        this.set({ settings: await this.bridge.request('settings.get') })
      } catch {
      }
    }
  }

  setHotkeyOverride = async (pluginId: string, actionId: string, combo: string): Promise<void> => {
    await this.bridge.request('hotkeys.setOverride', { pluginId, actionId, combo })
  }

  unbindHotkey = async (pluginId: string, actionId: string): Promise<void> => {
    try {
      await this.bridge.request('hotkeys.unbind', { pluginId, actionId })
    } catch {
      try {
        this.set({ hotkeys: (await this.bridge.request('hotkeys.rows')).rows })
      } catch {
      }
    }
  }

  resetHotkey = async (pluginId: string, actionId: string): Promise<void> => {
    try {
      await this.bridge.request('hotkeys.resetOverride', { pluginId, actionId })
    } catch {
      try {
        this.set({ hotkeys: (await this.bridge.request('hotkeys.rows')).rows })
      } catch {
      }
    }
  }
}

export const store = new ManagerStore()

export function useStore<T>(selector: (state: AppState) => T): T {
  return useSyncExternalStore(store.subscribe, () => selector(store.getState()))
}

export function useT(): (key: string) => string {
  const strings = useStore((s) => s.strings)
  return (key: string) => strings[key] ?? key
}
