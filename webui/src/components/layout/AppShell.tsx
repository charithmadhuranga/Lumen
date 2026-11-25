// App shell (DESIGN §6.1): desktop sidebar (Control / Configure groups),
// tablet icon rail, mobile top bar + bottom navigation + menu sheet.
import * as React from "react";
import { NavLink, Outlet } from "react-router-dom";
import * as DialogPrimitive from "@radix-ui/react-dialog";
import {
  Cpu,
  Home,
  LayoutDashboard,
  Lightbulb,
  LogOut,
  Map,
  Menu,
  Monitor,
  Moon,
  Network,
  Plug,
  Settings,
  Sparkles,
  Sun,
  Workflow,
  X,
} from "lucide-react";

import { useAuth } from "@/hooks/useAuth";
import { useTheme } from "@/hooks/useTheme";
import { useConnectionStatus } from "@/stores/liveStore";
import { cn } from "@/lib/utils";

const CONTROL = [
  { to: "/", label: "Overview", icon: LayoutDashboard },
  { to: "/rooms", label: "Rooms", icon: Home },
  { to: "/floorplans", label: "Floor Plans", icon: Map },
  { to: "/scenes", label: "Scenes", icon: Sparkles },
  { to: "/automations", label: "Automations", icon: Workflow },
  { to: "/devices", label: "Devices", icon: Lightbulb },
] as const;

const CONFIGURE = [
  { to: "/hardware", label: "Hardware", icon: Cpu },
  { to: "/integrations", label: "Integrations", icon: Plug },
  { to: "/network", label: "Network", icon: Network },
  { to: "/settings", label: "Settings", icon: Settings },
] as const;

const MOBILE = [CONTROL[0], CONTROL[1], CONTROL[3], CONTROL[2]];

type NavDef = { to: string; label: string; icon: typeof Home };

function NavItem({ to, label, icon: Icon, rail, onNavigate }: NavDef & { rail?: boolean; onNavigate?: () => void }) {
  return (
    <NavLink
      to={to}
      end={to === "/"}
      onClick={onNavigate}
      title={rail ? label : undefined}
      className={({ isActive }) =>
        cn(
          "flex min-h-11 items-center gap-3 rounded-lg px-3 text-sm font-medium transition-colors focus-visible:outline-none focus-visible:ring-3 focus-visible:ring-ring/50",
          isActive ? "bg-primary/15 text-foreground" : "text-muted-foreground hover:bg-muted hover:text-foreground",
          rail && "justify-center px-0",
        )
      }
    >
      <Icon className="size-5 shrink-0" aria-hidden="true" />
      <span className={cn(rail && "sr-only")}>{label}</span>
    </NavLink>
  );
}

function NavGroups({ rail, onNavigate }: { rail?: boolean; onNavigate?: () => void }) {
  return (
    <nav aria-label="Main" className="flex flex-1 flex-col gap-1 p-2">
      {!rail && <p className="px-3 pb-1 pt-2 text-xs font-medium uppercase tracking-wide text-muted-foreground">Control</p>}
      {CONTROL.map((i) => (
        <NavItem key={i.to} {...i} rail={rail} onNavigate={onNavigate} />
      ))}
      <div className="my-2 border-t" role="separator" />
      {!rail && <p className="px-3 pb-1 text-xs font-medium uppercase tracking-wide text-muted-foreground">Configure</p>}
      {CONFIGURE.map((i) => (
        <NavItem key={i.to} {...i} rail={rail} onNavigate={onNavigate} />
      ))}
    </nav>
  );
}

function ThemeButton({ rail }: { rail?: boolean }) {
  const { mode, cycleMode } = useTheme();
  const Icon = mode === "dark" ? Moon : mode === "light" ? Sun : Monitor;
  return (
    <button
      type="button"
      onClick={cycleMode}
      aria-label={rail ? `${mode} theme (change)` : undefined}
      title={`Change theme (now ${mode})`}
      className={cn(
        "flex min-h-11 w-full items-center gap-3 rounded-lg px-3 text-sm text-muted-foreground hover:bg-muted hover:text-foreground focus-visible:outline-none focus-visible:ring-3 focus-visible:ring-ring/50",
        rail && "justify-center px-0",
      )}
    >
      <Icon className="size-5" aria-hidden="true" />
      <span className={cn("capitalize", rail && "sr-only")}>{mode} theme</span>
    </button>
  );
}

function ConnectionBanner() {
  const status = useConnectionStatus();
  if (status === "connected" || status === "idle") return null;
  return (
    <div
      role="status"
      className={cn(
        "flex items-center justify-center gap-2 px-4 py-1.5 text-xs font-medium",
        status === "offline" ? "bg-destructive/15 text-destructive" : "bg-warning/20 text-foreground",
      )}
    >
      {status === "offline"
        ? "Controller unreachable — live updates paused. Retrying…"
        : status === "connecting"
          ? "Connecting to the controller…"
          : "Connection lost — reconnecting…"}
    </div>
  );
}

export function AppShell() {
  const { user, logout } = useAuth();
  const [menuOpen, setMenuOpen] = React.useState(false);
  const status = useConnectionStatus();

  const brand = (
    <div className="flex items-center gap-2 font-semibold">
      <span className="grid size-8 place-items-center rounded-lg bg-primary text-primary-foreground">
        <Lightbulb className="size-4" aria-hidden="true" />
      </span>
      <span>Lumen</span>
    </div>
  );

  return (
    <div className="flex min-h-dvh bg-background">
      <aside className="sticky top-0 hidden h-dvh shrink-0 flex-col border-r bg-card sm:flex sm:w-16 lg:w-60">
        <div className="flex h-14 items-center justify-center border-b px-3 lg:justify-start">
          <span className="lg:hidden">
            <Lightbulb className="size-5 text-primary" aria-label="Lumen" />
          </span>
          <span className="hidden lg:block">{brand}</span>
        </div>
        <div className="hidden flex-1 flex-col overflow-y-auto lg:flex">
          <NavGroups />
        </div>
        <div className="flex flex-1 flex-col overflow-y-auto lg:hidden">
          <NavGroups rail />
        </div>
        <div className="border-t p-2">
          <div className="hidden lg:block">
            <ThemeButton />
          </div>
          <div className="lg:hidden">
            <ThemeButton rail />
          </div>
        </div>
      </aside>

      <div className="flex min-w-0 flex-1 flex-col">
        <header className="sticky top-0 z-30 flex h-14 items-center gap-3 border-b bg-background/90 px-4 backdrop-blur">
          <button
            type="button"
            className="-ml-2 rounded-md p-2 hover:bg-muted sm:hidden"
            aria-label="Open menu"
            onClick={() => setMenuOpen(true)}
          >
            <Menu className="size-5" />
          </button>
          <span className="sm:hidden">{brand}</span>
          <div className="ml-auto flex items-center gap-3">
            <span
              role="img"
              className={cn(
                "size-2 rounded-full",
                status === "connected" ? "bg-success" : status === "offline" ? "bg-destructive" : "bg-warning",
              )}
              aria-label={status === "connected" ? "Controller online" : "Controller connection degraded"}
            />
            <span className="hidden text-sm text-muted-foreground sm:inline">{user?.username}</span>
            <button
              type="button"
              onClick={() => void logout()}
              className="rounded-md p-2 text-muted-foreground hover:bg-muted hover:text-foreground"
              aria-label="Sign out"
              title="Sign out"
            >
              <LogOut className="size-4" />
            </button>
          </div>
        </header>
        <ConnectionBanner />
        <main className="mx-auto w-full max-w-7xl flex-1 px-4 pb-24 pt-6 sm:pb-8 lg:px-8">
          <Outlet />
        </main>
      </div>

      <nav aria-label="Quick" className="fixed inset-x-0 bottom-0 z-30 grid grid-cols-4 border-t bg-card sm:hidden">
        {MOBILE.map(({ to, label, icon: Icon }) => (
          <NavLink
            key={to}
            to={to}
            end={to === "/"}
            className={({ isActive }) =>
              cn("flex min-h-14 flex-col items-center justify-center gap-0.5 text-xs", isActive ? "text-link font-medium" : "text-muted-foreground")
            }
          >
            <Icon className="size-5" aria-hidden="true" />
            {label}
          </NavLink>
        ))}
      </nav>

      <DialogPrimitive.Root open={menuOpen} onOpenChange={setMenuOpen}>
        <DialogPrimitive.Portal>
          <DialogPrimitive.Overlay className="fixed inset-0 z-40 bg-black/40 sm:hidden" />
          <DialogPrimitive.Content className="fixed inset-y-0 left-0 z-50 flex w-72 max-w-[85vw] flex-col bg-card shadow-xl sm:hidden">
            <DialogPrimitive.Title className="sr-only">Navigation</DialogPrimitive.Title>
            <DialogPrimitive.Description className="sr-only">Main navigation</DialogPrimitive.Description>
            <div className="flex h-14 items-center justify-between border-b px-4">
              {brand}
              <DialogPrimitive.Close className="rounded-md p-2 hover:bg-muted" aria-label="Close menu">
                <X className="size-5" />
              </DialogPrimitive.Close>
            </div>
            <div className="flex-1 overflow-y-auto">
              <NavGroups onNavigate={() => setMenuOpen(false)} />
            </div>
            <div className="border-t p-2">
              <ThemeButton />
            </div>
          </DialogPrimitive.Content>
        </DialogPrimitive.Portal>
      </DialogPrimitive.Root>
    </div>
  );
}
