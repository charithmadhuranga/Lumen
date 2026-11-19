"use client";

import * as React from "react";
import { useQueryClient } from "@tanstack/react-query";
import { cn } from "@/lib/utils";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { Badge } from "@/components/ui/badge";
import { Separator } from "@/components/ui/separator";
import { ScrollArea } from "@/components/ui/scroll-area";
import {
  Lightbulb,
  Home,
  Grid,
  Video,
  Settings,
  Sun,
  Moon,
  Power,
  SlidersHorizontal,
  Zap,
  Loader2,
} from "lucide-react";
import { useLights, useRooms, useGroups, useScenes, useSystemInfo } from "@/services/queries";
import { useLightCommand } from "@/services/queries";
import { useLiveStore, useCommandThrottle } from "@/stores/liveStore";
import { useAuth } from "@/hooks/useAuth";
import { Slider } from "@/components/ui/slider";

/* ================================================================
   Light Card Component
   ================================================================ */
interface LightCardProps {
  light: any;
}

function LightCard({ light }: LightCardProps) {
  const queryClient = useQueryClient();
  const commandLight = useLightCommand();
  const { canSend, markSent } = useCommandThrottle(`light-${light.id}`);

  const isOn = light.state?.power ?? false;
  const brightness = light.state?.brightness ?? 0;
  const [localBrightness, setLocalBrightness] = React.useState(brightness);
  const [dragging, setDragging] = React.useState(false);

  // Sync local brightness with server state when not dragging
  React.useEffect(() => {
    if (!dragging) setLocalBrightness(brightness);
  }, [brightness, dragging]);

  const handlePowerToggle = () => {
    commandLight.mutate({
      id: light.id,
      command: { power: !isOn, transitionMs: 0 },
    });
  };

  const handleBrightnessChange = (value: number[]) => {
    const newBrightness = value[0];
    setLocalBrightness(newBrightness);
    if (!canSend) return;
    markSent();
    commandLight.mutate({
      id: light.id,
      command: { brightness: newBrightness, power: true, transitionMs: 0 },
    });
  };

  const handleDragEnd = () => {
    setDragging(false);
    if (localBrightness !== brightness) {
      commandLight.mutate({
        id: light.id,
        command: { brightness: localBrightness, power: true, transitionMs: 0 },
      });
    }
  };

  const type = light.type ?? "dimmer";
  const supportsColor = ["rgb", "rgbw", "cct"].includes(type);
  const supportsCCT = ["cct", "rgbw"].includes(type);

  return (
    <Card className={cn("group", !light.state?.power && "opacity-60")}>
      <CardHeader className="pb-2">
        <div className="flex items-center justify-between">
          <CardTitle className="text-base truncate">{light.name}</CardTitle>
          <Badge variant={light.type === "switch" ? "default" : "secondary"}>
            {light.type?.charAt(0).toUpperCase() + light.type?.slice(1)}
          </Badge>
        </div>
        </CardHeader>
        <CardContent className="space-y-3">
          {/* Power Toggle */}
          <div className="flex items-center justify-between">
            <div className="flex items-center gap-2">
              <button
                onClick={handlePowerToggle}
                disabled={commandLight.isPending}
                className={cn(
                  "relative flex h-10 w-10 items-center justify-center rounded-lg transition-colors",
                  "hover:bg-accent disabled:opacity-50",
                  isOn ? "bg-primary text-primary-foreground" : "bg-muted"
                )}
                aria-label={isOn ? "Turn off" : "Turn on"}
                aria-pressed={isOn}
              >
                <span className="text-lg">{isOn ? "☀️" : "🌙"}</span>
              </button>
              <div>
                <p className="font-medium">{light.name}</p>
                <p className="text-xs text-muted-foreground capitalize">{light.type}</p>
              </div>
            </div>
          </div>

          {/* Brightness Slider (for dimmable lights) */}
          {type !== "switch" && (
            <div className="space-y-1">
              <label htmlFor={`brightness-${light.id}`} className="sr-only">
                Brightness
              </label>
              <Slider
                id={`brightness-${light.id}`}
                value={[localBrightness]}
                onValueChange={handleBrightnessChange}
                onMouseDown={() => setDragging(true)}
                onMouseUp={handleDragEnd}
                onTouchStart={() => setDragging(true)}
                onTouchEnd={handleDragEnd}
                min={0}
                max={100}
                step={1}
                disabled={commandLight.isPending || !isOn}
                aria-label="Brightness"
              />
              <div className="flex justify-between text-xs text-muted-foreground">
                <span>0%</span>
                <span>{localBrightness}%</span>
                <span>100%</span>
              </div>
            </div>
          )}

          {/* Color Temperature (for CCT lights) */}
          {supportsCCT && isOn && (
            <div className="space-y-1">
              <label className="text-xs text-muted-foreground">Color Temperature</label>
              <Slider
                value={[light.state?.colorTemperature ?? 4000]}
                onValueChange={([value]) => {
                  commandLight.mutate({
                    id: light.id,
                    command: { colorTemperature: value, transitionMs: 0 },
                  });
                }}
                min={2700}
                max={6500}
                step={100}
                disabled={commandLight.isPending || !isOn}
              />
              <div className="flex justify-between text-xs text-muted-foreground">
                <span>Warm (2700K)</span>
                <span>{light.state?.colorTemperature ?? 4000}K</span>
                <span>Cool (6500K)</span>
              </div>
            </div>
          )}

          {/* RGB Color Picker (simplified) */}
          {supportsColor && !supportsCCT && isOn && (
            <div className="space-y-1">
              <label className="text-xs text-muted-foreground">Color</label>
              <div className="flex gap-2">
                {["red", "green", "blue"].map((channel) => (
                  <div key={channel} className="flex-1 space-y-1">
                    <label className="text-xs capitalize text-muted-foreground">{channel}</label>
                    <Slider
                      value={[light.state?.[channel] ?? 0]}
                      onValueChange={([value]) => {
                        commandLight.mutate({
                          id: light.id,
                          command: { [channel]: value, transitionMs: 0 },
                        });
                      }}
                      min={0}
                      max={255}
                      step={1}
                      disabled={commandLight.isPending || !isOn}
                    />
                  </div>
                ))}
              </div>
            </div>
          )}
        </CardContent>
      </Card>
    );
  }

/* ================================================================
   Overview Screen
   ================================================================ */
export function Overview() {
  const { data: lights, isLoading: lightsLoading } = useLights();
  const { data: rooms, isLoading: roomsLoading } = useRooms();
  const { data: groups, isLoading: groupsLoading } = useGroups();
  const { data: scenes, isLoading: scenesLoading } = useScenes();
  const { data: system } = useSystemInfo();
  const { user } = useAuth();

  const loading = lightsLoading || roomsLoading || groupsLoading || scenesLoading;

  if (loading) {
    return (
      <div className="flex items-center justify-center h-64">
        <div className="animate-spin rounded-full h-8 w-8 border-b-2 border-primary" />
      </div>
    );
  }

  const lightsOn = lights?.filter((l) => l.state?.power).length ?? 0;
  const totalLights = lights?.length ?? 0;

  return (
    <div className="space-y-6">
      {/* Header */}
      <div className="flex flex-col sm:flex-row sm:items-center sm:justify-between gap-4">
        <div>
          <h1 className="text-3xl font-bold tracking-tight">Overview</h1>
          <p className="text-muted-foreground">
            Welcome back, {user?.username ?? "admin"}. Control your lights from here.
          </p>
        </div>
        <div className="flex items-center gap-2">
          <span className="text-sm text-muted-foreground">
            {totalLights} lights • {lightsOn} on
          </span>
        </div>
      </div>

      {/* Stats Cards */}
      <div className="grid gap-4 md:grid-cols-2 lg:grid-cols-4">
        <StatCard title="Total Lights" value={totalLights} icon={<Lightbulb />} />
        <StatCard title="Lights On" value={lightsOn} icon={<Sun />} />
        <StatCard title="Rooms" value={rooms?.length ?? 0} icon={<Home />} />
        <StatCard title="Scenes" value={scenes?.length ?? 0} icon={<Grid />} />
      </div>

      {/* Quick Actions */}
      <div className="flex flex-wrap gap-2">
        <Button variant="outline" size="sm">
          <Power className="mr-2 h-4 w-4" />
          All Off
        </Button>
        <Button variant="outline" size="sm">
          <Zap className="mr-2 h-4 w-4" />
          All On
        </Button>
        <Button variant="outline" size="sm">
          <SlidersHorizontal className="mr-2 h-4 w-4" />
          50% Brightness
        </Button>
      </div>

      {/* Lights Grid */}
      <div>
        <h2 className="text-lg font-semibold mb-4">All Lights</h2>
        {lights && lights.length === 0 ? (
          <Card className="py-12 text-center">
            <CardContent className="py-8">
              <Lightbulb className="mx-auto h-12 w-12 text-muted-foreground" />
              <p className="mt-4 text-muted-foreground">No lights configured yet</p>
              <p className="text-sm text-muted-foreground">
                Add lights in the Devices screen
              </p>
            </CardContent>
          </Card>
        ) : (
          <div className="grid gap-4 sm:grid-cols-2 lg:grid-cols-3 xl:grid-cols-4">
            {lights?.map((light) => (
              <LightCard key={light.id} light={light} />
            ))}
          </div>
        )}
      </div>
    </div>
  );
}

/* ================================================================
   Stat Card Component
   ================================================================ */
interface StatCardProps {
  title: string;
  value: number;
  icon: React.ReactNode;
}

function StatCard({ title, value, icon }: StatCardProps) {
  return (
    <Card>
      <CardContent className="flex items-center gap-4">
        <div className="flex h-12 w-12 items-center justify-center rounded-lg bg-primary/10">
          {icon}
        </div>
        <div>
          <p className="text-sm text-muted-foreground">{title}</p>
          <p className="text-2xl font-bold">{value}</p>
        </div>
      </CardContent>
    </Card>
  );
}