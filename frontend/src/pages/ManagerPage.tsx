import { AlertOctagon } from 'lucide-react'
import { Tabs, TabsContent, TabsList, TabsTrigger } from '@/components/ui/tabs'
import { AppearancePage } from '@/pages/AppearancePage'
import { HotkeysPage } from '@/pages/HotkeysPage'
import { PluginListPage } from '@/pages/PluginListPage'
import { SettingsPage } from '@/pages/SettingsPage'
import { store, useStore, useT, type ManagerTab } from '@/state/store'

export function ManagerPage() {
  const t = useT()
  const activeTab = useStore((s) => s.activeTab)
  const saveFailed = useStore((s) => s.saveFailed)

  return (
    <div className="space-y-3">
      <h2 className="text-base font-semibold tracking-tight">{t('Manager')}</h2>

      {saveFailed && (
        <div className="flex items-center gap-2 rounded-md border border-destructive/40 bg-destructive/10 px-3 py-2 text-sm text-destructive animate-fade-in">
          <AlertOctagon className="size-4 shrink-0" />
          {t('SaveFailed')}
        </div>
      )}

      <Tabs value={activeTab} onValueChange={(value) => store.setActiveTab(value as ManagerTab)}>
        <TabsList>
          <TabsTrigger value="plugin-list">{t('PluginList')}</TabsTrigger>
          <TabsTrigger value="appearance">{t('Appearance')}</TabsTrigger>
          <TabsTrigger value="hotkeys">{t('Hotkeys')}</TabsTrigger>
          <TabsTrigger value="settings">{t('Settings')}</TabsTrigger>
        </TabsList>
        <TabsContent value="plugin-list">
          <PluginListPage />
        </TabsContent>
        <TabsContent value="appearance">
          <AppearancePage />
        </TabsContent>
        <TabsContent value="hotkeys">
          <HotkeysPage />
        </TabsContent>
        <TabsContent value="settings">
          <SettingsPage />
        </TabsContent>
      </Tabs>
    </div>
  )
}
