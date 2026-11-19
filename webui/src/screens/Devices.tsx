"use client";

import * as React from "react";
import { cn } from "@/lib/utils";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { Badge } from "@/components/ui/badge";
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from "@/components/ui/table";
import { Plus, Edit, Trash2, Lightbulb } from "lucide-react";
import { useLights } from "@/services/queries";
import { LightChannel } from "@/schemas";

interface LightRowProps {
  light: LightChannel;
  onEdit: (light: LightChannel) => void;
  onDelete: (id: string) => void;
}

function LightRow({ light, onEdit, onDelete }: LightRowProps) {
  return (
    <TableRow>
      <TableCell className="font-medium">{light.name}</TableCell>
      <TableCell>
        <Badge variant={light.type === "switch" ? "default" : "secondary"}>
          {light.type?.charAt(0).toUpperCase() + light.type?.slice(1)}
        </Badge>
      </TableCell>
      <TableCell>
        <div className="flex items-center gap-2">
          <span className={cn(
            "h-2 w-2 rounded-full",
            light.state?.power ? "bg-success" : "bg-muted"
          )} />
          <span className="text-sm">{light.state?.power ? "On" : "Off"}</span>
        </div>
      </TableCell>
      <TableCell>
        {light.type !== "switch" && light.state?.brightness !== undefined ? (
          <span className="text-sm">{light.state.brightness}%</span>
        ) : (
          <span className="text-muted-foreground">—</span>
        )}
      </TableCell>
      <TableCell>
        {light.state?.colorTemperature && (
          <span className="text-sm">{light.state.colorTemperature}K</span>
        )}
      </TableCell>
      <TableCell className="text-right">
        <div className="flex items-center justify-end gap-1">
          <button className="p-1 rounded hover:bg-accent" aria-label="Edit" onClick={() => onEdit(light)}>
            <Edit className="h-4 w-4" />
          </button>
          <button className="p-1 rounded hover:bg-accent text-destructive" aria-label="Delete" onClick={() => onDelete(light.id)}>
            <Trash2 className="h-4 w-4" />
          </button>
        </div>
      </TableCell>
    </TableRow>
  );
}

export function Devices() {
  const { data: lights, isLoading } = useLights();

  if (isLoading) {
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
          <h1 className="text-3xl font-bold tracking-tight">Devices</h1>
          <p className="text-muted-foreground">Manage light channels and their configuration</p>
        </div>
        <Button onClick={() => {}}>
          <Plus className="mr-2 h-4 w-4" />
          Add Light
        </Button>
      </div>

      {lights && lights.length === 0 ? (
        <Card className="py-12 text-center">
          <CardContent className="py-8">
            <Lightbulb className="mx-auto h-12 w-12 text-muted-foreground" />
            <p className="mt-4 text-muted-foreground">No lights configured</p>
            <p className="text-sm text-muted-foreground">
              Add your first light channel to start controlling
            </p>
            <Button onClick={() => {}} className="mt-4">
              <Plus className="mr-2 h-4 w-4" />
              Add Light
            </Button>
          </CardContent>
        </Card>
      ) : (
        <Card>
          <CardHeader>
            <CardTitle>Light Channels ({lights?.length ?? 0})</CardTitle>
          </CardHeader>
          <CardContent>
            <Table>
              <TableHeader>
                <TableRow>
                  <TableHead>Name</TableHead>
                  <TableHead>Type</TableHead>
                  <TableHead>Power</TableHead>
                  <TableHead>Brightness</TableHead>
                  <TableHead>Color Temp</TableHead>
                  <TableHead className="text-right">Actions</TableHead>
                </TableRow>
              </TableHeader>
              <TableBody>
                {lights?.map((light) => (
                  <LightRow key={light.id} light={light} onEdit={() => {}} onDelete={() => {}} />
                ))}
              </TableBody>
            </Table>
          </CardContent>
        </Card>
      )}
    </div>
  );
}