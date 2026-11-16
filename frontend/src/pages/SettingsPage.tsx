import { Checkbox } from '@/components/ui/checkbox'
import { Separator } from '@/components/ui/separator'
import { store, useStore, useT } from '@/state/store'

export function SettingsPage() {
  const t = useT()
  const settings = useStore((s) => s.settings)
  const loaded = settings !== null

  return (
    <div className="max-w-lg space-y-4">
      <p className="text-sm text-muted-foreground">{t('NextStart')}</p>

      <div className="space-y-3">
        <label className="flex items-center gap-2.5 text-sm">
          <Checkbox
            checked={settings?.console ?? false}
            disabled={!loaded}
            onCheckedChange={(checked) => void store.setConsole(checked === true)}
          />
          <span>{t('Console')}</span>
        </label>
        <label className="flex items-center gap-2.5 text-sm">
          <Checkbox
            checked={settings?.trace ?? false}
            disabled={!loaded}
            onCheckedChange={(checked) => void store.setTrace(checked === true)}
          />
          <span>{t('Trace')}</span>
        </label>
      </div>

      <Separator />

      <div className="space-y-1">
        <h3 className="text-sm font-semibold">{t('PluginEnable')}</h3>
        <p className="text-sm text-muted-foreground">{t('PluginEnableHelp')}</p>
      </div>

      <div className="space-y-2.5">
        {(settings?.plugins ?? []).map((plugin) => (
          <label key={plugin.filename} className="flex items-center gap-2.5 text-sm">
            <Checkbox
              checked={plugin.enabled}
              onCheckedChange={(checked) => void store.setPluginEnabled(plugin.filename, checked === true)}
            />
            <span className="font-mono text-xs">{plugin.filename}</span>
          </label>
        ))}
      </div>
    </div>
  )
}
