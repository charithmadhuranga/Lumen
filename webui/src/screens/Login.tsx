// Sign in (DESIGN §7.1): never prefilled; rate-limit errors show a countdown.
import * as React from "react";
import { Navigate, useLocation, useNavigate } from "react-router-dom";
import { Lightbulb, Loader2 } from "lucide-react";

import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Field } from "@/components/common";
import { errorMessage, isApiError, useAuth } from "@/hooks/useAuth";

export default function Login() {
  const { status, user, login } = useAuth();
  const navigate = useNavigate();
  const location = useLocation();
  const [username, setUsername] = React.useState("");
  const [password, setPassword] = React.useState("");
  const [error, setError] = React.useState<string | null>(null);
  const [pending, setPending] = React.useState(false);
  const [retryIn, setRetryIn] = React.useState(0);

  React.useEffect(() => {
    if (retryIn <= 0) return;
    const t = setTimeout(() => setRetryIn((s) => s - 1), 1000);
    return () => clearTimeout(t);
  }, [retryIn]);

  if (status === "signed-in" && user) {
    return <Navigate to={user.forcePasswordChange ? "/setup/password" : "/"} replace />;
  }

  const submit = async (e: React.FormEvent) => {
    e.preventDefault();
    setError(null);
    setPending(true);
    try {
      await login(username.trim(), password);
      const u = useAuth.getState().user;
      const from = (location.state as { from?: string } | null)?.from;
      navigate(u?.forcePasswordChange ? "/setup/password" : from && from !== "/login" ? from : "/", { replace: true });
    } catch (err) {
      setPassword("");
      if (isApiError(err) && err.status === 429) setRetryIn(err.retryAfter ?? 60);
      setError(errorMessage(err, "Sign-in failed"));
    } finally {
      setPending(false);
    }
  };

  return (
    <div className="grid min-h-dvh place-items-center bg-background px-4">
      <form onSubmit={submit} className="w-full max-w-sm space-y-5 rounded-2xl border bg-card p-6 shadow-sm" noValidate>
        <div className="flex flex-col items-center gap-2 text-center">
          <span className="grid size-12 place-items-center rounded-xl bg-primary text-primary-foreground">
            <Lightbulb className="size-6" aria-hidden="true" />
          </span>
          <h1 className="text-xl font-semibold">Sign in to Lumen</h1>
          <p className="text-sm text-muted-foreground">Lighting controller</p>
        </div>
        <Field label="Username" htmlFor="username">
          <Input id="username" autoComplete="username" value={username} onChange={(e) => setUsername(e.target.value)} required />
        </Field>
        <Field label="Password" htmlFor="password">
          <Input
            id="password"
            type="password"
            autoComplete="current-password"
            value={password}
            onChange={(e) => setPassword(e.target.value)}
            required
          />
        </Field>
        {error && (
          <p className="rounded-md bg-destructive/10 px-3 py-2 text-sm text-destructive" role="alert">
            {retryIn > 0 ? `Too many attempts. Try again in ${retryIn} s.` : error}
          </p>
        )}
        <Button type="submit" className="h-11 w-full" disabled={pending || retryIn > 0 || !username || !password}>
          {pending && <Loader2 className="size-4 animate-spin" />}
          Sign in
        </Button>
        <p className="text-center text-xs text-muted-foreground">
          First time? Sign in with <span className="font-mono">admin</span> / <span className="font-mono">admin</span>; you will be asked to choose a new password.
        </p>
      </form>
    </div>
  );
}
