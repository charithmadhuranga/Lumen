"use client";

import * as React from "react";
import { useQueryClient } from "@tanstack/react-query";
import { cn } from "@/lib/utils";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { Badge } from "@/components/ui/badge";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Separator } from "@/components/ui/separator";
import { ScrollArea } from "@/components/ui/scroll-area";
import {
  Alert,
  AlertDescription,
  AlertDialog,
  AlertDialogTrigger,
  AlertDialogContent,
  AlertDialogHeader,
  AlertDialogTitle,
  AlertDialogDescription,
  AlertDialogFooter,
  AlertDialogAction,
  AlertDialogCancel,
} from "@/components/ui/alert-dialog";
import { Slider } from "@/components/ui/slider";
import {
  Plus,
  Grid,
  Play,
  Copy,
  Edit,
  Trash2,
  Download,
  Upload,
  Loader2,
  Palette,
  Sun,
  Moon,
  Zap,
} from "lucide-react";
import { useScenes, useLights, useCreateScene, useUpdateScene, useDeleteScene, useActivateScene, useDuplicateScene } from "@/services/queries";
import { Scene, LightChannel } from "@/schemas";

/* ================================================================
   Scene Card Component
   ================================================================ */
interface SceneCardProps {
  scene: Scene;
  lights: LightChannel[];
  onEdit: (scene: Scene) => void;
  onDelete: (id: string) => void;
  onDuplicate: (id: string) => void;
  onActivate: (id: string) => void;
}

function SceneCard({ scene, lights, onEdit, onDelete, onDuplicate, onActivate }: SceneCardProps) {
  const queryClient = useQueryClient();
  const activateScene = useActivateScene();
  const duplicateScene = useDuplicateScene();
  const deleteScene = useDeleteScene();

  const lightCount = scene.states?.length ?? 0;
  const hasTransition = (scene.transitionMs ?? 0) > 0;

  return (
    <Card className="group">
      <CardHeader>
        <div className="flex items-start justify-between">
          <div>
            <CardTitle className="text-lg">{scene.name}</CardTitle>
            <p className="text-sm text-muted-foreground">
              {lightCount} light{lightCount !== 1 ? "s" : ""}
              {hasTransition && ` • ${scene.transitionMs}ms transition`}
            </p>
          </div>
          <div className="flex items-center gap-1">
            <Button variant="ghost" size="icon" onClick={() => onEdit(scene)}>
              <Edit className="h-4 w-4" />
            </Button>
            <Button variant="ghost" size="icon" onClick={() => onDuplicate(scene.id)}>
              <Copy className="h-4 w-4" />
            </Button>
            <Button variant="ghost" size="icon" onClick={() => onDelete(scene.id)} className="text-destructive">
              <Trash2 className="h-4 w-4" />
            </Button>
          </div>
        </div>
      </CardHeader>
      <CardContent className="space-y-3">
        {/* Activate Button */}
        <Button
          onClick={() => activateScene.mutate({ id: scene.id })}
          disabled={activateScene.isPending}
          className="w-full"
          size="lg"
        >
          <Play className="mr-2 h-4 w-4" />
          {activateScene.isPending ? "Activating..." : "Activate Scene"}
        </Button>

        {/* Transition */}
        {hasTransition && (
          <div className="flex items-center gap-2 text-sm text-muted-foreground">
            <Zap className="h-4 w-4" />
            <span>Transition: {scene.transitionMs}ms</span>
          </div>
        )}

        {/* Light States Preview */}
        <div className="space-y-2">
          <p className="text-xs font-medium text-muted-foreground">Light States</p>
          <div className="grid gap-2 sm:grid-cols-2 max-h-48 overflow-y-auto">
            {scene.states?.map((state) => {
              const light = lights.find((l) => l.id === state.lightId);
              return (
                <div key={state.lightId} className="flex items-center justify-between p-2 rounded-lg bg-muted/50">
                  <div className="flex items-center gap-2 min-w-0">
                    <div className={cn(
                      "relative flex h-8 w-8 items-center justify-center rounded",
                      state.power ? "bg-yellow-100 dark:bg-yellow-900" : "bg-muted"
                    )}>
                      <span className="text-sm">{state.power ? "☀️" : "🌙"}</span>
                    </div>
                    <div className="min-w-0">
                      <p className="text-sm font-medium truncate">{light?.name ?? state.lightId}</p>
                      <p className="text-xs text-muted-foreground capitalize">{light?.type}</p>
                    </div>
                  </div>
                  <div className="flex items-center gap-1 text-xs text-muted-foreground">
                    {state.brightness > 0 && <span>🔆 {state.brightness}%</span>}
                    {(state.red || state.green || state.blue) && (
                      <span>🎨 RGB({state.red},{state.green},{state.blue})</span>
                    )}
                    {state.colorTemperature && <span>🌡 {state.colorTemperature}K</span>}
                  </div>
                </div>
              );
            })}
          </div>
        </div>
      </CardContent>
    </Card>
  );
}

/* ================================================================
   Scene Dialog
   ================================================================ */
interface SceneDialogProps {
  open: boolean;
  onOpenChange: (open: boolean) => void;
  scene?: Scene | null;
  lights: LightChannel[];
  onSubmit: (data: any) => Promise<void>;
  loading: boolean;
}

function SceneDialog({ open, onOpenChange, scene, lights, onSubmit, loading }: SceneDialogProps) {
  const { useForm } = require("react-hook-form");
  const { zodResolver } = require("@hookform/resolvers/zod");
  const { z } = require("zod");

  const form = useForm({
    resolver: zodResolver(z.object({
      id: z.string().min(1).optional(),
      name: z.string().min(1),
      transitionMs: z.number().int().min(0).optional(),
      states: z.array(z.object({
        lightId: z.string(),
        power: z.boolean(),
        brightness: z.number().int().min(0).max(100),
        red: z.number().int().min(0).max(255),
        green: z.number().int().min(0).max(255),
        blue: z.number().int().min(0).max(255),
        white: z.number().int().min(0).max(255),
        colorTemperature: z.number().int().min(1500).max(6500),
      })).optional(),
    })),
    defaultValues: scene ? {
      id: scene.id,
      name: scene.name,
      transitionMs: scene.transitionMs,
      states: scene.states,
    } : { name: "", transitionMs: 0, states: [] },
  });

  const [expandedLights, setExpandedLights] = React.useState<string[]>([]);

  const onSubmitHandler = async (data: any) => {
    try {
      await onSubmit(data);
      onOpenChange(false);
    } catch (e) {
      // Error handled by mutation
    }
  };

  const addLightState = () => {
    const current = form.getValues("states") ?? [];
    form.setValue("states", [...current, {
      lightId: lights[0]?.id ?? "",
      power: true,
      brightness: 100,
      red: 255,
      green: 255,
      blue: 255,
      white: 0,
      colorTemperature: 4000,
    }]);
  };

  const removeLightState = (index: number) => {
    const current = form.getValues("states") ?? [];
    form.setValue("states", current.filter((_: any, i: number) => i !== index));
  };

  return (
    <AlertDialog open={open} onOpenChange={onOpenChange}>
      <AlertDialogContent className="max-w-2xl max-h-[80vh] overflow-y-auto">
        <AlertDialogHeader>
          <AlertDialogTitle>{scene ? "Edit Scene" : "Create Scene"}</AlertDialogTitle>
          <AlertDialogDescription>
            {scene ? "Update scene details and light states" : "Create a new scene with custom light states"}
          </AlertDialogDescription>
        </AlertDialogHeader>
        <form onSubmit={form.handleSubmit(onSubmitHandler)} className="space-y-4">
          <div className="space-y-2">
            <Label htmlFor="name">Name</Label>
            <Input id="name" {...form.register("name")} placeholder="Movie Night" disabled={loading} />
            {form.formState.errors.name && (
              <p className="text-sm text-destructive">{form.formState.errors.name.message}</p>
            )}
          </div>

          <div className="space-y-2">
            <Label htmlFor="transitionMs">Transition (ms)</Label>
            <Input
              id="transitionMs"
              type="number"
              {...form.register("transitionMs", { valueAsNumber: true })}
              placeholder="0"
              min={0}
              disabled={loading}
            />
          </div>

          <div className="space-y-2">
            <div className="flex items-center justify-between">
              <Label>Light States</Label>
              <Button type="button" variant="outline" size="sm" onClick={addLightState} disabled={loading}>
                <Plus className="mr-1 h-3 w-3" />
                Add Light
              </Button>
            </div>

            {form.getValues("states")?.length === 0 ? (
              <p className="text-sm text-muted-foreground text-center py-4">
                No lights added yet. Click "Add Light" to include lights in this scene.
              </p>
            ) : (
              <div className="space-y-3">
                {form.getValues("states")?.map((state: any, index: number) => (
                  <div key={index} className="border rounded-lg p-3 space-y-3">
                    <div className="flex items-center justify-between">
                      <h4 className="font-medium">Light {index + 1}</h4>
                      <Button
                        type="button"
                        variant="ghost"
                        size="icon"
                        onClick={() => removeLightState(index)}
                        className="text-destructive"
                        disabled={loading}
                      >
                        <Trash2 className="h-4 w-4" />
                      </Button>
                    </div>

                    <div className="grid gap-2 sm:grid-cols-2">
                      <div className="space-y-2">
                        <Label htmlFor={`lightId-${index}`}>Light</Label>
                        <select
                          {...form.register(`states.${index}.lightId`, { valueAsString: true })}
                          disabled={loading}
                          className="w-full rounded border p-2"
                        >
                          {lights.map((l) => (
                            <option key={l.id} value={l.id}>{l.name} ({l.type})</option>
                          ))}
                        </select>
                      </div>

                      <div className="space-y-2">
                        <Label htmlFor={`power-${index}`}>Power</Label>
                        <select
                          {...form.register(`states.${index}.power`, { valueAsBoolean: true })}
                          disabled={loading}
                          className="w-full rounded border p-2"
                        >
                          <option value="true">On</option>
                          <option value="false">Off</option>
                        </select>
                      </div>

                      <div className="space-y-2">
                        <Label htmlFor={`brightness-${index}`}>Brightness</Label>
                        <Slider
                          value={[form.watch(`states.${index}.brightness`) ?? 100]}
                          onValueChange={([v]) => form.setValue(`states.${index}.brightness`, v)}
                          min={0}
                          max={100}
                          step={1}
                          disabled={loading}
                        />
                        <div className="flex justify-between text-xs text-muted-foreground">
                          <span>0%</span>
                          <span>{form.watch(`states.${index}.brightness`) ?? 100}%</span>
                          <span>100%</span>
                        </div>
                      </div>

                      <div className="space-y-2">
                        <Label htmlFor={`colorTemperature-${index}`}>Color Temp (K)</Label>
                        <Slider
                          value={[form.watch(`states.${index}.colorTemperature`) ?? 4000]}
                          onValueChange={([v]) => form.setValue(`states.${index}.colorTemperature`, v)}
                          min={2700}
                          max={6500}
                          step={100}
                          disabled={loading}
                        />
                      </div>

                      <div className="space-y-2">
                        <Label>RGB</Label>
                        <div className="grid gap-2 grid-cols-3">
                          {["red", "green", "blue"].map((channel) => (
                            <div key={channel} className="space-y-1">
                              <Label htmlFor={`${channel}-${index}`}>{channel.toUpperCase()}</Label>
                              <Slider
                                value={[form.watch(`states.${index}.${channel}`) ?? 255]}
                                onValueChange={([v]) => form.setValue(`states.${index}.${channel}`, v)}
                                min={0}
                                max={255}
                                step={1}
                                disabled={loading}
                              />
                            </div>
                          ))}
                        </div>
                      </div>

                      <div className="space-y-2">
                        <Label htmlFor={`white-${index}`}>White</Label>
                        <Slider
                          value={[form.watch(`states.${index}.white`) ?? 0]}
                          onValueChange={([v]) => form.setValue(`states.${index}.white`, v)}
                          min={0}
                          max={255}
                          step={1}
                          disabled={loading}
                        />
                      </div>
                    </div>
                  </div>
                ))}
              </div>
            )}
          </div>

          <AlertDialogFooter>
            <Button type="button" variant="outline" onClick={() => onOpenChange(false)} disabled={loading}>
              Cancel
            </Button>
            <Button type="submit" disabled={loading}>
              {loading ? "Saving..." : scene ? "Update" : "Create"}
            </Button>
          </AlertDialogFooter>
        </form>
      </AlertDialogContent>
    </AlertDialog>
  );
}

/* ================================================================
   Scenes Screen
   ================================================================ */
export function Scenes() {
  const { data: scenes, isLoading: scenesLoading } = useScenes();
  const { data: lights, isLoading: lightsLoading } = useLights();
  const [dialogOpen, setDialogOpen] = React.useState(false);
  const [editingScene, setEditingScene] = React.useState<Scene | null>(null);
  const createScene = useCreateScene();
  const updateScene = useUpdateScene();
  const deleteScene = useDeleteScene();
  const activateScene = useActivateScene();
  const duplicateScene = useDuplicateScene();

  const handleSubmit = async (data: any) => {
    if (data.id) {
      const { id, ...updates } = data;
      await updateScene.mutateAsync({ id, updates });
    } else {
      await createScene.mutateAsync(data);
    }
  };

  const handleDelete = async (id: string) => {
    if (confirm("Delete this scene?")) {
      await deleteScene.mutateAsync(id);
    }
  };

  const handleDuplicate = async (id: string) => {
    await duplicateScene.mutateAsync(id);
  };

  const handleActivate = async (id: string) => {
    await activateScene.mutateAsync({ id });
  };

  const handleEdit = (scene: Scene) => {
    setEditingScene(scene);
    setDialogOpen(true);
  };

  const handleCreate = () => {
    setEditingScene(null);
    setDialogOpen(true);
  };

  if (scenesLoading || lightsLoading) {
    return (
      <div className="flex items-center justify-center h-64">
        <div className="animate-spin rounded-full h-8 w-8 border-b-2 border-primary" />
      </div>
    );
  }

  return (
    <div className="space-y-6">
      <div className="flex flex-col sm:flex-row sm:items-center sm:justify-between gap-4">
        <div>
          <h1 className="text-3xl font-bold tracking-tight">Scenes</h1>
          <p className="text-muted-foreground">Save and activate lighting presets</p>
        </div>
        <Button onClick={handleCreate}>
          <Plus className="mr-2 h-4 w-4" />
          Create Scene
        </Button>
      </div>

      {scenes && scenes.length === 0 ? (
        <Card className="py-12 text-center">
          <CardContent className="py-8">
            <Grid className="mx-auto h-12 w-12 text-muted-foreground" />
            <p className="mt-4 text-muted-foreground">No scenes yet</p>
            <p className="text-sm text-muted-foreground">Create your first scene to save lighting presets</p>
            <Button onClick={handleCreate} className="mt-4">
              <Plus className="mr-2 h-4 w-4" />
              Create Scene
            </Button>
          </CardContent>
        </Card>
      ) : (
        <div className="grid gap-4 md:grid-cols-2 lg:grid-cols-3 xl:grid-cols-4">
          {scenes?.map((scene) => (
            <SceneCard
              key={scene.id}
              scene={scene}
              lights={lights ?? []}
              onEdit={handleEdit}
              onDelete={handleDelete}
              onDuplicate={handleDuplicate}
              onActivate={handleActivate}
            />
          ))}
        </div>
      )}
    </div>
  );
}