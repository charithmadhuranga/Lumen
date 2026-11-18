// App providers (DESIGN §3): Motion, theme, tooltips, toasters.
import type { ReactNode } from "react";
import { MotionConfig } from "motion/react";

import { Toaster } from "@/components/ui/sonner";
import { TooltipProvider } from "@/components/ui/tooltip";
import { ThemeProvider } from "@/hooks/useTheme";

export function Providers({ children }: { children: ReactNode }) {
  return (
    <ThemeProvider>
      {/* Respect prefers-reduced-motion app-wide (AGENTS §9). */}
      <MotionConfig reducedMotion="user">
        <TooltipProvider delayDuration={300}>{children}</TooltipProvider>
        <Toaster position="top-center" closeButton />
      </MotionConfig>
    </ThemeProvider>
  );
}
