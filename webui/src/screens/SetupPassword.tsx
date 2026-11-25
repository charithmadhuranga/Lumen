// Forced password change (DESIGN §7.1, SPEC §16.1). Blocks every other route
// until the default credential is replaced.
import * as React from "react";
import { Navigate, useNavigate } from "react-router-dom";
import { useForm } from "react-hook-form";
import { zodResolver } from "@hookform/resolvers/zod";
import { KeyRound, Loader2 } from "lucide-react";
import { toast } from "sonner";

import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Field } from "@/components/common";
import { errorMessage, useAuth } from "@/hooks/useAuth";
import { PasswordChangeForm } from "@/schemas";
import type { z } from "zod";

type Form = z.infer<typeof PasswordChangeForm>;

export function passwordStrength(p: string): { score: number; label: string } {
  let score = 0;
  if (p.length >= 10) score++;
  if (p.length >= 14) score++;
  if (/[a-z]/.test(p) && /[A-Z]/.test(p)) score++;
  if (/\d/.test(p)) score++;
  if (/[^A-Za-z0-9]/.test(p)) score++;
  const label = score <= 1 ? "Weak" : score <= 3 ? "Fair" : "Strong";
  return { score, label };
}

export function PasswordFields({ form, includeCurrent = true }: { form: ReturnType<typeof useForm<Form>>; includeCurrent?: boolean }) {
  const next = form.watch("newPassword") ?? "";
  const strength = passwordStrength(next);
  const e = form.formState.errors;
  return (
    <>
      {includeCurrent && (
        <Field label="Current password" htmlFor="current" error={e.currentPassword?.message}>
          <Input id="current" type="password" autoComplete="current-password" {...form.register("currentPassword")} />
        </Field>
      )}
      <Field label="New password" htmlFor="new" error={e.newPassword?.message} hint="At least 10 characters">
        <Input id="new" type="password" autoComplete="new-password" {...form.register("newPassword")} />
      </Field>
      {next && (
        <div aria-live="polite" className="space-y-1">
          <div className="flex h-1.5 gap-1" aria-hidden="true">
            {[0, 1, 2, 3, 4].map((i) => (
              <span
                key={i}
                className={`flex-1 rounded-full ${i < strength.score ? (strength.score <= 1 ? "bg-destructive" : strength.score <= 3 ? "bg-warning" : "bg-success") : "bg-muted"}`}
              />
            ))}
          </div>
          <p className="text-xs text-muted-foreground">Strength: {strength.label}</p>
        </div>
      )}
      <Field label="Confirm new password" htmlFor="confirm" error={e.confirmPassword?.message}>
        <Input id="confirm" type="password" autoComplete="new-password" {...form.register("confirmPassword")} />
      </Field>
    </>
  );
}

export default function SetupPassword() {
  const { status, user, changePassword } = useAuth();
  const navigate = useNavigate();
  const [error, setError] = React.useState<string | null>(null);
  const form = useForm<Form>({
    resolver: zodResolver(PasswordChangeForm),
    defaultValues: { currentPassword: "", newPassword: "", confirmPassword: "" },
  });

  if (status === "unknown") return null;
  if (status === "signed-out") return <Navigate to="/login" replace />;
  if (user && !user.forcePasswordChange) return <Navigate to="/" replace />;

  const submit = form.handleSubmit(async (v) => {
    setError(null);
    try {
      await changePassword(v.currentPassword, v.newPassword);
      toast.success("Password changed", { duration: 2500 });
      navigate("/", { replace: true });
    } catch (e) {
      setError(errorMessage(e));
    }
  });

  return (
    <div className="grid min-h-dvh place-items-center bg-background px-4">
      <form onSubmit={submit} className="w-full max-w-sm space-y-5 rounded-2xl border bg-card p-6 shadow-sm" noValidate>
        <div className="flex flex-col items-center gap-2 text-center">
          <span className="grid size-12 place-items-center rounded-xl bg-primary text-primary-foreground">
            <KeyRound className="size-6" aria-hidden="true" />
          </span>
          <h1 className="text-xl font-semibold">Choose a new password</h1>
          <p className="text-sm text-muted-foreground">
            The default password must be replaced before the controller can be used.
          </p>
        </div>
        <PasswordFields form={form} />
        {error && (
          <p className="rounded-md bg-destructive/10 px-3 py-2 text-sm text-destructive" role="alert">
            {error}
          </p>
        )}
        <Button type="submit" className="h-11 w-full" disabled={form.formState.isSubmitting}>
          {form.formState.isSubmitting && <Loader2 className="size-4 animate-spin" />}
          Save password
        </Button>
      </form>
    </div>
  );
}
