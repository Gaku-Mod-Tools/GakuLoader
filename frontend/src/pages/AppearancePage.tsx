import { AlertTriangle } from 'lucide-react'
import { Label } from '@/components/ui/label'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select'
import { store, useStore, useT } from '@/state/store'

const SCALE_PERCENTS = [50, 75, 90, 100, 110, 125, 150, 175, 200, 250, 300]

export function AppearancePage() {
  const t = useT()
  const appearance = useStore((s) => s.appearance)
  const language = useStore((s) => s.language)
  const scaleLimited = useStore((s) => s.scaleLimited)
  const theme = appearance?.theme ?? 0
  const currentPercent = Math.round((appearance?.uiScale ?? 1) * 100)
  const themeKeys = ['Dark', 'Sapphire', 'Paper', 'AppleLight', 'AppleDark']
  const languageNames = language?.names ?? ['English', '简体中文', '繁體中文', '日本語']

  return (
    <div className="max-w-md space-y-5">
      {scaleLimited && (
        <div className="flex items-start gap-2 rounded-md border border-warning/40 bg-warning/10 p-3 text-sm text-warning">
          <AlertTriangle className="mt-0.5 size-4 shrink-0" />
          <span>{t('ScaleLimit')}</span>
        </div>
      )}

      <div className="space-y-2">
        <Label htmlFor="theme">{t('Theme')}</Label>
        <Select value={String(theme)} onValueChange={(value) => void store.setTheme(Number(value))}>
          <SelectTrigger id="theme" className="w-[240px]">
            <SelectValue />
          </SelectTrigger>
          <SelectContent>
            {themeKeys.map((key, index) => (
              <SelectItem key={key} value={String(index)}>
                {t(key)}
              </SelectItem>
            ))}
          </SelectContent>
        </Select>
      </div>

      <div className="space-y-2">
        <Label htmlFor="scale">{t('Scale')}</Label>
        <Select value={String(currentPercent)} onValueChange={(value) => void store.setScale(Number(value))}>
          <SelectTrigger id="scale" className="w-[240px]">
            <SelectValue />
          </SelectTrigger>
          <SelectContent>
            {SCALE_PERCENTS.map((percent) => (
              <SelectItem key={percent} value={String(percent)}>
                {percent}%
              </SelectItem>
            ))}
          </SelectContent>
        </Select>
      </div>

      <div className="space-y-2">
        <Label htmlFor="language">{t('Languages')}</Label>
        <Select
          value={String(language?.language ?? 0)}
          onValueChange={(value) => void store.setLanguage(Number(value))}
        >
          <SelectTrigger id="language" className="w-[240px]">
            <SelectValue />
          </SelectTrigger>
          <SelectContent>
            {languageNames.map((name, index) => (
              <SelectItem key={index} value={String(index)}>
                {name}
              </SelectItem>
            ))}
          </SelectContent>
        </Select>
      </div>

      <p className="text-sm text-muted-foreground">{t('LanguageHelp')}</p>
    </div>
  )
}
