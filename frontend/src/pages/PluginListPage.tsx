import { Badge } from '@/components/ui/badge'
import { Spinner } from '@/components/ui/spinner'
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from '@/components/ui/table'
import { Tooltip, TooltipContent, TooltipTrigger } from '@/components/ui/tooltip'
import { useStore, useT } from '@/state/store'
import type { PluginPanel } from '@/bridge/protocol'

function StateBadge({ plugin }: { plugin: PluginPanel }) {
  const t = useT()
  if (plugin.failed) return <Badge variant="destructive">{t('Failed')}</Badge>
  if (plugin.runtimeReady) return <Badge variant="success">{t('Ready')}</Badge>
  if (plugin.activated) return <Badge variant="warning">{t('Activated')}</Badge>
  return <Badge variant="muted">{t('Inactive')}</Badge>
}

export function PluginListPage() {
  const t = useT()
  const snapshot = useStore((s) => s.snapshot)
  const plugins = snapshot?.plugins ?? []
  const runtime = snapshot?.runtimeReady ? t('Ready') : t('Waiting')
  const lifecycle = snapshot?.shuttingDown ? t('Stopping') : t('Running')

  return (
    <div className="space-y-3">
      <p className="text-sm text-muted-foreground">{t('SummaryHelp')}</p>
      <div className="flex flex-wrap items-center gap-x-2 gap-y-1 text-sm">
        <span className="text-muted-foreground">{t('Count')}:</span>
        <span className="font-semibold">{plugins.length}</span>
        <span className="opacity-40">|</span>
        <span className="text-muted-foreground">{t('Runtime')}:</span>
        <span className="font-medium">{runtime}</span>
        <span className="opacity-40">|</span>
        <span className="font-medium">{lifecycle}</span>
      </div>

      <Table>
        <TableHeader>
          <TableRow>
            <TableHead>{t('Plugin')}</TableHead>
            <TableHead className="w-32">{t('Version')}</TableHead>
            <TableHead className="w-44">{t('State')}</TableHead>
            <TableHead className="w-20">{t('Panel')}</TableHead>
          </TableRow>
        </TableHeader>
        <TableBody>
          {plugins.map((plugin) => (
            <TableRow key={plugin.id}>
              <TableCell>
                <Tooltip>
                  <TooltipTrigger asChild>
                    <span className="cursor-default font-medium">{plugin.name}</span>
                  </TooltipTrigger>
                  <TooltipContent>{plugin.id}</TooltipContent>
                </Tooltip>
              </TableCell>
              <TableCell className="font-mono text-xs text-muted-foreground">{plugin.version}</TableCell>
              <TableCell>
                <span className="flex items-center gap-2">
                  <StateBadge plugin={plugin} />
                  {(plugin.failed || !plugin.runtimeReady) && <Spinner />}
                </span>
              </TableCell>
              <TableCell className="text-muted-foreground">{plugin.hasPanel ? t('Yes') : t('No')}</TableCell>
            </TableRow>
          ))}
        </TableBody>
      </Table>
    </div>
  )
}
