"use client";

import * as React from "react";
import { cn } from "@/lib/utils";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { Badge } from "@/components/ui/badge";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { ScrollArea } from "@/components/ui/scroll-area";
import { Alert, AlertDescription } from "@/components/ui/alert";
import {
  Plus,
  Home,
  Edit,
  Trash2,
  Power,
  SlidersHorizontal,
} from "lucide-react";
import { useRooms, useLights, useDeleteRoom, useRoomPower, useRoomBrightness } from "@/services/queries";
import { useCommandThrottle } from "@/stores/liveStore";
import { Slider } from "@/components/ui/slider";
import { Room, LightChannel } from "@/schemas";

/* ================================================================
   Room Card Component
   ================================================================ */
interface RoomCardProps {
  room: Room;
  lights: LightChannel[];
  onEdit: (room: Room) => void;
  onDelete: (id: string) => void;
}

function RoomCard({ room, lights, onEdit, onDelete }: RoomCardProps) {
  const roomPower = useRoomPower();
  const roomBrightness = useRoomBrightness();

  const lightsInRoom = lights.filter((l) => room.channelIds.includes(l.id));
  const lightsOn = lightsInRoom.filter((l) => l.state?.power).length;
  const totalLights = lightsInRoom.length;
  const avgBrightness = lightsInRoom.reduce((sum, l) => sum + (l.state?.brightness ?? 0), 0) / totalLights || 0;

  const isAnyOn = lightsOn > 0;
  const [localBrightness, setLocalBrightness] = React.useState(Math.round(avgBrightness));

  return (
    <Card className="group">
      <CardHeader>
        <div className="flex items-start justify-between">
          <div>
            <CardTitle className="text-lg">{room.name}</CardTitle>
            <p className="text-sm text-muted-foreground">
              {totalLights} light{totalLights !== 1 ? "s" : ""} • {lightsOn} on
            </p>
            {room.floor && (
              <p className="text-xs text-muted-foreground">{room.floor}</p>
            )}
          </div>
          <div className="flex items-center gap-2">
            <Button variant="ghost" size="icon" onClick={() => onEdit(room)}>
              <Edit className="h-4 w-4" />
            </Button>
            <Button variant="ghost" size="icon" onClick={() => onDelete(room.id)} className="text-destructive">
              <Trash2 className="h-4 w-4" />
            </Button>
          </div>
        </div>
      </CardHeader>
      <CardContent className="space-y-3">
        {/* Master Controls */}
        <div className="flex items-center gap-3">
          <button
            onClick={() => roomPower.mutate({ id: room.id, power: !isAnyOn, transitionMs: 0 })}
            disabled={roomPower.isPending}
            className={cn(
              "relative flex h-12 w-12 items-center justify-center rounded-lg transition-colors",
              "hover:bg-accent disabled:opacity-50",
              isAnyOn ? "bg-primary text-primary-foreground" : "bg-muted"
            )}
            aria-label={isAnyOn ? "Turn off all" : "Turn on all"}
          >
            <span className="text-xl">{isAnyOn ? "☀️" : "🌙"}</span>
          </button>
          <div className="flex-1">
            <p className="text-sm font-medium">Master Power</p>
            <p className="text-xs text-muted-foreground">
              {isAnyOn ? "On" : "Off"} • {lightsOn}/{totalLights} lights
            </p>
          </div>
        </div>

        {/* Master Brightness */}
        {totalLights > 0 && (
          <div className="space-y-1">
            <label className="text-xs text-muted-foreground">Master Brightness</label>
            <Slider
              value={[localBrightness]}
              onValueChange={([value]) => {
                setLocalBrightness(value);
                roomBrightness.mutate({ id: room.id, brightness: value, transitionMs: 0 });
              }}
              onMouseDown={() => setDragging(true)}
              onMouseUp={() => setDragging(false)}
              min={0}
              max={100}
              step={1}
              disabled={roomBrightness.isPending || !isAnyOn}
            />
            <div className="flex justify-between text-xs text-muted-foreground">
              <span>0%</span>
              <span>{localBrightness}%</span>
              <span>100%</span>
            </div>
          </div>
        )}

        {/* Individual Lights */}
        {lightsInRoom.length > 0 && (
          <div className="space-y-2 pt-2 border-t">
            <p className="text-xs font-medium text-muted-foreground">Lights in this room</p>
            <div className="grid gap-2 sm:grid-cols-2">
              {lightsInRoom.map((light) => (
                <div key={light.id} className="flex items-center justify-between p-2 rounded-lg bg-muted/50">
                  <div className="flex items-center gap-2">
                    <button
                      onClick={() => {}}
                      disabled={false}
                      className={cn(
                        "relative flex h-8 w-8 items-center justify-center rounded transition-colors",
                        "hover:bg-accent",
                        light.state?.power ? "bg-primary text-primary-foreground" : "bg-muted"
                      )}
                    >
                      <span className="text-sm">{light.state?.power ? "☀️" : "🌙"}</span>
                    </button>
                    <div>
                      <p className="text-sm font-medium truncate max-w-[120px]">{light.name}</p>
                      <p className="text-xs text-muted-foreground capitalize">{light.type}</p>
                    </div>
                  </div>
                  {light.type !== "switch" && (
                    <div className="flex items-center gap-2 w-32">
                      <Slider
                        value={[light.state?.brightness ?? 0]}
                        onValueChange={([value]) => {
                          // Individual light brightness change
                        }}
                        min={0}
                        max={100}
                        step={1}
                        disabled={!light.state?.power}
                      />
                    </div>
                  )}
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
   Rooms Screen
   ================================================================ */
export function Rooms() {
  const { data: rooms, isLoading: roomsLoading } = useRooms();
  const { data: lights, isLoading: lightsLoading } = useLights();
  const deleteRoom = useDeleteRoom();

  const handleDelete = async (id: string) => {
    if (confirm("Delete this room? Lights will be detached but not deleted.")) {
      await deleteRoom.mutateAsync(id);
    }
  };

  if (roomsLoading || lightsLoading) {
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
          <h1 className="text-3xl font-bold tracking-tight">Rooms</h1>
          <p className="text-muted-foreground">Organize lights into rooms for group control</p>
        </div>
        <Button onClick={handleCreate}>
          <Plus className="mr-2 h-4 w-4" />
          Add Room
        </Button>
      </div>

      {rooms && rooms.length === 0 ? (
        <Card className="py-12 text-center">
          <CardContent className="py-8">
            <Home className="mx-auto h-12 w-12 text-muted-foreground" />
            <p className="mt-4 text-muted-foreground">No rooms yet</p>
            <p className="text-sm text-muted-foreground">Create your first room to organize lights</p>
            <Button onClick={handleCreate} className="mt-4">
              <Plus className="mr-2 h-4 w-4" />
              Create Room
            </Button>
          </CardContent>
        </Card>
      ) : (
        <div className="grid gap-4 md:grid-cols-2 lg:grid-cols-3 xl:grid-cols-4">
          {rooms?.map((room) => (
            <RoomCard
              key={room.id}
              room={room}
              lights={lights ?? []}
              onEdit={handleEdit}
              onDelete={handleDelete}
            />
          ))}
        </div>
      )}
    </div>
  );
}