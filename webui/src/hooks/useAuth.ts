// Auth state (DESIGN §13): the session token lives only in an HttpOnly
// cookie; the UI keeps the username, the forced-password-change flag and
// the CSRF token in memory. Nothing auth-related is written to storage.
import { create } from "zustand";

import { api, ApiError, setCsrfToken, setUnauthorizedHandler } from "@/services/api";
import type { SessionInfo } from "@/schemas";

interface AuthState {
  status: "unknown" | "signed-out" | "signed-in";
  user: { username: string; forcePasswordChange: boolean } | null;
  checkSession: () => Promise<void>;
  login: (username: string, password: string) => Promise<void>;
  logout: () => Promise<void>;
  changePassword: (current: string, next: string) => Promise<void>;
  expire: () => void;
}

function applySession(s: SessionInfo) {
  setCsrfToken(s.csrfToken);
  return { status: "signed-in" as const, user: { username: s.username, forcePasswordChange: s.forcePasswordChange } };
}

export const useAuth = create<AuthState>()((set, get) => ({
  status: "unknown",
  user: null,

  checkSession: async () => {
    try {
      set(applySession(await api.session()));
    } catch {
      setCsrfToken(null);
      set({ status: "signed-out", user: null });
    }
  },

  login: async (username, password) => {
    const s = await api.login(username, password);
    set(applySession(s));
  },

  logout: async () => {
    try {
      await api.logout();
    } catch {
      // the session may already be gone; sign out locally regardless
    }
    setCsrfToken(null);
    set({ status: "signed-out", user: null });
  },

  changePassword: async (current, next) => {
    const s = await api.changePassword(current, next);
    if (s) {
      set(applySession(s)); // firmware issues a fresh session after the change
    } else {
      await get().checkSession();
    }
  },

  expire: () => {
    setCsrfToken(null);
    set({ status: "signed-out", user: null });
  },
}));

setUnauthorizedHandler(() => useAuth.getState().expire());

export function isApiError(e: unknown): e is ApiError {
  return e instanceof ApiError;
}

export function errorMessage(e: unknown, fallback = "Something went wrong"): string {
  return e instanceof ApiError ? e.message : e instanceof Error ? e.message : fallback;
}
