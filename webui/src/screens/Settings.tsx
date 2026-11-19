"use client";

import * as React from "react";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardHeader, CardTitle, CardDescription } from "@/components/ui/card";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Separator } from "@/components/ui/separator";
import { Alert, AlertDescription } from "@/components/ui/alert";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { Switch } from "@/components/ui/switch";
import { Save, Wifi, Cpu, Shield, Key, Monitor, Loader2, Download } from "lucide-react";
import { useForm } from "react-hook-form";
import { zodResolver } from "@hookform/resolvers/zod";
import { z } from "zod";

const passwordSchema = z.object({
  currentPassword: z.string().min(1),
  newPassword: z.string().min(10),
  confirmPassword: z.string().min(1),
}).refine((data) => data.newPassword === data.confirmPassword, {
  message: "Passwords do not match",
  path: ["confirmPassword"],
});

type PasswordForm = z.infer<typeof passwordSchema>;

export function Settings() {
  const [activeTab, setActiveTab] = React.useState("general");
  const [saving, setSaving] = React.useState(false);

  const passwordForm = useForm<PasswordForm>({
    resolver: zodResolver(passwordSchema),
    defaultValues: { currentPassword: "", newPassword: "", confirmPassword: "" },
  });

  const handlePasswordSubmit = async () => {
    setSaving(true);
    try {
      // Call change password API
      setSaving(false);
    } catch {
      setSaving(false);
    }
  };

  return (
    <div className="space-y-6 max-w-3xl">
      <div>
        <h1 className="text-3xl font-bold tracking-tight">Settings</h1>
        <p className="text-muted-foreground">Manage your Lumen controller settings</p>
      </div>

      <Tabs value={activeTab} onValueChange={setActiveTab} className="w-full">
        <TabsList className="grid w-full grid-cols-4">
          <TabsTrigger value="general">General</TabsTrigger>
          <TabsTrigger value="network">Network</TabsTrigger>
          <TabsTrigger value="security">Security</TabsTrigger>
          <TabsTrigger value="about">About</TabsTrigger>
        </TabsList>

        {/* General Tab */}
        <TabsContent value="general" className="space-y-6">
          <Card>
            <CardHeader>
              <CardTitle>General Settings</CardTitle>
              <CardDescription>Configure general controller behavior</CardDescription>
            </CardHeader>
            <CardContent className="space-y-4">
              <div className="flex items-center justify-between">
                <div>
                  <Label>Device Name</Label>
                  <p className="text-sm text-muted-foreground">Name shown in Home Assistant and mDNS</p>
                </div>
                <Input defaultValue="lumen" disabled />
              </div>

              <div className="flex items-center justify-between">
                <div>
                  <Label>Timezone</Label>
                  <p className="text-sm text-muted-foreground">Controller timezone for scheduling</p>
                </div>
                <select className="w-48 rounded border p-2" disabled>
                  <option>UTC</option>
                  <option>America/New_York</option>
                  <option>Europe/London</option>
                </select>
              </div>

              <div className="flex items-center justify-between">
                <div>
                  <Label>Language</Label>
                </div>
                <select className="w-48 rounded border p-2" disabled>
                  <option>English</option>
                </select>
              </div>

              <Button onClick={() => {}} disabled>
                <Save className="mr-2 h-4 w-4" />
                Save General Settings
              </Button>
            </CardContent>
          </Card>
        </TabsContent>

        {/* Network Tab */}
        <TabsContent value="network" className="space-y-6">
          <Card>
            <CardHeader>
              <Wifi className="mr-2 h-5 w-5" />
              <CardTitle>Network Configuration</CardTitle>
              <CardDescription>Wi-Fi and Ethernet settings</CardDescription>
            </CardHeader>
            <CardContent className="space-y-4">
              <Alert variant="info">
                <AlertDescription>
                  Network configuration requires a controller reboot to take effect.
                  Changes made here will be applied on next restart.
                </AlertDescription>
              </Alert>

              <div className="space-y-4">
                <div className="grid gap-4 md:grid-cols-2">
                  <div className="space-y-2">
                    <Label>Wi-Fi Mode</Label>
                    <select className="rounded border p-2" disabled>
                      <option>Station (STA)</option>
                      <option>Access Point (AP)</option>
                      <option>STA + AP Fallback</option>
                    </select>
                  </div>
                  <div className="space-y-2">
                    <Label>SSID</Label>
                    <Input placeholder="MyNetwork" disabled />
                  </div>
                  <div className="space-y-2">
                    <Label>Password</Label>
                    <Input type="password" placeholder="••••••••" disabled />
                  </div>
                </div>

                <Separator />

                <h4 className="font-medium">Ethernet</h4>
                <div className="grid gap-4 md:grid-cols-2">
                  <div className="space-y-2">
                    <Label>IP Mode</Label>
                    <select className="rounded border p-2" disabled>
                      <option>DHCP</option>
                      <option>Static</option>
                    </select>
                  </div>
                  <div className="space-y-2">
                    <Label>Static IP</Label>
                    <Input placeholder="192.168.1.100" disabled />
                  </div>
                </div>

                <Button disabled>
                  <Save className="mr-2 h-4 w-4" />
                  Save Network Settings
                </Button>
              </div>
            </CardContent>
          </Card>
        </TabsContent>

        {/* Security Tab */}
        <TabsContent value="security" className="space-y-6">
          <Card>
            <CardHeader>
              <Shield className="mr-2 h-5 w-5" />
              <CardTitle>Security</CardTitle>
              <CardDescription>Password and access control</CardDescription>
            </CardHeader>
            <CardContent className="space-y-6">
              <form onSubmit={passwordForm.handleSubmit(handlePasswordSubmit)} className="space-y-4">
                <div className="space-y-2">
                  <Label htmlFor="currentPassword">Current Password</Label>
                  <Input
                    id="currentPassword"
                    type="password"
                    {...passwordForm.register("currentPassword")}
                    placeholder="••••••••"
                    disabled={saving}
                  />
                  {passwordForm.formState.errors.currentPassword && (
                    <p className="text-sm text-destructive">{passwordForm.formState.errors.currentPassword.message}</p>
                  )}
                </div>

                <div className="space-y-2">
                  <Label htmlFor="newPassword">New Password</Label>
                  <Input
                    id="newPassword"
                    type="password"
                    {...passwordForm.register("newPassword")}
                    placeholder="At least 10 characters"
                    disabled={saving}
                  />
                  {passwordForm.formState.errors.newPassword && (
                    <p className="text-sm text-destructive">{passwordForm.formState.errors.newPassword.message}</p>
                  )}
                </div>

                <div className="space-y-2">
                  <Label htmlFor="confirmPassword">Confirm New Password</Label>
                  <Input
                    id="confirmPassword"
                    type="password"
                    {...passwordForm.register("confirmPassword")}
                    placeholder="Confirm new password"
                    disabled={saving}
                  />
                  {passwordForm.formState.errors.confirmPassword && (
                    <p className="text-sm text-destructive">{passwordForm.formState.errors.confirmPassword.message}</p>
                  )}
                </div>

                <Button type="submit" disabled={saving}>
                  {saving ? <Loader2 className="mr-2 h-4 w-4 animate-spin" /> : <> <Key className="mr-2 h-4 w-4" /> Change Password </>}
                </Button>
              </form>

              <Separator />

              <div className="space-y-4">
                <h4 className="font-medium">Session Management</h4>
                <div className="flex items-center justify-between">
                  <div>
                    <p className="font-medium">Active Sessions</p>
                    <p className="text-sm text-muted-foreground">Manage logged-in sessions</p>
                  </div>
                  <Button variant="outline" size="sm" disabled>
                    View Sessions
                  </Button>
                </div>

                <div className="flex items-center justify-between">
                  <div>
                    <p className="font-medium">Force Password Change</p>
                    <p className="text-sm text-muted-foreground">Require password change on next login</p>
                  </div>
                  <Switch defaultChecked={false} disabled />
                </div>
              </div>
            </CardContent>
          </Card>
        </TabsContent>

        {/* About Tab */}
        <TabsContent value="about" className="space-y-6">
          <Card>
            <CardHeader>
              <Monitor className="mr-2 h-5 w-5" />
              <CardTitle>System Information</CardTitle>
              <CardDescription>Firmware and hardware details</CardDescription>
            </CardHeader>
            <CardContent className="space-y-4">
              <dl className="grid gap-4 md:grid-cols-2">
                <div className="space-y-1">
                  <dt className="text-sm text-muted-foreground">Firmware Version</dt>
                  <dd className="font-mono text-lg">0.1.0-dev</dd>
                </div>
                <div className="space-y-1">
                  <dt className="text-sm text-muted-foreground">ESP-IDF Version</dt>
                  <dd className="font-mono">6.1.0</dd>
                </div>
                <div className="space-y-1">
                  <dt className="text-sm text-muted-foreground">Chip Model</dt>
                  <dd>ESP32-D0WD-V3</dd>
                </div>
                <div className="space-y-1">
                  <dt className="text-sm text-muted-foreground">Chip Revision</dt>
                  <dd>v3.0</dd>
                </div>
                <div className="space-y-1">
                  <dt className="text-sm text-muted-foreground">CPU Frequency</dt>
                  <dd>240 MHz</dd>
                </div>
                <div className="space-y-1">
                  <dt className="text-sm text-muted-foreground">Flash Size</dt>
                  <dd>4 MB</dd>
                </div>
                <div className="space-y-1">
                  <dt className="text-sm text-muted-foreground">Free Heap</dt>
                  <dd className="font-mono">~300 KB</dd>
                </div>
                <div className="space-y-1">
                  <dt className="text-sm text-muted-foreground">Uptime</dt>
                  <dd id="uptime">Loading...</dd>
                </div>
              </dl>

              <Separator />

              <div className="flex gap-2">
                <Button variant="outline" disabled>
                  <Cpu className="mr-2 h-4 w-4" />
                  View Diagnostics
                </Button>
                <Button variant="outline" disabled>
                  <Download className="mr-2 h-4 w-4" />
                  Export Config
                </Button>
              </div>
            </CardContent>
          </Card>
        </TabsContent>
      </Tabs>
    </div>
  );
}