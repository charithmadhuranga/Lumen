"use client";

import * as React from "react";
import { NavLink, useLocation } from "react-router-dom";
import { cn } from "@/lib/utils";
import { X, LayoutDashboard, Home, Grid, Video, Settings } from "lucide-react";

const navigation = [
  { name: "Overview", href: "/", icon: LayoutDashboard },
  { name: "Rooms", href: "/rooms", icon: Home },
  { name: "Scenes", href: "/scenes", icon: Grid },
  { name: "Floor Plans", href: "/floorplans", icon: Video },
  { name: "Devices", href: "/devices", icon: Settings },
] as const;

export function MobileDrawer({ open, onOpenChange }: { open: boolean; onOpenChange: (open: boolean) => void }) {
  const location = useLocation();

  if (!open) return null;

  return (
    <div className="fixed inset-0 z-50 flex flex-col lg:hidden">
      <div className="absolute inset-0 bg-black/50 backdrop-blur-sm" onClick={() => onOpenChange(false)} />
      <div className="relative z-50 w-full max-w-xs bg-card border border-border shadow-lg">
        <div className="flex h-16 items-center justify-between border-b px-4">
          <NavLink to="/" className="flex items-center gap-2 font-semibold text-lg">
            <LayoutDashboard className="h-6 w-6" />
            <span>Lumen</span>
          </NavLink>
          <button
            className="p-2"
            onClick={() => onOpenChange(false)}
            aria-label="Close menu"
          >
            <X className="h-6 w-6" />
          </button>
        </div>

        <nav className="flex-1 space-y-1 p-2" aria-label="Main navigation">
          {navigation.map((item) => {
            const isActive = location.pathname === item.href ||
              (item.href !== "/" && location.pathname.startsWith(item.href));
            return (
              <NavLink
                key={item.name}
                to={item.href}
                className={cn(
                  "flex items-center gap-3 rounded-lg px-3 py-2 text-sm font-medium transition-colors",
                  "hover:bg-accent hover:text-accent-foreground",
                  isActive && "bg-primary text-primary-foreground"
                )}
                aria-current={isActive ? "page" : undefined}
                onClick={() => onOpenChange(false)}
              >
                <item.icon className="h-5 w-5 flex-shrink-0" aria-hidden="true" />
                <span>{item.name}</span>
              </NavLink>
            );
          })}
        </nav>

        <div className="border-t p-4">
          <p className="text-xs text-muted-foreground">Lumen v0.1.0-dev</p>
        </div>
      </div>
    </div>
  );
}