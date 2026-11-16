import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { afterEach, describe, expect, it, vi } from 'vitest'
import App from '@/prototype/LoaderPrototypeApp'
import { setBridge } from '@/bridge'
import { MockBridgeClient } from '@/bridge/mock'

afterEach(() => {
  vi.restoreAllMocks()
})

describe('App (mock bridge)', () => {
  it('renders the manager shell with all four tabs and seeded plugins', async () => {
    setBridge(new MockBridgeClient())
    render(<App />)
    await waitFor(() => expect(screen.getByText('Plugin List')).toBeInTheDocument())
    expect(screen.getByText('Appearance')).toBeInTheDocument()
    expect(screen.getByText('Hotkeys')).toBeInTheDocument()
    expect(screen.getByText('Settings')).toBeInTheDocument()
    // Seeded plugin appears in navigation and the plugin list table.
    expect(screen.getAllByText('GakuMotion').length).toBeGreaterThan(0)
    expect(screen.getAllByText('GakuLocalify').length).toBeGreaterThan(0)
  })

  it('switches to the Appearance tab and shows theme/scale/language controls', async () => {
    setBridge(new MockBridgeClient())
    const user = userEvent.setup()
    render(<App />)
    await waitFor(() => expect(screen.getByText('Appearance')).toBeInTheDocument())
    await user.click(screen.getByText('Appearance'))
    await waitFor(() => expect(screen.getByText('Theme')).toBeInTheDocument())
    expect(screen.getByText('UI Scale')).toBeInTheDocument()
    expect(screen.getByText('Languages')).toBeInTheDocument()
  })

  it('opens the hotkey rebind dialog from the Hotkeys tab', async () => {
    setBridge(new MockBridgeClient())
    const user = userEvent.setup()
    render(<App />)
    await waitFor(() => expect(screen.getByText('Hotkeys')).toBeInTheDocument())
    await user.click(screen.getByText('Hotkeys'))
    await waitFor(() => expect(screen.getAllByText('Edit').length).toBeGreaterThan(0))
    await user.click(screen.getAllByText('Edit')[0])
    await waitFor(() => expect(screen.getByText('Rebind hotkey')).toBeInTheDocument())
    expect(screen.getByText('Capture keys')).toBeInTheDocument()
  })

  it('latches native hotkey suppression while the rebind dialog is open', async () => {
    const bridge = new MockBridgeClient()
    setBridge(bridge)
    const spy = vi.spyOn(bridge, 'request')
    const user = userEvent.setup()
    render(<App />)
    await waitFor(() => expect(screen.getByText('Hotkeys')).toBeInTheDocument())
    await user.click(screen.getByText('Hotkeys'))
    await waitFor(() => expect(screen.getAllByText('Edit').length).toBeGreaterThan(0))
    await user.click(screen.getAllByText('Edit')[0])
    await waitFor(() => expect(screen.getByText('Rebind hotkey')).toBeInTheDocument())
    expect(spy).toHaveBeenCalledWith('hotkeys.setEditorActive', { active: true })
    await user.click(screen.getByRole('button', { name: 'Cancel' }))
    await waitFor(() => expect(spy).toHaveBeenCalledWith('hotkeys.setEditorActive', { active: false }))
  })
})
