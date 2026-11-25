// App providers (DESIGN §3): theme and toaster. Animation uses CSS
// transitions only; `prefers-reduced-motion` is honoured in globals.css.
import type { ReactNode } from "react";

import { Toaster } from "@/components/ui/sonner";
import { ThemeProvider } from "@/hooks/useTheme";

export function Providers({ children }: { children: ReactNode }) {
  return (
    <ThemeProvider>
      {children}
      <Toaster position="bottom-right" closeButton />
    </ThemeProvider>
  );
}
