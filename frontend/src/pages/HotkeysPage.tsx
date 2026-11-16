import * as React from 'react'
import { HotkeyEditor } from '@/components/HotkeyEditor'
import { Badge } from '@/components/ui/badge'
import { Button } from '@/components/ui/button'
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from '@/components/ui/table'
import { Tooltip, TooltipContent, TooltipTrigger } from '@/components/ui/tooltip'
import type { HotkeyRow } from '@/bridge/protocol'
import { store, useStore, useT } from '@/state/store'

export function HotkeysPage() {
  const t = useT()
  const hotkeys = useStore((s) => s.hotkeys)
  const [editing, setEditing] = React.useState<HotkeyRow | null>(null)
  const [open, setOpen] = React.useState(false)

  const openEditor = (row: HotkeyRow) => {
    setEditing(row)
    setOpen(true)
  }

  return (
    <div className="space-y-3">
      <p className="text-sm text-muted-foreground">{t('HotkeyHelp')}</p>

      {hotkeys.length === 0 ? (
        <p className="text-sm text-muted-foreground">{t('NoHotkeys')}</p>
      ) : (
        <Table>
          <TableHeader>
            <TableRow>
              <TableHead>{t('Plugin')}</TableHead>
              <TableHead>{t('Name')}</TableHead>
              <TableHead className="w-40">{t('Combo')}</TableHead>
              <TableHead className="w-28">{t('State')}</TableHead>
              <TableHead className="w-56">{t('Action')}</TableHead>
            </TableRow>
          </TableHeader>
          <TableBody>
            {hotkeys.map((row) => (
              <TableRow key={`${row.pluginId}.${row.actionId}`}>
                <TableCell className="font-mono text-xs text-muted-foreground">{row.pluginId}</TableCell>
                <TableCell>
                  <Tooltip>
                    <TooltipTrigger asChild>
                      <span className="cursor-default">{row.displayName}</span>
                    </TooltipTrigger>
                    <TooltipContent>{row.actionId}</TooltipContent>
                  </Tooltip>
                </TableCell>
                <TableCell>
                  <button
                    type="button"
                    onClick={() => openEditor(row)}
                    className="rounded-md border border-transparent px-2 py-0.5 font-mono text-xs outline-none transition-colors hover:border-input hover:bg-accent focus-visible:ring-2 focus-visible:ring-ring"
                  >
                    {row.unbound ? <span className="text-muted-foreground">{t('Unbound')}</span> : row.combo}
                  </button>
                </TableCell>
                <TableCell>
                  {row.unbound ? (
                    <Badge variant="muted">{t('Unbound')}</Badge>
                  ) : row.conflicted ? (
                    <Badge variant="warning">{t('Conflict')}</Badge>
                  ) : (
                    <Badge variant="success">{t('Active')}</Badge>
                  )}
                </TableCell>
                <TableCell>
                  <div className="flex items-center gap-1">
                    <Button variant="outline" size="sm" onClick={() => openEditor(row)}>
                      {t('Edit')}
                    </Button>
                    <Button
                      variant="ghost"
                      size="sm"
                      onClick={() => void store.unbindHotkey(row.pluginId, row.actionId)}
                    >
                      {t('Unbind')}
                    </Button>
                    <Button variant="ghost" size="sm" onClick={() => void store.resetHotkey(row.pluginId, row.actionId)}>
                      {t('Reset')}
                    </Button>
                  </div>
                </TableCell>
              </TableRow>
            ))}
          </TableBody>
        </Table>
      )}

      <HotkeyEditor row={editing} open={open} onOpenChange={setOpen} />
    </div>
  )
}
