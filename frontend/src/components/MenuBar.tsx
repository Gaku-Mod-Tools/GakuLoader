import { Check, Eye, FileText, Puzzle } from 'lucide-react'
import {
  DropdownMenu, DropdownMenuContent, DropdownMenuItem, DropdownMenuLabel,
  DropdownMenuSeparator, DropdownMenuShortcut, DropdownMenuTrigger,
} from '@/components/ui/dropdown-menu'
import { store, useStore, useT } from '@/state/store'

// Mirrors the ImGui menu bar: File (Hide), View (Manager F1), Plugins (select).
export function MenuBar() {
  const t = useT()
  const snapshot = useStore((s) => s.snapshot)
  const selectedPanel = useStore((s) => s.selectedPanel)
  const managerId = useStore((s) => s.shell?.managerPanelId ?? 'gakuloader.manager')
  const plugins = snapshot?.plugins ?? []

  return (
    <header className="flex h-9 shrink-0 items-center gap-0.5 border-b bg-sidebar px-1.5 text-sidebar-foreground">
      <DropdownMenu>
        <DropdownMenuTrigger className="flex items-center gap-1.5 rounded-md px-2.5 py-1 text-sm outline-none transition-colors hover:bg-sidebar-accent hover:text-sidebar-accent-foreground focus-visible:bg-sidebar-accent data-[state=open]:bg-sidebar-accent">
          <FileText className="size-3.5 opacity-70" />
          {t('File')}
        </DropdownMenuTrigger>
        <DropdownMenuContent align="start" className="min-w-[12rem]">
          <DropdownMenuItem onSelect={() => void store.hideWindow()}>
            {t('Hide')}
          </DropdownMenuItem>
        </DropdownMenuContent>
      </DropdownMenu>

      <DropdownMenu>
        <DropdownMenuTrigger className="flex items-center gap-1.5 rounded-md px-2.5 py-1 text-sm outline-none transition-colors hover:bg-sidebar-accent hover:text-sidebar-accent-foreground focus-visible:bg-sidebar-accent data-[state=open]:bg-sidebar-accent">
          <Eye className="size-3.5 opacity-70" />
          {t('View')}
        </DropdownMenuTrigger>
        <DropdownMenuContent align="start" className="min-w-[12rem]">
          <DropdownMenuItem onSelect={() => void store.selectPanel(managerId)}>
            {t('Manager')}
            <DropdownMenuShortcut>F1</DropdownMenuShortcut>
          </DropdownMenuItem>
        </DropdownMenuContent>
      </DropdownMenu>

      <DropdownMenu>
        <DropdownMenuTrigger className="flex items-center gap-1.5 rounded-md px-2.5 py-1 text-sm outline-none transition-colors hover:bg-sidebar-accent hover:text-sidebar-accent-foreground focus-visible:bg-sidebar-accent data-[state=open]:bg-sidebar-accent">
          <Puzzle className="size-3.5 opacity-70" />
          {t('Plugins')}
        </DropdownMenuTrigger>
        <DropdownMenuContent align="start" className="min-w-[14rem]">
          <DropdownMenuLabel>{t('Plugins')}</DropdownMenuLabel>
          <DropdownMenuSeparator />
          {plugins.map((plugin) => (
            <DropdownMenuItem key={plugin.id} onSelect={() => void store.selectPanel(plugin.id)}>
              <span className="flex size-4 items-center justify-center">
                {selectedPanel === plugin.id && <Check className="size-4 text-primary" />}
              </span>
              {plugin.name}
            </DropdownMenuItem>
          ))}
        </DropdownMenuContent>
      </DropdownMenu>
    </header>
  )
}
