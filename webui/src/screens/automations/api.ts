// Automation API (kept out of the initial bundle; loaded with the screen).
import { z } from "zod";

import { request } from "@/services/api";
import * as A from "./model";

export const automationApi = {
  list: () => request("GET", "/automations", z.array(A.AutomationSummary)),
  get: (id: string) => request("GET", `/automations/${encodeURIComponent(id)}`, A.AutomationDoc),
  create: (doc: A.AutomationDocT) => request("POST", "/automations", A.AutomationSummary, doc),
  update: (id: string, doc: A.AutomationDocT) => request("PUT", `/automations/${encodeURIComponent(id)}`, A.AutomationSummary, doc),
  remove: (id: string) => request("DELETE", `/automations/${encodeURIComponent(id)}`, z.object({ id: z.string() })),
  enable: (id: string, enabled: boolean) =>
    request("POST", `/automations/${encodeURIComponent(id)}/${enabled ? "enable" : "disable"}`, A.AutomationSummary),
  test: (id: string) => request("POST", `/automations/${encodeURIComponent(id)}/test`, z.object({ started: z.boolean() }).passthrough()),
  cancel: (id: string) => request("POST", `/automations/${encodeURIComponent(id)}/cancel`, z.unknown()),
  history: (id?: string) =>
    request("GET", id ? `/automations/${encodeURIComponent(id)}/history` : "/automations/history", z.array(A.HistoryEntry)),
  settings: () => request("GET", "/automations/settings", A.AutomationSettings),
  setLocation: (latitude: number, longitude: number) => request("PUT", "/automations/settings", z.unknown(), { latitude, longitude }),
  variables: () => request("GET", "/automations/variables", z.record(z.string(), z.number())),
};

