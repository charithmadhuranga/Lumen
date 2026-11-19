// Auth state management — session, user, forced password change.
// DESIGN §13: HTTP-only cookies only, never localStorage for tokens.
import { create } from "zustand";
import { persist, createJSONStorage } from "zustand/middleware";

import { authApi, ApiError } from "@/services/api";

/* ================================================================
   Types
   ================================================================ */
interface User {
  username: string;
  forcePasswordChange: boolean;
}

interface AuthState {
  user: User | null;
  loading: boolean;
  error: string | null;

  // Actions
  login: (username: string, password: string) => Promise<void>;
  logout: () => Promise<void>;
  checkSession: () => Promise<void>;
  changePassword: (current: string, next: string) => Promise<void>;
  clearError: () => void;
}

/* ================================================================
   Store
   ================================================================ */
export const useAuth = create<AuthState>()(
  persist(
    (set, get) => ({
      user: null,
      loading: true,
      error: null,

      login: async (username: string, password: string) => {
        set({ loading: true, error: null });
        try {
          const data = await authApi.login(username, password);
          set({
            user: { username: data.username, forcePasswordChange: data.forcePasswordChange },
            loading: false,
            error: null,
          });
        } catch (e) {
          const message = e instanceof ApiError ? e.message : "Login failed";
          set({ loading: false, error: message, user: null });
          throw e;
        }
      },

      logout: async () => {
        set({ loading: true });
        try {
          await authApi.logout();
        } catch {
          // Ignore logout errors
        } finally {
          set({ user: null, loading: false });
        }
      },

      checkSession: async () => {
        set({ loading: true });
        try {
          const data = await authApi.session();
          set({
            user: { username: data.username, forcePasswordChange: data.forcePasswordChange },
            loading: false,
          });
        } catch {
          set({ user: null, loading: false });
        }
      },

      changePassword: async (current: string, next: string) => {
        set({ loading: true, error: null });
        try {
          await authApi.changePassword(current, next);
          const user = get().user;
          if (user) {
            set({ user: { ...user, forcePasswordChange: false }, loading: false });
          } else {
            set({ loading: false });
          }
        } catch (e) {
          const message = e instanceof ApiError ? e.message : "Password change failed";
          set({ loading: false, error: message });
          throw e;
        }
      },

      clearError: () => set({ error: null }),
    }),
    {
      name: "lumen-auth",
      storage: createJSONStorage(() => sessionStorage),
      partialize: (state) => ({ user: state.user }), // Only persist user, not loading/error
    }
  )
);

/* ================================================================
   Initialization helper
   ================================================================ */
export function initAuth() {
  useAuth.getState().checkSession();
}