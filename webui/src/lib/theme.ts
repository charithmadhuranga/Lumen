// Theme mode logic — single source of truth for light/dark/system.
//
// Phase 0 owner: this module + localStorage (key `lumen-theme`, also used by
// the pre-paint inline script in index.html). DESIGN §2.1 moves theme state
// into the Zustand uiStore in Phase 6; keep the storage key unchanged.

export type ThemeMode = "light" | "dark" | "system";
export type ResolvedTheme = "light" | "dark";

export const THEME_STORAGE_KEY = "lumen-theme";

export const THEME_ORDER: readonly ThemeMode[] = ["light", "dark", "system"];

/** Resolve a mode against the OS preference. */
export function resolveTheme(mode: ThemeMode, prefersDark: boolean): ResolvedTheme {
  if (mode === "system") {
    return prefersDark ? "dark" : "light";
  }
  return mode;
}

/** Light → dark → system → light (toggle cycle order). */
export function nextTheme(mode: ThemeMode): ThemeMode {
  const index = THEME_ORDER.indexOf(mode);
  return THEME_ORDER[(index + 1) % THEME_ORDER.length] ?? "system";
}

/** Current OS dark preference; safe when matchMedia is unavailable. */
export function prefersDark(): boolean {
  if (typeof window === "undefined" || typeof window.matchMedia !== "function") {
    return false;
  }
  return window.matchMedia("(prefers-color-scheme: dark)").matches;
}

/** Read the persisted mode; defaults to `system` (DESIGN §4.1). */
export function loadTheme(): ThemeMode {
  if (typeof window === "undefined") {
    return "system";
  }
  try {
    const raw = window.localStorage.getItem(THEME_STORAGE_KEY);
    if (raw === "light" || raw === "dark" || raw === "system") {
      return raw;
    }
  } catch {
    // private mode / storage disabled — fall through to default
  }
  return "system";
}

/** Persist the mode (best effort; storage may be unavailable). */
export function saveTheme(mode: ThemeMode): void {
  if (typeof window === "undefined") {
    return;
  }
  try {
    window.localStorage.setItem(THEME_STORAGE_KEY, mode);
  } catch {
    // ignore — theme still applies for this session
  }
}

/**
 * Apply the resolved theme by toggling the `dark` class on <html>
 * (DESIGN §4.1). Idempotent.
 */
export function applyTheme(resolved: ResolvedTheme): void {
  if (typeof document === "undefined") {
    return;
  }
  document.documentElement.classList.toggle("dark", resolved === "dark");
}
