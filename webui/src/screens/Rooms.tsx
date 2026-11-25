// Rooms (DESIGN §7.3): grid of RoomCards; room detail with master controls,
// individual LightCards and an edit dialog (RHF + Zod).
import * as React from "react";
import { Link, Route, Routes, useNavigate, useParams } from "react-router-dom";
import { useForm } from "react-hook-form";
import { zodResolver } from "@hookform/resolvers/zod";
import { ArrowLeft, Home, Pencil, Plus, Trash2 } from "lucide-react";

import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Dialog, DialogContent, DialogDescription, DialogFooter, DialogHeader, DialogTitle } from "@/components/ui/dialog";
import { Skeleton } from "@/components/ui/skeleton";
import { ConfirmDialog, EmptyState, Field, PageHeader } from "@/components/common";
import { RoomCard } from "@/components/lighting/RoomCard";
import { LightCard } from "@/components/lighting/LightCard";
import { useCreateRoom, useDeleteRoom, useLights, useRooms, useUpdateRoom } from "@/services/queries";
import { RoomForm, type Light, type Room } from "@/schemas";

function RoomDialog({ open, onOpenChange, room, lights }: { open: boolean; onOpenChange: (o: boolean) => void; room?: Room; lights: Light[] }) {
  const create = useCreateRoom();
  const update = useUpdateRoom();
  const form = useForm<RoomForm>({
    resolver: zodResolver(RoomForm),
    values: { name: room?.name ?? "", floor: room?.floor ?? "", icon: room?.icon ?? "", channelIds: room?.channelIds ?? [] },
  });
  const selected = form.watch("channelIds");
  const submit = form.handleSubmit(async (v) => {
    if (room) await update.mutateAsync({ id: room.id, room: v });
    else await create.mutateAsync(v);
    onOpenChange(false);
  });
  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent className="max-h-[90dvh] overflow-y-auto">
        <DialogHeader>
          <DialogTitle>{room ? "Edit room" : "New room"}</DialogTitle>
          <DialogDescription>Rooms group lights for master power and brightness.</DialogDescription>
        </DialogHeader>
        <form id="room-form" onSubmit={submit} className="space-y-4">
          <Field label="Name" htmlFor="room-name" error={form.formState.errors.name?.message}>
            <Input id="room-name" {...form.register("name")} autoFocus />
          </Field>
          <Field label="Floor (optional)" htmlFor="room-floor">
            <Input id="room-floor" {...form.register("floor")} placeholder="Ground floor" />
          </Field>
          <fieldset className="space-y-2">
            <legend className="text-sm font-medium">Lights</legend>
            {lights.length === 0 && <p className="text-sm text-muted-foreground">Add lights on the Devices page first.</p>}
            <div className="max-h-56 space-y-1 overflow-y-auto">
              {lights.map((l) => {
                const other = l.roomId && l.roomId !== room?.id;
                return (
                  <label key={l.id} className="flex min-h-11 cursor-pointer items-center gap-3 rounded-md px-2 hover:bg-muted">
                    <input
                      type="checkbox"
                      className="size-4 accent-[var(--primary)]"
                      checked={selected.includes(l.id)}
                      onChange={(e) =>
                        form.setValue(
                          "channelIds",
                          e.target.checked ? [...selected, l.id] : selected.filter((x) => x !== l.id),
                          { shouldDirty: true },
                        )
                      }
                    />
                    <span className="flex-1 text-sm">{l.name}</span>
                    {other && <span className="text-xs text-muted-foreground">moves from another room</span>}
                  </label>
                );
              })}
            </div>
          </fieldset>
        </form>
        <DialogFooter>
          <Button variant="outline" onClick={() => onOpenChange(false)}>Cancel</Button>
          <Button type="submit" form="room-form" disabled={form.formState.isSubmitting}>
            {room ? "Save" : "Create room"}
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}

function RoomList() {
  const rooms = useRooms();
  const lights = useLights();
  const [creating, setCreating] = React.useState(false);
  return (
    <>
      <PageHeader title="Rooms" actions={<Button onClick={() => setCreating(true)}><Plus className="size-4" /> New room</Button>} />
      {rooms.isLoading ? (
        <div className="grid items-start gap-3 sm:grid-cols-2 xl:grid-cols-3">{[0, 1, 2].map((i) => <Skeleton key={i} className="h-28 rounded-xl" />)}</div>
      ) : (rooms.data ?? []).length === 0 ? (
        <EmptyState icon={Home} title="No rooms yet" description="Create a room and add lights to it." action={<Button onClick={() => setCreating(true)}>New room</Button>} />
      ) : (
        <div className="grid items-start gap-3 sm:grid-cols-2 xl:grid-cols-3">
          {(rooms.data ?? []).map((r) => <RoomCard key={r.id} room={r} lights={lights.data ?? []} />)}
        </div>
      )}
      <RoomDialog open={creating} onOpenChange={setCreating} lights={lights.data ?? []} />
    </>
  );
}

function RoomDetail() {
  const { id } = useParams();
  const navigate = useNavigate();
  const rooms = useRooms();
  const lights = useLights();
  const del = useDeleteRoom();
  const [editing, setEditing] = React.useState(false);
  const [deleting, setDeleting] = React.useState(false);
  const room = rooms.data?.find((r) => r.id === id);
  if (rooms.isLoading) return <Skeleton className="h-40 rounded-xl" />;
  if (!room) return <EmptyState icon={Home} title="Room not found" description="It may have been deleted." action={<Button asChild><Link to="/rooms">Back to rooms</Link></Button>} />;
  const members = (lights.data ?? []).filter((l) => room.channelIds.includes(l.id));
  return (
    <div className="space-y-6">
      <Link to="/rooms" className="inline-flex items-center gap-1 text-sm text-muted-foreground hover:text-foreground">
        <ArrowLeft className="size-4" /> Rooms
      </Link>
      <PageHeader
        title={room.name}
        description={room.floor || undefined}
        actions={
          <>
            <Button variant="outline" onClick={() => setEditing(true)}><Pencil className="size-4" /> Edit</Button>
            <Button variant="destructive" onClick={() => setDeleting(true)}><Trash2 className="size-4" /> Delete</Button>
          </>
        }
      />
      <RoomCard room={room} lights={lights.data ?? []} />
      <section className="space-y-3" aria-labelledby="room-lights">
        <h2 id="room-lights" className="text-lg font-semibold">Lights</h2>
        {members.length === 0 ? (
          <p className="text-sm text-muted-foreground">This room has no lights. Use Edit to add some.</p>
        ) : (
          <div className="grid items-start gap-3 sm:grid-cols-2 xl:grid-cols-3">{members.map((l) => <LightCard key={l.id} light={l} />)}</div>
        )}
      </section>
      <RoomDialog open={editing} onOpenChange={setEditing} room={room} lights={lights.data ?? []} />
      <ConfirmDialog
        open={deleting}
        onOpenChange={setDeleting}
        destructive
        title={`Delete “${room.name}”?`}
        description="The lights stay configured; they are only removed from this room."
        confirmLabel="Delete room"
        onConfirm={() => del.mutate(room.id, { onSuccess: () => navigate("/rooms") })}
      />
    </div>
  );
}

export default function Rooms() {
  return (
    <Routes>
      <Route index element={<RoomList />} />
      <Route path=":id" element={<RoomDetail />} />
    </Routes>
  );
}
