// TanStack Query hooks + queryKeys — single source of truth for REST cache.
// DESIGN §8.3: staleTime rules; mutations invalidate, light commands rely on WS confirmation.
import { QueryClient, useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import {
  authApi,
  systemApi,
  lightsApi,
  roomsApi,
  groupsApi,
  scenesApi,
  boardsApi,
  hardwareApi,
  ApiError,
} from "@/services/api";

import type {
  LightChannel,
  Room,
  Group,
  Scene,
  LightCommandResponse,
  BoardProfile,
  BoardCandidate,
  ValidationReport,
  ApplyBoardResult,
  HardwareStatus,
  I2cScanResult,
} from "@/schemas";

/* ================================================================
   Query Keys (single source of truth)
   ================================================================ */
export const queryKeys = {
  // Auth
  session: () => ["auth", "session"] as const,

  // System
  systemInfo: () => ["system", "info"] as const,
  health: () => ["system", "health"] as const,

  // Lights
  lights: () => ["lights"] as const,
  light: (id: string) => ["lights", id] as const,

  // Rooms
  rooms: () => ["rooms"] as const,
  room: (id: string) => ["rooms", id] as const,

  // Groups
  groups: () => ["groups"] as const,
  group: (id: string) => ["groups", id] as const,

  // Scenes
  scenes: () => ["scenes"] as const,
  scene: (id: string) => ["scenes", id] as const,

  // Boards
  boards: () => ["boards"] as const,
  board: (id: string) => ["boards", id] as const,
  boardActive: () => ["boards", "active"] as const,

  // Hardware
  hardwareStatus: () => ["hardware", "status"] as const,
  hardwareInputs: () => ["hardware", "inputs"] as const,
  i2cScan: () => ["hardware", "i2c", "scan"] as const,
} as const;

/* ================================================================
   Query Client (singleton for providers)
   ================================================================ */
export function createQueryClient() {
  return new QueryClient({
    defaultOptions: {
      queries: {
        staleTime: 30_000, // 30s — DESIGN §8.3
        refetchOnWindowFocus: false,
        retry: (failureCount: number, error: unknown) => {
          if (error instanceof ApiError && error.status === 401) return false;
          return failureCount < 3;
        },
      },
    },
  });
}

/* ================================================================
   Auth Hooks
   ================================================================ */
export function useSession() {
  return useQuery({
    queryKey: queryKeys.session(),
    queryFn: authApi.session,
    retry: false,
    staleTime: 5 * 60 * 1000, // 5 min
  });
}

/* ================================================================
   System Hooks
   ================================================================ */
export function useSystemInfo() {
  return useQuery({
    queryKey: queryKeys.systemInfo(),
    queryFn: systemApi.info,
    staleTime: 60_000,
  });
}

export function useHealth() {
  return useQuery({
    queryKey: queryKeys.health(),
    queryFn: systemApi.health,
    staleTime: 30_000,
  });
}

/* ================================================================
   Lights Hooks
   ================================================================ */
export function useLights() {
  return useQuery({
    queryKey: queryKeys.lights(),
    queryFn: lightsApi.list,
    staleTime: 10_000, // 10s — lights change frequently
  });
}

export function useLight(id: string) {
  return useQuery({
    queryKey: queryKeys.light(id),
    queryFn: () => lightsApi.get(id),
    enabled: !!id,
    staleTime: 5_000,
  });
}

interface LightCommandVariables { id: string; command: Record<string, unknown> }

export function useLightCommand() {
  const queryClient = useQueryClient();
  return useMutation<LightCommandResponse, Error, LightCommandVariables>({
    mutationFn: ({ id, command }) => lightsApi.command(id, command),
    onSuccess: (_data, variables) => {
      // Optimistic update via WS will handle; just invalidate
      queryClient.invalidateQueries({ queryKey: queryKeys.light(variables.id) });
      queryClient.invalidateQueries({ queryKey: queryKeys.lights() });
    },
  });
}

interface LightCreateVariables { id: string; name: string; type: LightChannel["type"] }
interface LightUpdateVariables { id: string; updates: Partial<LightChannel> }

export function useCreateLight() {
  const queryClient = useQueryClient();
  return useMutation<LightChannel, Error, LightCreateVariables>({
    mutationFn: ({ id, name, type }) => lightsApi.create({ id, name, type }),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: queryKeys.lights() });
    },
  });
}

export function useUpdateLight() {
  const queryClient = useQueryClient();
  return useMutation<LightChannel, Error, LightUpdateVariables>({
    mutationFn: ({ id, updates }) => lightsApi.update(id, updates),
    onSuccess: (_data, variables) => {
      queryClient.invalidateQueries({ queryKey: queryKeys.light(variables.id) });
      queryClient.invalidateQueries({ queryKey: queryKeys.lights() });
    },
  });
}

export function useDeleteLight() {
  const queryClient = useQueryClient();
  return useMutation<void, Error, string>({
    mutationFn: (id) => lightsApi.delete(id),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: queryKeys.lights() });
    },
  });
}

/* ================================================================
   Rooms Hooks
   ================================================================ */
export function useRooms() {
  return useQuery({
    queryKey: queryKeys.rooms(),
    queryFn: roomsApi.list,
    staleTime: 30_000,
  });
}

export function useRoom(id: string) {
  return useQuery({
    queryKey: queryKeys.room(id),
    queryFn: () => roomsApi.get(id),
    enabled: !!id,
  });
}

export function useCreateRoom() {
  const queryClient = useQueryClient();
  return useMutation({
    mutationFn: roomsApi.create,
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: queryKeys.rooms() });
    },
  });
}

interface UpdateRoomVariables { id: string; updates: Record<string, unknown> }
interface RoomPowerVariables { id: string; power: boolean; transitionMs?: number }
interface RoomBrightnessVariables { id: string; brightness: number; transitionMs?: number }

export function useUpdateRoom() {
  const queryClient = useQueryClient();
  return useMutation<Room, Error, UpdateRoomVariables>({
    mutationFn: ({ id, updates }) => roomsApi.update(id, updates),
    onSuccess: (_data, variables) => {
      queryClient.invalidateQueries({ queryKey: queryKeys.room(variables.id) });
      queryClient.invalidateQueries({ queryKey: queryKeys.rooms() });
    },
  });
}

export function useDeleteRoom() {
  const queryClient = useQueryClient();
  return useMutation<void, Error, string>({
    mutationFn: (id) => roomsApi.delete(id),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: queryKeys.rooms() });
    },
  });
}

export function useRoomPower() {
  const queryClient = useQueryClient();
  return useMutation<void, Error, RoomPowerVariables>({
    mutationFn: ({ id, power, transitionMs }) => roomsApi.setPower(id, power, transitionMs),
    onSuccess: (_data, variables) => {
      queryClient.invalidateQueries({ queryKey: queryKeys.room(variables.id) });
      queryClient.invalidateQueries({ queryKey: queryKeys.rooms() });
    },
  });
}

export function useRoomBrightness() {
  const queryClient = useQueryClient();
  return useMutation<void, Error, RoomBrightnessVariables>({
    mutationFn: ({ id, brightness, transitionMs }) => roomsApi.setBrightness(id, brightness, transitionMs),
    onSuccess: (_data, variables) => {
      queryClient.invalidateQueries({ queryKey: queryKeys.room(variables.id) });
      queryClient.invalidateQueries({ queryKey: queryKeys.rooms() });
    },
  });
}

/* ================================================================
   Groups Hooks
   ================================================================ */
export function useGroups() {
  return useQuery({
    queryKey: queryKeys.groups(),
    queryFn: groupsApi.list,
    staleTime: 30_000,
  });
}

export function useGroup(id: string) {
  return useQuery({
    queryKey: queryKeys.group(id),
    queryFn: () => groupsApi.get(id),
    enabled: !!id,
  });
}

export function useCreateGroup() {
  const queryClient = useQueryClient();
  return useMutation({
    mutationFn: groupsApi.create,
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: queryKeys.groups() });
      queryClient.invalidateQueries({ queryKey: queryKeys.rooms() });
    },
  });
}

interface UpdateGroupVariables { id: string; updates: Record<string, unknown> }
interface GroupCommandVariables { id: string; command: Record<string, unknown> }
interface GroupPowerVariables { id: string; power: boolean; transitionMs?: number }
interface GroupBrightnessVariables { id: string; brightness: number; transitionMs?: number }

interface UpdateSceneVariables { id: string; updates: Record<string, unknown> }
interface SceneActivateVariables { id: string; params?: Record<string, unknown> }

export function useUpdateGroup() {
  const queryClient = useQueryClient();
  return useMutation<Group, Error, UpdateGroupVariables>({
    mutationFn: ({ id, updates }) => groupsApi.update(id, updates),
    onSuccess: (_data, variables) => {
      queryClient.invalidateQueries({ queryKey: queryKeys.group(variables.id) });
      queryClient.invalidateQueries({ queryKey: queryKeys.groups() });
    },
  });
}

export function useDeleteGroup() {
  const queryClient = useQueryClient();
  return useMutation<void, Error, string>({
    mutationFn: (id) => groupsApi.delete(id),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: queryKeys.groups() });
    },
  });
}

export function useGroupCommand() {
  const queryClient = useQueryClient();
  return useMutation<{ accepted: boolean; commandId?: string }, Error, GroupCommandVariables>({
    mutationFn: ({ id, command }) => groupsApi.command(id, command),
    onSuccess: (_data, variables) => {
      queryClient.invalidateQueries({ queryKey: queryKeys.group(variables.id) });
      queryClient.invalidateQueries({ queryKey: queryKeys.groups() });
    },
  });
}

export function useGroupPower() {
  const queryClient = useQueryClient();
  return useMutation<void, Error, GroupPowerVariables>({
    mutationFn: ({ id, power, transitionMs }) => groupsApi.setPower(id, power, transitionMs),
    onSuccess: (_data, variables) => {
      queryClient.invalidateQueries({ queryKey: queryKeys.group(variables.id) });
      queryClient.invalidateQueries({ queryKey: queryKeys.groups() });
    },
  });
}

export function useGroupBrightness() {
  const queryClient = useQueryClient();
  return useMutation<void, Error, GroupBrightnessVariables>({
    mutationFn: ({ id, brightness, transitionMs }) => groupsApi.setBrightness(id, brightness, transitionMs),
    onSuccess: (_data, variables) => {
      queryClient.invalidateQueries({ queryKey: queryKeys.group(variables.id) });
      queryClient.invalidateQueries({ queryKey: queryKeys.groups() });
    },
  });
}

/* ================================================================
   Scenes Hooks
   ================================================================ */
export function useScenes() {
  return useQuery({
    queryKey: queryKeys.scenes(),
    queryFn: scenesApi.list,
    staleTime: 30_000,
  });
}

export function useScene(id: string) {
  return useQuery({
    queryKey: queryKeys.scene(id),
    queryFn: () => scenesApi.get(id),
    enabled: !!id,
  });
}

export function useCreateScene() {
  const queryClient = useQueryClient();
  return useMutation({
    mutationFn: scenesApi.create,
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: queryKeys.scenes() });
    },
  });
}

export function useUpdateScene() {
  const queryClient = useQueryClient();
  return useMutation<Scene, Error, UpdateSceneVariables>({
    mutationFn: ({ id, updates }) => scenesApi.update(id, updates),
    onSuccess: (_data, variables) => {
      queryClient.invalidateQueries({ queryKey: queryKeys.scene(variables.id) });
      queryClient.invalidateQueries({ queryKey: queryKeys.scenes() });
    },
  });
}

export function useDeleteScene() {
  const queryClient = useQueryClient();
  return useMutation<void, Error, string>({
    mutationFn: (id) => scenesApi.delete(id),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: queryKeys.scenes() });
    },
  });
}

export function useActivateScene() {
  const queryClient = useQueryClient();
  return useMutation<void, Error, SceneActivateVariables>({
    mutationFn: ({ id, params }) => scenesApi.activate(id, params),
    onSuccess: () => {
      // Scene activation updates light states via WS
      queryClient.invalidateQueries({ queryKey: queryKeys.lights() });
      queryClient.invalidateQueries({ queryKey: queryKeys.groups() });
    },
  });
}

export function useDuplicateScene() {
  const queryClient = useQueryClient();
  return useMutation<Scene, Error, string>({
    mutationFn: (id) => scenesApi.duplicate(id),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: queryKeys.scenes() });
    },
  });
}
/* ================================================================
   Boards Hooks (Phase 7)
   ================================================================ */
export function useBoards() {
  return useQuery({
    queryKey: queryKeys.boards(),
    queryFn: boardsApi.list,
    staleTime: 60_000,
  });
}

export function useBoard(id: string) {
  return useQuery({
    queryKey: queryKeys.board(id),
    queryFn: () => boardsApi.get(id),
    enabled: !!id,
    staleTime: 30_000,
  });
}

export function useActiveBoard() {
  return useQuery({
    queryKey: queryKeys.boardActive(),
    queryFn: boardsApi.active,
    staleTime: 30_000,
  });
}

export function useValidateBoard() {
  return useMutation<ValidationReport, Error, BoardProfile>({
    mutationFn: (profile) => boardsApi.validate(profile),
  });
}

export function useCreateBoard() {
  const queryClient = useQueryClient();
  return useMutation<BoardProfile, Error, BoardProfile>({
    mutationFn: (profile) => boardsApi.create(profile),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: queryKeys.boards() });
      queryClient.invalidateQueries({ queryKey: queryKeys.boardActive() });
    },
  });
}

export function useUpdateBoard() {
  const queryClient = useQueryClient();
  return useMutation<BoardProfile, Error, { id: string; profile: Partial<BoardProfile> }>({
    mutationFn: ({ id, profile }) => boardsApi.update(id, profile),
    onSuccess: (_data, variables) => {
      queryClient.invalidateQueries({ queryKey: queryKeys.board(variables.id) });
      queryClient.invalidateQueries({ queryKey: queryKeys.boards() });
      queryClient.invalidateQueries({ queryKey: queryKeys.boardActive() });
    },
  });
}

export function useDeleteBoard() {
  const queryClient = useQueryClient();
  return useMutation<void, Error, string>({
    mutationFn: (id) => boardsApi.remove(id),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: queryKeys.boards() });
    },
  });
}

export function useApplyBoard() {
  const queryClient = useQueryClient();
  return useMutation<ApplyBoardResult, Error, string>({
    mutationFn: (id) => boardsApi.apply(id),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: queryKeys.boardActive() });
      queryClient.invalidateQueries({ queryKey: queryKeys.boards() });
      queryClient.invalidateQueries({ queryKey: queryKeys.hardwareStatus() });
    },
  });
}

/* ================================================================
   Hardware Hooks (Phase 7)
   ================================================================ */
export function useHardwareStatus() {
  return useQuery({
    queryKey: queryKeys.hardwareStatus(),
    queryFn: hardwareApi.status,
    staleTime: 5_000,
    refetchInterval: 10_000,
  });
}

export function useHardwareInputs() {
  return useQuery({
    queryKey: queryKeys.hardwareInputs(),
    queryFn: hardwareApi.inputs,
    staleTime: 5_000,
    refetchInterval: 10_000,
  });
}

export function useI2cScan() {
  return useMutation<I2cScanResult, Error, void>({
    mutationFn: () => hardwareApi.i2cScan(),
  });
}
