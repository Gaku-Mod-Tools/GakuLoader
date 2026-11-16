import * as React from 'react'
import { FlaskConical, Loader2, PlugZap, RefreshCw } from 'lucide-react'
import { Alert, AlertDescription } from '@/components/ui/alert'
import { Button } from '@/components/ui/button'
import { TooltipProvider } from '@/components/ui/tooltip'
import { motionStore, useMotionStore } from '../store'
import { useMotionAppearanceEffect, useMotionTextInputBridge } from '../hooks'
import { DropOverlay } from './DropOverlay'
import { ImportProgressModal } from './ImportProgressModal'
import { MotionErrorBoundary } from './MotionErrorBoundary'
import { MotionSidebar } from './MotionSidebar'
import { MotionStatusBar } from './MotionStatusBar'
import { PermissionGate } from './PermissionGate'
import { TransportBar } from './TransportBar'
import { LibraryPage } from '../pages/LibraryPage'
import { MotionPage } from '../pages/MotionPage'
import { CameraPage } from '../pages/CameraPage'
import { StagePage } from '../pages/StagePage'
import { SettingsPage } from '../pages/SettingsPage'
import { DeveloperPage } from '../pages/DeveloperPage'

function BootScreen({ error, canReconnect }: { error: string | null; canReconnect: boolean }) {
  return (
    <div className="flex h-full flex-col items-center justify-center gap-3 text-muted-foreground">
      {error ? (
        <>
          <PlugZap className="size-8 text-destructive" />
          <p className="max-w-md text-center text-sm">GakuMotion 桥不可用：{error}</p>
          {canReconnect && (
            <Button type="button" variant="outline" size="sm" onClick={() => void motionStore.restart()}>
              <RefreshCw className="size-3.5" />
              重新连接
            </Button>
          )}
        </>
      ) : (
        <>
          <Loader2 className="size-7 animate-spin text-primary" />
          <p className="text-sm">正在连接 GakuMotion…</p>
        </>
      )}
    </div>
  )
}

function ActivePage() {
  const page = useMotionStore((state) => state.page)
  switch (page) {
    case 'library':
      return <LibraryPage />
    case 'motion':
      return <MotionPage />
    case 'camera':
      return <CameraPage />
    case 'stage':
      return <StagePage />
    case 'settings':
      return <SettingsPage />
    case 'developer':
      return <DeveloperPage />
  }
}

export default function MotionApp() {
  const ready = useMotionStore((state) => state.ready)
  const connected = useMotionStore((state) => state.connected)
  const error = useMotionStore((state) => state.error)
  const bridgeKind = useMotionStore((state) => state.bridgeKind)
  const permission = useMotionStore((state) => state.permission)
  const appearance = useMotionStore((state) => state.appearance)
  const page = useMotionStore((state) => state.page)
  const textInputSupported = useMotionStore((state) => state.capabilities.includes('shell.setTextInputActive'))

  React.useEffect(() => {
    void motionStore.start()
  }, [])

  useMotionAppearanceEffect(appearance)
  useMotionTextInputBridge(textInputSupported)

  return (
    <MotionErrorBoundary scope="app" onRestart={() => void motionStore.restart()}>
      {!ready || !connected ? (
        <BootScreen error={error ?? (ready ? '桥已断开' : null)} canReconnect={ready} />
      ) : (
        <TooltipProvider delayDuration={250}>
          <div className="relative flex h-full flex-col">
            {bridgeKind === 'mock' && (
              <Alert variant="warning" className="rounded-none border-x-0 border-t-0 py-1.5" data-testid="mock-banner">
                <FlaskConical />
                <AlertDescription className="text-xs">
                  开发 Mock 模式：数据为本地模拟，不代表真实插件行为；未实现功能标记为“待迁移”。
                </AlertDescription>
              </Alert>
            )}
            {permission.injectionAllowed ? (
              <>
                <div className="flex min-h-0 flex-1">
                  <MotionSidebar />
                  <main className="flex min-w-0 flex-1 flex-col">
                    <MotionStatusBar />
                    <div className="min-h-0 flex-1 overflow-y-auto p-4">
                      <MotionErrorBoundary
                        scope="page"
                        resetKey={page}
                        onResync={() => void motionStore.resync()}
                        onNavigateLibrary={() => motionStore.setPage('library')}
                      >
                        <ActivePage />
                      </MotionErrorBoundary>
                    </div>
                  </main>
                </div>
                <TransportBar />
              </>
            ) : (
              <PermissionGate />
            )}
            <DropOverlay />
            <ImportProgressModal />
          </div>
        </TooltipProvider>
      )}
    </MotionErrorBoundary>
  )
}
