import { afterEach, describe, expect, it, vi } from 'vitest'
import { MockBridgeClient } from '@/bridge/mock'
import { BridgeError } from '@/bridge/protocol'
import { ManagerStore } from '@/state/store'

async function startedStore() {
  const bridge = new MockBridgeClient()
  const store = new ManagerStore(bridge)
  await store.start()
  return { bridge, store }
}

afterEach(() => {
  vi.restoreAllMocks()
})

describe('ManagerStore', () => {
  it('populates state from the bridge on start', async () => {
    const { store } = await startedStore()
    const state = store.getState()
    expect(state.ready).toBe(true)
    expect(state.connected).toBe(true)
    expect(state.snapshot?.plugins.length).toBe(3)
    expect(state.hotkeys.length).toBe(5)
    expect(state.settings?.plugins.length).toBe(4)
    expect(state.strings.Manager).toBe('Manager')
    expect(state.selectedPanel).toBe('gakuloader.manager')
  })

  it('applies a theme change optimistically and via event', async () => {
    const { store } = await startedStore()
    await store.setTheme(2)
    expect(store.getState().appearance?.theme).toBe(2)
  })

  it('switches the selected panel', async () => {
    const { store } = await startedStore()
    await store.selectPanel('gakumotion')
    expect(store.getState().selectedPanel).toBe('gakumotion')
    await store.selectPanel('gakuloader.manager')
    expect(store.getState().selectedPanel).toBe('gakuloader.manager')
  })

  it('toggles a plugin enable flag', async () => {
    const { store } = await startedStore()
    await store.setPluginEnabled('LegacyTool.dll', true)
    const plugin = store.getState().settings?.plugins.find((p) => p.filename === 'LegacyTool.dll')
    expect(plugin?.enabled).toBe(true)
  })

  it('updates hotkey rows after an override', async () => {
    const { store } = await startedStore()
    await store.setHotkeyOverride('gakumotion', 'play-pause', 'Ctrl+P')
    const row = store.getState().hotkeys.find((r) => r.actionId === 'play-pause')
    expect(row?.combo).toBe('Ctrl+P')
    await store.unbindHotkey('gakumotion', 'play-pause')
    const unbound = store.getState().hotkeys.find((r) => r.actionId === 'play-pause')
    expect(unbound?.unbound).toBe(true)
  })

  it('reflects native push events', async () => {
    const { bridge, store } = await startedStore()
    bridge.push('shell.saveFailed', { failed: true })
    expect(store.getState().saveFailed).toBe(true)
    bridge.push('shell.saveFailed', { failed: false })
    expect(store.getState().saveFailed).toBe(false)
  })

  it('rolls back an optimistic console toggle when the native write fails', async () => {
    const { bridge, store } = await startedStore()
    vi.spyOn(bridge, 'request').mockRejectedValueOnce(new BridgeError('save_failed', 'Could not save'))
    await store.setConsole(true)
    expect(store.getState().settings?.console).toBe(false)
  })

  it('rolls back an optimistic theme change when the native apply fails', async () => {
    const { bridge, store } = await startedStore()
    vi.spyOn(bridge, 'request').mockRejectedValueOnce(new BridgeError('apply_failed', 'Could not apply'))
    await store.setTheme(4)
    expect(store.getState().appearance?.theme).toBe(0)
  })

  it('rolls back panel selection when the native selection fails', async () => {
    const { bridge, store } = await startedStore()
    vi.spyOn(bridge, 'request').mockRejectedValueOnce(new BridgeError('unknown_panel', 'Unknown panel'))
    await store.selectPanel('not-registered')
    expect(store.getState().selectedPanel).toBe('gakuloader.manager')
  })
})
