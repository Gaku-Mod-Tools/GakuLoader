import { Puzzle } from 'lucide-react'
import { Spinner } from '@/components/ui/spinner'
import { useStore, useT } from '@/state/store'

// Plugin panels are drawn by the native ImGui surface (frozen plugin ABI), not by
// React. In production the WebView2 child is hidden the moment a plugin is
// selected, so this placeholder is only seen in the browser dev/mock preview. It
// reproduces the ImGui plugin-host fallback states for a coherent preview.
export function PluginHostPlaceholder() {
  const t = useT()
  const snapshot = useStore((s) => s.snapshot)
  const selectedPanel = useStore((s) => s.selectedPanel)
  const plugin = snapshot?.plugins.find((p) => p.id === selectedPanel)

  return (
    <div className="space-y-3">
      <div className="flex items-center gap-2">
        <Puzzle className="size-5 text-primary" />
        <h2 className="text-base font-semibold tracking-tight">{plugin?.name ?? 'GakuLoader'}</h2>
        {plugin && <span className="font-mono text-xs text-muted-foreground">{plugin.version}</span>}
      </div>
      <div className="rounded-lg border bg-card p-6 text-sm text-muted-foreground">
        {!plugin ? (
          <p>{t('SelectPanel')}</p>
        ) : plugin.failed ? (
          <p className="text-destructive">{t('FailedHelp')}</p>
        ) : !plugin.hasPanel ? (
          <p>{t('NoPanel')}</p>
        ) : !plugin.runtimeReady ? (
          <p className="flex items-center gap-2">
            <Spinner /> {t('Waiting')}
          </p>
        ) : (
          <p>This panel is rendered by the native ImGui surface in the GakuLoader control window's plugin content area.</p>
        )}
      </div>
    </div>
  )
}
