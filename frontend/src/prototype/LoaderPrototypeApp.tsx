import * as React from 'react'
import { Loader2, PlugZap } from 'lucide-react'
import { MenuBar } from '@/components/MenuBar'
import { PluginHostPlaceholder } from '@/components/PluginHostPlaceholder'
import { Sidebar } from '@/components/Sidebar'
import { StatusBar } from '@/components/StatusBar'
import { TooltipProvider } from '@/components/ui/tooltip'
import { useAppearanceEffect, useTextInputBridge } from '@/lib/appHooks'
import { ManagerPage } from '@/pages/ManagerPage'
import { store, useStore } from '@/state/store'

function BootScreen({ error }: { error: string | null }) {
  return (
    <div className="flex h-full flex-col items-center justify-center gap-3 text-muted-foreground">
      {error ? (
        <>
          <PlugZap className="size-8 text-destructive" />
          <p className="text-sm">Bridge unavailable: {error}</p>
        </>
      ) : (
        <>
          <Loader2 className="size-7 animate-spin text-primary" />
          <p className="text-sm">Loading GakuLoader…</p>
        </>
      )}
    </div>
  )
}

export default function LoaderPrototypeApp() {
  const ready = useStore((s) => s.ready)
  const connected = useStore((s) => s.connected)
  const error = useStore((s) => s.error)
  const appearance = useStore((s) => s.appearance)
  const selectedPanel = useStore((s) => s.selectedPanel)
  const managerId = useStore((s) => s.shell?.managerPanelId ?? 'gakuloader.manager')

  React.useEffect(() => {
    void store.start()
  }, [])

  useAppearanceEffect(appearance)
  useTextInputBridge()

  if (!ready || !connected) return <BootScreen error={error ?? (ready ? 'Bridge disconnected' : null)} />

  const isManager = selectedPanel === managerId

  return (
    <TooltipProvider delayDuration={250}>
      <div className="flex h-full flex-col">
        <MenuBar />
        <div className="flex min-h-0 flex-1">
          <Sidebar />
          <main className="min-w-0 flex-1 overflow-y-auto p-4">
            {isManager ? <ManagerPage /> : <PluginHostPlaceholder />}
          </main>
        </div>
        <StatusBar />
      </div>
    </TooltipProvider>
  )
}
