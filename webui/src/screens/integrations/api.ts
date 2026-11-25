// Integrations API (loaded with the screen, kept out of the initial bundle).
import { z } from "zod";

import { request } from "@/services/api";

export const MqttStatus = z.object({
  connected: z.boolean(),
  broker: z.string(),
  lastError: z.string(),
  published: z.number(),
  received: z.number(),
  rejected: z.number(),
  reconnects: z.number(),
  dropped: z.number().default(0),
});

export const MqttSettings = z.object({
  enabled: z.boolean(),
  host: z.string(),
  port: z.number().int(),
  tls: z.boolean(),
  username: z.string(),
  hasPassword: z.boolean(),
  clientId: z.string(),
  topicPrefix: z.string(),
  controllerId: z.string(),
  qos: z.number().int(),
  keepalive: z.number().int(),
  retain: z.boolean(),
  homeAssistant: z.object({ enabled: z.boolean(), prefix: z.string() }),
  status: MqttStatus,
});

export const ControlSettings = z.object({
  tcpEnabled: z.boolean(),
  tcpPort: z.number().int(),
  udpEnabled: z.boolean(),
  udpControl: z.boolean(),
  udpAnnounce: z.boolean(),
  udpPort: z.number().int(),
  status: z.object({
    tcpClients: z.number(),
    tcpCommands: z.number(),
    tcpAuthFailures: z.number(),
    udpCommands: z.number(),
    udpRejected: z.number(),
    discoveryReplies: z.number(),
  }),
});

export const Integrations = z.object({ mqtt: MqttSettings, control: ControlSettings, baseTopic: z.string() });
export type IntegrationsT = z.infer<typeof Integrations>;
export type ControlSettingsT = z.infer<typeof ControlSettings>;

// Same rules as the firmware (mqtt_service.cpp valid_prefix).
const topicPart = z
  .string()
  .min(1, "Required")
  .max(32, "At most 32 characters")
  .regex(/^[^/+#\s]+$/, "No '/', '+', '#' or spaces");

export const MqttForm = z
  .object({
    enabled: z.boolean(),
    host: z.string().max(128).trim(),
    port: z.number({ error: "Port 1–65535" }).int().min(1, "Port 1–65535").max(65535, "Port 1–65535"),
    tls: z.boolean(),
    username: z.string().max(63),
    password: z.string().max(128),
    clientId: z.string().max(63),
    topicPrefix: topicPart,
    controllerId: z.union([z.literal(""), topicPart]),
    qos: z.union([z.literal(0), z.literal(1)]),
    keepalive: z.number({ error: "10–3600 s" }).int().min(10, "10–3600 s").max(3600, "10–3600 s"),
    retain: z.boolean(),
  })
  .refine((v) => !v.enabled || v.host.length > 0, { path: ["host"], message: "Broker address is required" });
export type MqttFormT = z.infer<typeof MqttForm>;

export const integrationsApi = {
  get: () => request("GET", "/integrations", Integrations),
  updateMqtt: (body: Partial<MqttFormT>) => request("PUT", "/integrations/mqtt", Integrations, body),
  testMqtt: () => request("POST", "/integrations/mqtt/test", z.object({ connected: z.boolean(), message: z.string() })),
  updateHa: (body: { enabled?: boolean; prefix?: string }) => request("PUT", "/integrations/homeassistant", Integrations, body),
  updateControl: (body: Partial<Omit<ControlSettingsT, "status">>) => request("PUT", "/integrations/tcp", Integrations, body),
  token: () => request("GET", "/integrations/token", z.object({ token: z.string() })),
  regenerateToken: () => request("POST", "/integrations/token/regenerate", z.object({ token: z.string() })),
};
