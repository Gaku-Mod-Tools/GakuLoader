import * as React from 'react'
import { Keyboard } from 'lucide-react'
import { getBridge } from '@/bridge'
import type { HotkeyRow } from '@/bridge/protocol'
import { Button } from '@/components/ui/button'
import {
  Dialog, DialogContent, DialogDescription, DialogFooter, DialogHeader, DialogTitle,
} from '@/components/ui/dialog'
import { Input } from '@/components/ui/input'
import { Spinner } from '@/components/ui/spinner'
import { store, useT } from '@/state/store'

interface HotkeyEditorProps {
  row: HotkeyRow | null
  open: boolean
  onOpenChange: (open: boolean) => void
}

// Rebind dialog mirroring the ImGui modal: type a combo or capture physical keys
// (native GetAsyncKeyState scan; F1 reserved; Esc cancels capture). Capture stays
// native so naming/validation and the capture-exit latch are identical.
export function HotkeyEditor({ row, open, onOpenChange }: HotkeyEditorProps) {
  const t = useT()
  const [combo, setCombo] = React.useState('')
  const [invalid, setInvalid] = React.useState(false)
  const [capturing, setCapturing] = React.useState(false)

  React.useEffect(() => {
    if (open && row) {
      setCombo(row.unbound ? '' : row.combo)
      setInvalid(false)
      setCapturing(false)
    }
  }, [open, row])

  React.useEffect(() => {
    if (!open) return
    const bridge = getBridge()
    void bridge.request('hotkeys.setEditorActive', { active: true }).catch(() => {})
    const offCaptured = bridge.on('hotkeys.captured', (payload) => {
      setCombo(payload.combo)
      setInvalid(payload.invalid)
      setCapturing(false)
    })
    const offCancelled = bridge.on('hotkeys.captureCancelled', () => setCapturing(false))
    const offState = bridge.on('hotkeys.captureStateChanged', (payload) => setCapturing(payload.capturing))
    return () => {
      offCaptured()
      offCancelled()
      offState()
      void bridge.request('hotkeys.setEditorActive', { active: false }).catch(() => {})
    }
  }, [open])

  const beginCapture = () => {
    if (!row) return
    setInvalid(false)
    setCapturing(true)
    void getBridge().request('hotkeys.beginCapture', {
      pluginId: row.pluginId,
      actionId: row.actionId,
      combo,
    }).catch(() => {
      setCapturing(false)
      setInvalid(true)
    })
  }

  const cancel = () => {
    if (capturing) void getBridge().request('hotkeys.endCapture')
    setCapturing(false)
    onOpenChange(false)
  }

  const apply = () => {
    if (!row) return
    void store
      .setHotkeyOverride(row.pluginId, row.actionId, combo.trim())
      .then(() => onOpenChange(false))
      .catch(() => setInvalid(true))
  }

  return (
    <Dialog open={open} onOpenChange={(next) => (next ? onOpenChange(true) : cancel())}>
      <DialogContent
        className="max-w-md"
        onEscapeKeyDown={(event) => {
          // While capturing, Esc cancels the capture (handled natively) rather
          // than closing the dialog, matching the ImGui editor.
          if (capturing) event.preventDefault()
        }}
        onPointerDownOutside={(event) => {
          if (capturing) event.preventDefault()
        }}
      >
        <DialogHeader>
          <DialogTitle className="flex items-center gap-2">
            <Keyboard className="size-5 text-primary" />
            {t('Capture')}
          </DialogTitle>
          <DialogDescription className="font-mono text-xs">
            {row?.pluginId}.{row?.actionId}
          </DialogDescription>
        </DialogHeader>

        <p className="text-sm text-muted-foreground">{t('CaptureHelp')}</p>

        <Input
          value={combo}
          onChange={(event) => {
            setCombo(event.target.value)
            setInvalid(false)
          }}
          placeholder="Ctrl+M"
          spellCheck={false}
          maxLength={127}
          aria-label={t('Combo')}
          className="font-mono"
        />

        <div className="flex items-center gap-2">
          <Button variant="outline" size="sm" onClick={beginCapture} disabled={capturing}>
            {t('CaptureButton')}
          </Button>
          {capturing && <Spinner />}
        </div>

        {invalid && <p className="text-sm text-destructive">{t('InvalidCombo')}</p>}

        <DialogFooter>
          <Button variant="ghost" onClick={cancel}>
            {t('Cancel')}
          </Button>
          <Button onClick={apply}>{t('Apply')}</Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  )
}
