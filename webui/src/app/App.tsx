"use client";

import * as React from "react";
import { BrowserRouter, Routes, Route, Navigate, Outlet } from "react-router-dom";
import { QueryClient, QueryClientProvider } from "@tanstack/react-query";
import { MotionConfig, AnimatePresence } from "motion/react";
import { Toaster } from "@/components/ui/sonner";
import { ThemeProvider } from "@/hooks/useTheme";
import { Providers } from "./providers";
import { Sidebar } from "@/components/layout/Sidebar";
import { TopBar } from "@/components/layout/TopBar";
import { MobileDrawer } from "@/components/layout/MobileDrawer";
import { Login } from "@/screens/Login";
import { SetupPassword } from "@/screens/SetupPassword";
import { Overview } from "@/screens/Overview";
import { Rooms } from "@/screens/Rooms";
import { Scenes } from "@/screens/Scenes";
import { FloorPlans } from "@/screens/FloorPlans";
import { Devices } from "@/screens/Devices";
import { Settings } from "@/screens/Settings";
import { ConnectionBanner } from "@/components/ui/ConnectionBanner";
import { useAuth } from "@/hooks/useAuth";
import { useEffect } from "react";

/* ================================================================
   Query Client
   ================================================================ */
const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      staleTime: 30_000,
      refetchOnWindowFocus: false,
      retry: (failureCount, error: any) => {
        if (error?.status === 401) return false;
        return failureCount < 3;
      },
    },
  },
});

/* ================================================================
   Auth Guard
   ================================================================ */
function AuthGuard({ children }: { children: React.ReactNode }) {
  const { user, loading } = useAuth();

  useEffect(() => {
    // Check session on mount
  }, []);

  if (loading) {
    return (
      <div className="flex h-screen items-center justify-center">
        <div className="animate-spin rounded-full h-8 w-8 border-b-2 border-primary" />
      </div>
    );
  }

  if (!user) {
    return <Navigate to="/login" replace />;
  }

  return <>{children}</>;
}

/* ================================================================
   Setup Password Guard
   ================================================================ */
function SetupPasswordGuard({ children }: { children: React.ReactNode }) {
  const { user, loading } = useAuth();

  if (loading) {
    return (
      <div className="flex h-screen items-center justify-center">
        <div className="animate-spin rounded-full h-8 w-8 border-b-2 border-primary" />
      </div>
    );
  }

  if (user?.forcePasswordChange) {
    return <Navigate to="/setup/password" replace />;
  }

  return <>{children}</>;
}

/* ================================================================
   Main Layout
   ================================================================ */
function MainLayout() {
  return (
    <div className="flex h-screen bg-background">
      <Sidebar />
      <MobileDrawer />
      <div className="flex-1 flex flex-col overflow-hidden lg:pl-64">
        <TopBar />
        <ConnectionBanner />
        <main className="flex-1 overflow-auto p-4 lg:p-6">
          <AnimatePresence mode="wait">
            <Outlet />
          </AnimatePresence>
        </main>
      </div>
    </div>
  );
}

/* ================================================================
   Public Layout (Login, Setup)
   ================================================================ */
function PublicLayout() {
  return (
    <div className="flex min-h-screen bg-background">
      <main className="flex-1 flex items-center justify-center p-4">
        <AnimatePresence mode="wait">
          <Outlet />
        </AnimatePresence>
      </main>
    </div>
  );
}

/* ================================================================
   App
   ================================================================ */
export default function App() {
  return (
    <QueryClientProvider client={queryClient}>
      <Providers>
        <MotionConfig reducedMotion="user">
          <BrowserRouter>
            <Toaster position="top-center" closeButton richColors />
            <Routes>
              {/* Public routes */}
              <Route element={<PublicLayout />}>
                <Route path="/login" element={<Login />} />
                <Route path="/setup/password" element={<SetupPassword />} />
              </Route>

              {/* Protected routes */}
              <Route element={<AuthGuard><SetupPasswordGuard><MainLayout /></SetupPasswordGuard></AuthGuard>}>
                <Route path="/" element={<Overview />} />
                <Route path="/rooms" element={<Rooms />} />
                <Route path="/scenes" element={<Scenes />} />
                <Route path="/floorplans" element={<FloorPlans />} />
                <Route path="/devices" element={<Devices />} />
                <Route path="/settings" element={<Settings />} />
              </Route>

              {/* Fallback */}
              <Route path="*" element={<Navigate to="/" replace />} />
            </Routes>
          </BrowserRouter>
        </MotionConfig>
      </Providers>
    </QueryClientProvider>
  );
}