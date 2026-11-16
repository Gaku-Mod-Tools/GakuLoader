import * as React from 'react'
import { getBridge } from '@/bridge'
import type { Appearance } from '@/bridge/protocol'

export const THEME_NAMES = ['dark', 'sapphire', 'paper', 'apple-light', 'apple-dark'] as const
const DARK_THEMES = new Set([0, 1, 4])

// Applies the authoritative native theme + UI scale to the document. DPI scaling
// is handled by WebView2 itself; uiScale is the user multiplier (CSS zoom), so the
// effective scale matches ImGui's uiScale * dpiScale.
export function useAppearanceEffect(appearance: Appearance | null): void {
  React.useEffect(() => {
    const theme = appearance?.theme ?? 0
    const root = document.documentElement
    root.dataset.theme = THEME_NAMES[theme] ?? 'dark'
    root.classList.toggle('dark', DARK_THEMES.has(theme))
    root.style.zoom = String(appearance?.uiScale ?? 1)
  }, [appearance])
}

function isEditable(target: EventTarget | null): boolean {
  if (!(target instanceof HTMLElement)) return false
  if (target.tagName === 'TEXTAREA') return true
  if (target.tagName === 'INPUT') {
    const type = (target as HTMLInputElement).type
    return !['button', 'checkbox', 'radio', 'range', 'submit', 'reset', 'file', 'color'].includes(type)
  }
  return target.isContentEditable
}

// Reports editable-focus to the native hotkey poller (WantsTextInput) so typing in
// a React field suppresses plugin hotkeys and F1, exactly like ImGui text input.
export function useTextInputBridge(): void {
  React.useEffect(() => {
    const bridge = getBridge()
    let active = false
    const report = (focused: boolean) => {
      if (focused === active) return
      active = focused
      void bridge.request('shell.setTextInputActive', { active: focused }).catch(() => {})
    }
    const onFocusIn = (event: FocusEvent) => report(isEditable(event.target))
    const onFocusOut = (event: FocusEvent) => report(isEditable(event.relatedTarget))
    document.addEventListener('focusin', onFocusIn)
    document.addEventListener('focusout', onFocusOut)
    return () => {
      document.removeEventListener('focusin', onFocusIn)
      document.removeEventListener('focusout', onFocusOut)
      report(false)
    }
  }, [])
}
