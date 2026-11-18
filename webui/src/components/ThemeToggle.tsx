// Theme toggle: cycles light → dark → system (DESIGN §4.1, §7.11).
import { Monitor, Moon, Sun } from "lucide-react";

import { Button } from "@/components/ui/button";
import { Tooltip, TooltipContent, TooltipTrigger } from "@/components/ui/tooltip";
import { useTheme } from "@/hooks/useTheme";
import type { ThemeMode } from "@/lib/theme";

const MODE_META: Record<ThemeMode, { label: string; Icon: typeof Sun }> = {
  light: { label: "Light", Icon: Sun },
  dark: { label: "Dark", Icon: Moon },
  system: { label: "System", Icon: Monitor },
};

export function ThemeToggle() {
  const { mode, cycleMode } = useTheme();
  const { label, Icon } = MODE_META[mode];

  return (
    <Tooltip>
      <TooltipTrigger asChild>
        <Button
          variant="ghost"
          size="sm"
          onClick={cycleMode}
          aria-label={`Theme: ${label}. Click to change.`}
        >
          <Icon className="size-4" aria-hidden="true" />
          <span className="text-xs">{label}</span>
        </Button>
      </TooltipTrigger>
      <TooltipContent>Switch theme: light → dark → system</TooltipContent>
    </Tooltip>
  );
}
