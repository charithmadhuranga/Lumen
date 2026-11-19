"use client";

import * as React from "react";
import { useForm } from "react-hook-form";
import { zodResolver } from "@hookform/resolvers/zod";
import { z } from "zod";
import { cn } from "@/lib/utils";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Card, CardContent, CardDescription, CardFooter, CardHeader, CardTitle } from "@/components/ui/card";
import { Label } from "@/components/ui/label";
import { Separator } from "@/components/ui/separator";
import { Alert, AlertDescription } from "@/components/ui/alert";
import { Loader2, Lock, AlertCircle, CheckCircle } from "lucide-react";
import { useAuth } from "@/hooks/useAuth";
import { useNavigate } from "react-router-dom";

/* ================================================================
   Validation Schema
   ================================================================ */
const passwordSchema = z
  .object({
    currentPassword: z.string().min(1, "Current password is required"),
    newPassword: z.string().min(10, "Password must be at least 10 characters"),
    confirmPassword: z.string().min(1, "Please confirm your password"),
  })
  .refine((data) => data.newPassword !== data.currentPassword, {
    message: "New password must be different from current password",
    path: ["newPassword"],
  })
  .refine((data) => data.newPassword === data.confirmPassword, {
    message: "Passwords do not match",
    path: ["confirmPassword"],
  });

type PasswordForm = z.infer<typeof passwordSchema>;

/* ================================================================
   Password Strength Helper
   ================================================================ */
function getPasswordStrength(password: string): { score: number; label: string; color: string } {
  let score = 0;
  if (password.length >= 10) score++;
  if (password.length >= 14) score++;
  if (/[A-Z]/.test(password)) score++;
  if (/[a-z]/.test(password)) score++;
  if (/[0-9]/.test(password)) score++;
  if (/[^A-Za-z0-9]/.test(password)) score++;

  if (score <= 2) return { score, label: "Weak", color: "text-destructive" };
  if (score <= 4) return { score, label: "Fair", color: "text-warning" };
  return { score, label: "Strong", color: "text-success" };
}

/* ================================================================
   Setup Password Screen
   ================================================================ */
export function SetupPassword() {
  const navigate = useNavigate();
  const { changePassword, user, loading: authLoading } = useAuth();

  const [localLoading, setLocalLoading] = React.useState(false);
  const [error, setError] = React.useState<string | null>(null);
  const [strength, setStrength] = React.useState<{ score: number; label: string; color: string }>({ score: 0, label: "", color: "" });

  const form = useForm<PasswordForm>({
    resolver: zodResolver(passwordSchema),
    defaultValues: { currentPassword: "admin", newPassword: "", confirmPassword: "" },
    mode: "onChange",
  });

  // Watch new password for strength indicator
  const newPassword = form.watch("newPassword");
  React.useEffect(() => {
    setStrength(getPasswordStrength(newPassword));
  }, [newPassword]);

  const onSubmit = async (data: PasswordForm) => {
    setLocalLoading(true);
    setError(null);
    try {
      await changePassword(data.currentPassword, data.newPassword);
      navigate("/");
    } catch (e) {
      setError(e instanceof Error ? e.message : "Password change failed");
    } finally {
      setLocalLoading(false);
    }
  };

  return (
    <div className="flex min-h-screen items-center justify-center bg-background p-4">
      <Card className="w-full max-w-md">
        <CardHeader className="text-center">
          <div className="mx-auto mb-4 flex h-12 w-12 items-center justify-center rounded-full bg-primary">
            <Lock className="h-7 w-7 text-primary-foreground" />
          </div>
          <CardTitle className="text-2xl">Change Default Password</CardTitle>
          <CardDescription>
            The default password must be changed before continuing.
            Your new password must be at least 10 characters and different from "admin".
          </CardDescription>
        </CardHeader>

        <CardContent>
          {error && (
            <Alert variant="destructive" className="mb-4">
              <AlertCircle className="h-4 w-4" />
              <AlertDescription>{error}</AlertDescription>
            </Alert>
          )}

          <form onSubmit={form.handleSubmit(onSubmit)} className="space-y-4">
            <div className="space-y-2">
              <Label htmlFor="currentPassword">Current Password</Label>
              <Input
                id="currentPassword"
                type="password"
                autoComplete="current-password"
                placeholder="admin"
                {...form.register("currentPassword")}
                disabled={localLoading || authLoading}
                aria-invalid={!!form.formState.errors.currentPassword}
              />
              {form.formState.errors.currentPassword && (
                <p className="text-sm text-destructive" role="alert">
                  {form.formState.errors.currentPassword.message}
                </p>
              )}
            </div>

            <div className="space-y-2">
              <Label htmlFor="newPassword">New Password</Label>
              <Input
                id="newPassword"
                type="password"
                autoComplete="new-password"
                placeholder="At least 10 characters"
                {...form.register("newPassword")}
                disabled={localLoading || authLoading}
                aria-invalid={!!form.formState.errors.newPassword}
              />
              {form.formState.errors.newPassword && (
                <p className="text-sm text-destructive" role="alert">
                  {form.formState.errors.newPassword.message}
                </p>
              )}

              {/* Strength indicator */}
              {newPassword && (
                <div className="space-y-1">
                  <div className="h-1.5 w-full rounded-full bg-muted overflow-hidden">
                    <div
                      className={cn(
                        "h-full rounded-full transition-all duration-300",
                        strength.color.replace("text-", "bg-")
                      )}
                      style={{ width: `${(strength.score / 6) * 100}%` }}
                    />
                  </div>
                  <p className={cn("text-xs", strength.color)}>
                    {strength.label} ({strength.score}/6)
                  </p>
                </div>
              )}
            </div>

            <div className="space-y-2">
              <Label htmlFor="confirmPassword">Confirm New Password</Label>
              <Input
                id="confirmPassword"
                type="password"
                autoComplete="new-password"
                placeholder="Confirm new password"
                {...form.register("confirmPassword")}
                disabled={localLoading || authLoading}
                aria-invalid={!!form.formState.errors.confirmPassword}
              />
              {form.formState.errors.confirmPassword && (
                <p className="text-sm text-destructive" role="alert">
                  {form.formState.errors.confirmPassword.message}
                </p>
              )}
            </div>

            <Button
              type="submit"
              className="w-full"
              size="lg"
              disabled={localLoading || authLoading}
            >
              {localLoading || authLoading ? (
                <>
                  <Loader2 className="mr-2 h-4 w-4 animate-spin" />
                  Updating...
                </>
              ) : (
                "Update Password"
              )}
            </Button>
          </form>
        </CardContent>

        <CardFooter className="flex flex-col items-center gap-2 text-sm text-muted-foreground">
          <Separator className="w-full" />
          <p>Use a strong, unique password. Minimum 10 characters.</p>
        </CardFooter>
      </Card>
    </div>
  );
}