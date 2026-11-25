// Routes (DESIGN §6.2): lazy screens, auth gate, forced password change gate,
// WebSocket lifecycle tied to a fully authenticated session.
import * as React from "react";
import { BrowserRouter, Navigate, Route, Routes, useLocation } from "react-router-dom";
import { QueryClientProvider, useQueryClient } from "@tanstack/react-query";
import { Loader2 } from "lucide-react";

import { AppShell } from "@/components/layout/AppShell";
import { useAuth } from "@/hooks/useAuth";
import { createQueryClient } from "@/services/queries";
import { LumenSocket } from "@/services/ws";
import { Providers } from "./providers";

const Login = React.lazy(() => import("@/screens/Login"));
const SetupPassword = React.lazy(() => import("@/screens/SetupPassword"));
const Overview = React.lazy(() => import("@/screens/Overview"));
const Rooms = React.lazy(() => import("@/screens/Rooms"));
const Scenes = React.lazy(() => import("@/screens/Scenes"));
const Devices = React.lazy(() => import("@/screens/Devices"));
const Hardware = React.lazy(() => import("@/screens/Hardware"));
const NetworkScreen = React.lazy(() => import("@/screens/Network"));
const Settings = React.lazy(() => import("@/screens/Settings"));
const FloorPlans = React.lazy(() => import("@/screens/FloorPlans"));
const Automations = React.lazy(() => import("@/screens/Automations"));
const Integrations = React.lazy(() => import("@/screens/Integrations"));

function Spinner() {
  return (
    <div className="grid min-h-[50vh] place-items-center" role="status" aria-label="Loading">
      <Loader2 className="size-6 animate-spin text-muted-foreground" />
    </div>
  );
}

class RouteErrorBoundary extends React.Component<{ children: React.ReactNode }, { error: Error | null }> {
  override state = { error: null as Error | null };
  static getDerivedStateFromError(error: Error) {
    return { error };
  }
  override render() {
    if (this.state.error) {
      return (
        <div className="mx-auto max-w-md py-16 text-center">
          <p className="font-medium">This screen failed to load.</p>
          <p className="mt-1 text-sm text-muted-foreground">{this.state.error.message}</p>
          <button className="mt-4 text-sm text-link underline" onClick={() => window.location.reload()}>
            Reload
          </button>
        </div>
      );
    }
    return this.props.children;
  }
}

function Gate({ children }: { children: React.ReactNode }) {
  const { status, user } = useAuth();
  const location = useLocation();
  const qc = useQueryClient();
  const socket = React.useRef<LumenSocket | null>(null);
  const ready = status === "signed-in" && user?.forcePasswordChange === false;

  React.useEffect(() => {
    if (ready) {
      socket.current ??= new LumenSocket(qc);
      socket.current.start();
    } else {
      socket.current?.stop();
      if (status === "signed-out") qc.clear();
    }
    return () => socket.current?.stop();
  }, [ready, status, qc]);

  if (status === "unknown") return <Spinner />;
  if (status === "signed-out") return <Navigate to="/login" replace state={{ from: location.pathname }} />;
  if (user?.forcePasswordChange) return <Navigate to="/setup/password" replace />;
  return <>{children}</>;
}

function AppRoutes() {
  const checkSession = useAuth((s) => s.checkSession);
  React.useEffect(() => {
    void checkSession();
  }, [checkSession]);

  return (
    <React.Suspense fallback={<Spinner />}>
      <Routes>
        <Route path="/login" element={<Login />} />
        <Route path="/setup/password" element={<SetupPassword />} />
        <Route
          element={
            <Gate>
              <AppShell />
            </Gate>
          }
        >
          {(
            [
              ["/", Overview],
              ["/rooms/*", Rooms],
              ["/scenes", Scenes],
              ["/devices", Devices],
              ["/hardware/*", Hardware],
              ["/network", NetworkScreen],
              ["/settings", Settings],
              ["/floorplans/*", FloorPlans],
              ["/automations/*", Automations],
              ["/integrations/*", Integrations],
            ] as const
          ).map(([path, Screen]) => (
            <Route
              key={path}
              path={path}
              element={
                <RouteErrorBoundary>
                  <React.Suspense fallback={<Spinner />}>
                    <Screen />
                  </React.Suspense>
                </RouteErrorBoundary>
              }
            />
          ))}
        </Route>
        <Route path="*" element={<Navigate to="/" replace />} />
      </Routes>
    </React.Suspense>
  );
}

const queryClient = createQueryClient();

export default function App() {
  return (
    <QueryClientProvider client={queryClient}>
      <Providers>
        <BrowserRouter>
          <AppRoutes />
        </BrowserRouter>
      </Providers>
    </QueryClientProvider>
  );
}
