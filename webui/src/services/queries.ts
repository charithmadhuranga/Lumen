// TanStack Query hooks (DESIGN §8.3). Config resources: staleTime 30 s;
// light state stays fresh through WebSocket cache patches. Mutations
// invalidate what they change; light commands rely on WS confirmation.
import { QueryClient, useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { toast } from "sonner";

import { api, ApiError } from "@/services/api";
import { queryKeys } from "@/services/queryKeys";
import { errorMessage } from "@/hooks/useAuth";
import type { Light, LightCommand } from "@/schemas";

export { queryKeys };

export function createQueryClient(): QueryClient {
  return new QueryClient({
    defaultOptions: {
      queries: {
        staleTime: 30_000,
        refetchOnWindowFocus: false,
        retry: (count, error) => !(error instanceof ApiError && [401, 403, 404].includes(error.status)) && count < 1,
      },
      mutations: { retry: 0 },
    },
  });
}

function onError(title: string) {
  return (e: unknown) => toast.error(title, { description: errorMessage(e), duration: Infinity });
}

/* --------------------------------------------------------------- system */
export const useSystemInfo = () => useQuery({ queryKey: queryKeys.systemInfo(), queryFn: api.systemInfo, staleTime: 10_000 });
export const useDiagnostics = (enabled: boolean) =>
  useQuery({ queryKey: queryKeys.diagnostics(), queryFn: api.diagnostics, enabled, refetchInterval: enabled ? 2000 : false });

/* --------------------------------------------------------------- lights */
export const useLights = () => useQuery({ queryKey: queryKeys.lights(), queryFn: api.lights, staleTime: Infinity });
export const useEffects = () => useQuery({ queryKey: queryKeys.effects(), queryFn: api.effects, staleTime: Infinity });

export function useLightCommand() {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: ({ id, command }: { id: string; command: LightCommand }) => api.commandLight(id, command),
    onMutate: ({ id, command }) => {
      // Optimistic: reflect the target immediately; the WS event reconciles.
      qc.setQueryData<Light[]>(queryKeys.lights(), (old) =>
        old?.map((l) => {
          if (l.id !== id) return l;
          const s = { ...l.state };
          if (command.toggle) s.power = !s.power;
          if (command.power !== undefined) s.power = command.power;
          if (command.brightness !== undefined) {
            s.brightness = command.brightness;
            if (command.power === undefined) s.power = command.brightness > 0;
          }
          if (command.red !== undefined) s.red = command.red;
          if (command.green !== undefined) s.green = command.green;
          if (command.blue !== undefined) s.blue = command.blue;
          if (command.white !== undefined) s.white = command.white;
          if (command.colorTemperature !== undefined) s.colorTemperature = command.colorTemperature;
          return { ...l, state: s };
        }),
      );
    },
    onError: (e) => {
      void qc.invalidateQueries({ queryKey: queryKeys.lights() });
      onError("Light command failed")(e);
    },
  });
}

function useInvalidating<TArgs, TResult>(
  fn: (args: TArgs) => Promise<TResult>,
  keys: readonly (readonly unknown[])[],
  messages: { success?: string; error: string },
) {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: fn,
    onSuccess: () => {
      keys.forEach((k) => void qc.invalidateQueries({ queryKey: k }));
      if (messages.success) toast.success(messages.success, { duration: 2500 });
    },
    onError: onError(messages.error),
  });
}

export const useCreateLight = () =>
  useInvalidating(api.createLight, [queryKeys.lights(), queryKeys.rooms(), queryKeys.hardwareOutputs()], {
    success: "Light added",
    error: "Could not add the light",
  });
export const useUpdateLight = () =>
  useInvalidating(
    ({ id, patch }: { id: string; patch: Parameters<typeof api.updateLight>[1] }) => api.updateLight(id, patch),
    [queryKeys.lights(), queryKeys.rooms(), queryKeys.hardwareOutputs()],
    { success: "Light saved", error: "Could not save the light" },
  );
export const useDeleteLight = () =>
  useInvalidating(api.deleteLight, [queryKeys.lights(), queryKeys.rooms(), queryKeys.scenes(), queryKeys.groups()], {
    success: "Light removed",
    error: "Could not remove the light",
  });

/* ---------------------------------------------------------------- rooms */
export const useRooms = () => useQuery({ queryKey: queryKeys.rooms(), queryFn: api.rooms });
export const useCreateRoom = () =>
  useInvalidating(api.createRoom, [queryKeys.rooms(), queryKeys.lights()], { success: "Room created", error: "Could not create the room" });
export const useUpdateRoom = () =>
  useInvalidating(
    ({ id, room }: { id: string; room: Parameters<typeof api.updateRoom>[1] }) => api.updateRoom(id, room),
    [queryKeys.rooms(), queryKeys.lights()],
    { success: "Room saved", error: "Could not save the room" },
  );
export const useDeleteRoom = () =>
  useInvalidating(api.deleteRoom, [queryKeys.rooms(), queryKeys.lights()], { success: "Room deleted", error: "Could not delete the room" });
export const useRoomPower = () =>
  useMutation({
    mutationFn: ({ id, power }: { id: string; power: boolean }) => api.roomPower(id, power),
    onError: onError("Room command failed"),
  });
export const useRoomBrightness = () =>
  useMutation({
    mutationFn: ({ id, brightness }: { id: string; brightness: number }) => api.roomBrightness(id, brightness, 150),
    onError: onError("Room command failed"),
  });

/* --------------------------------------------------------------- groups */
export const useGroups = () => useQuery({ queryKey: queryKeys.groups(), queryFn: api.groups });

/* --------------------------------------------------------------- scenes */
export const useScenes = () => useQuery({ queryKey: queryKeys.scenes(), queryFn: api.scenes });
export const useActivateScene = () =>
  useMutation({
    mutationFn: (id: string) => api.activateScene(id),
    onError: onError("Scene could not be activated"),
  });
export const useCaptureScene = () =>
  useInvalidating(({ name, lightIds }: { name: string; lightIds?: string[] }) => api.captureScene(name, lightIds), [queryKeys.scenes()], {
    success: "Scene saved",
    error: "Could not save the scene",
  });
export const useCreateScene = () =>
  useInvalidating(api.createScene, [queryKeys.scenes()], { success: "Scene created", error: "Could not create the scene" });
export const useUpdateScene = () =>
  useInvalidating(
    ({ id, scene }: { id: string; scene: Parameters<typeof api.updateScene>[1] }) => api.updateScene(id, scene),
    [queryKeys.scenes()],
    { success: "Scene saved", error: "Could not save the scene" },
  );
export const useDeleteScene = () =>
  useInvalidating(api.deleteScene, [queryKeys.scenes()], { success: "Scene deleted", error: "Could not delete the scene" });
export const useDuplicateScene = () =>
  useInvalidating(api.duplicateScene, [queryKeys.scenes()], { success: "Scene duplicated", error: "Could not duplicate the scene" });

/* ------------------------------------------------------------- hardware */
export const useBoards = () => useQuery({ queryKey: queryKeys.boards(), queryFn: api.boards });
export const useBoard = (id: string | null) =>
  useQuery({ queryKey: queryKeys.board(id ?? ""), queryFn: () => api.board(id ?? ""), enabled: !!id });
export const useBoardPins = (id: string | null) =>
  useQuery({ queryKey: queryKeys.boardPins(id ?? ""), queryFn: () => api.boardPins(id ?? ""), enabled: !!id });
export const useHardwareStatus = (poll = false) =>
  useQuery({ queryKey: queryKeys.hardwareStatus(), queryFn: api.hardwareStatus, refetchInterval: poll ? 3000 : false });
export const useHardwareOutputs = () => useQuery({ queryKey: queryKeys.hardwareOutputs(), queryFn: api.hardwareOutputs });

/* -------------------------------------------------------------- network */
export const useNetwork = () => useQuery({ queryKey: queryKeys.network(), queryFn: api.network });
export const useNetworkStatus = () =>
  useQuery({ queryKey: queryKeys.networkStatus(), queryFn: api.networkStatus, refetchInterval: 5000 });
