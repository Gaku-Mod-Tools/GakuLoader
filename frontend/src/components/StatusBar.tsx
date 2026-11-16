import { useStore, useT } from '@/state/store'

// Mirrors the ImGui footer: "GakuLoader: N | Gakumas: Ready/Waiting/Stopping".
export function StatusBar() {
  const t = useT()
  const snapshot = useStore((s) => s.snapshot)
  const count = snapshot?.plugins.length ?? 0
  const runtime = snapshot?.shuttingDown ? t('Stopping') : snapshot?.runtimeReady ? t('Ready') : t('Waiting')

  return (
    <footer className="flex h-7 shrink-0 items-center gap-2 border-t bg-sidebar px-3 text-xs text-muted-foreground">
      <span>
        GakuLoader: <span className="font-medium text-sidebar-foreground">{count}</span>
      </span>
      <span className="opacity-40">|</span>
      <span>
        Gakumas: <span className="font-medium text-sidebar-foreground">{runtime}</span>
      </span>
    </footer>
  )
}
