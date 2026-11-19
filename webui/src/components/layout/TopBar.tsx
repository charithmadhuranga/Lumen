"use client";

import * as React from "react";
import { useNavigate, useLocation } from "react-router-dom";
import { cn } from "@/lib/utils";
import { Button } from "@/components/ui/button";
import { Separator } from "@/components/ui/separator";
import { useTheme } from "@/hooks/useTheme";
import { useAuth } from "@/hooks/useAuth";
import { Menu, X, User, LogOut, Sun, Moon, Monitor, Settings } from "lucide-react";
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuSeparator,
  DropdownMenuTrigger,
} from "@/components/ui/dropdown-menu";
import { Tooltip, TooltipContent, TooltipProvider, TooltipTrigger } from "@/components/ui/tooltip";
import { useConnectionStatus } from "@/stores/liveStore";

export function TopBar() {
  const navigate = useNavigate();
  const location = useLocation();
  const { resolved, cycleMode } = useTheme();
  const { user, logout } = useAuth();
  const [mobileMenuOpen, setMobileMenuOpen] = React.useState(false);
  const status = useConnectionStatus();

  const isAuthPage = location.pathname.startsWith("/login") || location.pathname.startsWith("/setup");

  if (isAuthPage) return null;

  return (
    <TooltipProvider delayDuration={300}>
      <header className={cn(
        "sticky top-0 z-30 flex h-16 items-center gap-4 border-b bg-background/95 backdrop-blur supports-[backdrop-filter]:bg-background/60 px-4",
        "transition-all duration-200"
      )}>
        {/* Mobile menu toggle */}
        <Button
          variant="ghost"
          size="icon"
          className="lg:hidden"
          onClick={() => setMobileMenuOpen(!mobileMenuOpen)}
          aria-label={mobileMenuOpen ? "Close menu" : "Open menu"}
        >
          {mobileMenuOpen ? <X className="h-6 w-6" /> : <Menu className="h-6 w-6" />}
        </Button>

        {/* Page title */}
        <div className="flex-1 lg:hidden">
          <h1 className="text-lg font-semibold truncate">
            {getPageTitle(location.pathname)}
          </h1>
        </div>

        {/* Connection status */}
        <Tooltip>
          <TooltipTrigger asChild>
            <div className={cn(
              "flex items-center gap-1.5 rounded-lg px-2 py-1 text-xs font-medium transition-colors",
              status === "connected" ? "bg-success/10 text-success" :
              status === "connecting" ? "bg-warning/10 text-warning" :
              "bg-destructive/10 text-destructive"
            )}>
              <span className={cn(
                "h-1.5 w-1.5 rounded-full",
                status === "connected" && "bg-success",
                status === "connecting" && "bg-warning animate-pulse",
                status === "disconnected" && "bg-destructive",
                status === "failed" && "bg-destructive"
              )} />
              <span>{status}</span>
            </div>
          </TooltipTrigger>
          <TooltipContent side="bottom" align="end">
            WebSocket connection
          </TooltipContent>
        </Tooltip>

        <Separator orientation="vertical" className="h-6 mx-2" />

        {/* Theme toggle */}
        <Tooltip>
          <TooltipTrigger asChild>
            <Button variant="ghost" size="icon" onClick={cycleMode} aria-label="Cycle theme">
              {resolved === "dark" ? <Moon className="h-5 w-5" /> : <Sun className="h-5 w-5" />}
            </Button>
          </TooltipTrigger>
          <TooltipContent side="bottom">Cycle theme</TooltipContent>
        </Tooltip>

        {/* User menu */}
        <DropdownMenu>
          <DropdownMenuTrigger asChild>
            <Button variant="ghost" className="relative h-9 w-9 rounded-full">
              <div className="h-9 w-9 rounded-full bg-primary flex items-center justify-center">
                <User className="h-5 w-5 text-primary-foreground" />
              </div>
            </Button>
          </DropdownMenuTrigger>
          <DropdownMenuContent align="end" className="w-48">
            <div className="px-2 py-1 text-sm">
              <p className="font-medium">{user?.username ?? "admin"}</p>
              <p className="text-muted-foreground truncate">Administrator</p>
            </div>
            <DropdownMenuSeparator />
            <DropdownMenuItem onClick={() => navigate("/devices")}>
              <Settings className="mr-2 h-4 w-4" />
              Devices
            </DropdownMenuItem>
            <DropdownMenuItem onClick={() => navigate("/settings")}>
              <Monitor className="mr-2 h-4 w-4" />
              Settings
            </DropdownMenuItem>
            <DropdownMenuSeparator />
            <DropdownMenuItem onClick={logout} className="text-destructive focus:text-destructive">
              <LogOut className="mr-2 h-4 w-4" />
              Log out
            </DropdownMenuItem>
          </DropdownMenuContent>
        </DropdownMenu>
      </header>
    </TooltipProvider>
  );
}

function getPageTitle(pathname: string): string {
  if (pathname === "/") return "Overview";
  if (pathname.startsWith("/rooms")) return "Rooms";
  if (pathname.startsWith("/scenes")) return "Scenes";
  if (pathname.startsWith("/floorplans")) return "Floor Plans";
  if (pathname.startsWith("/devices")) return "Devices";
  if (pathname.startsWith("/settings")) return "Settings";
  return "Lumen";
}