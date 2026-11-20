"use client";

import * as React from "react";
import { NavLink, useLocation } from "react-router-dom";
import { cn } from "@/lib/utils";
import {
  LayoutDashboard,
  Home,
  Grid,
  Video,
  Settings,
  Cpu,
  ChevronLeft,
  ChevronRight,
} from "lucide-react";
import { useTheme } from "@/hooks/useTheme";
import { Button } from "@/components/ui/button";
import { Tooltip, TooltipContent, TooltipProvider, TooltipTrigger } from "@/components/ui/tooltip";

const navigation = [
  { name: "Overview", href: "/", icon: LayoutDashboard },
  { name: "Rooms", href: "/rooms", icon: Home },
  { name: "Scenes", href: "/scenes", icon: Grid },
  { name: "Floor Plans", href: "/floorplans", icon: Video },
  { name: "Devices", href: "/devices", icon: Settings },
  { name: "Hardware", href: "/hardware", icon: Cpu },
] as const;

export function Sidebar() {
  const location = useLocation();
  const { resolved, cycleMode } = useTheme();
  const [collapsed, setCollapsed] = React.useState(false);

  return (
    <TooltipProvider delayDuration={300}>
      <aside
        className={cn(
          "fixed left-0 top-0 z-40 h-full border-r bg-card transition-all duration-200",
          collapsed ? "w-16" : "w-64"
        )}
      >
        <div className="flex h-full flex-col">
          {/* Header */}
          <div className="flex h-16 items-center justify-between border-b px-4">
            {!collapsed && (
              <NavLink to="/" className="flex items-center gap-2 font-semibold text-lg">
                <LayoutDashboard className="h-6 w-6" />
                <span>Lumen</span>
              </NavLink>
            )}
            <Tooltip>
              <TooltipTrigger asChild>
                <Button
                  variant="ghost"
                  size="icon"
                  onClick={() => setCollapsed(!collapsed)}
                  aria-label={collapsed ? "Expand sidebar" : "Collapse sidebar"}
                >
                  {collapsed ? <ChevronRight className="h-4 w-4" /> : <ChevronLeft className="h-4 w-4" />}
                </Button>
              </TooltipTrigger>
              <TooltipContent side="right">{collapsed ? "Expand" : "Collapse"}</TooltipContent>
            </Tooltip>
          </div>

          {/* Navigation */}
          <nav className="flex-1 space-y-1 p-2" aria-label="Main navigation">
            {navigation.map((item) => {
              const isActive = location.pathname === item.href ||
                (item.href !== "/" && location.pathname.startsWith(item.href));
              const showTooltip = collapsed && !isActive;
              return (
                <Tooltip key={item.name} open={showTooltip} onOpenChange={() => {}}>
                  <TooltipTrigger asChild>
                    <NavLink
                      to={item.href}
                      className={cn(
                        "flex items-center gap-3 rounded-lg px-3 py-2 text-sm font-medium transition-colors",
                        "hover:bg-accent hover:text-accent-foreground",
                        isActive && "bg-primary text-primary-foreground",
                        collapsed && "justify-center px-2"
                      )}
                      aria-current={isActive ? "page" : undefined}
                    >
                      <item.icon className="h-5 w-5 flex-shrink-0" aria-hidden="true" />
                      {!collapsed && <span>{item.name}</span>}
                    </NavLink>
                  </TooltipTrigger>
                  <TooltipContent side="right">{item.name}</TooltipContent>
                </Tooltip>
              );
            })}
          </nav>

          {/* Footer */}
          <div className="border-t p-4">
            <Tooltip>
              <TooltipTrigger asChild>
                <Button
                  variant="ghost"
                  size="icon"
                  onClick={cycleMode}
                  className={cn("w-full justify-start", collapsed && "justify-center")}
                  aria-label="Cycle theme"
                >
                  {resolved === "dark" ? (
                    <span className="text-lg">🌙</span>
                  ) : (
                    <span className="text-lg">☀️</span>
                  )}
                  {!collapsed && <span className="ml-3 text-sm capitalize">{resolved}</span>}
                </Button>
              </TooltipTrigger>
              <TooltipContent side="right">Cycle theme (light/dark/system)</TooltipContent>
            </Tooltip>
          </div>
        </div>
      </aside>
    </TooltipProvider>
  );
}