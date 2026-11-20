"use client";

import * as React from "react";
import { cn } from "@/lib/utils";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { Badge } from "@/components/ui/badge";
import { Separator } from "@/components/ui/separator";
import { Alert, AlertDescription } from "@/components/ui/alert";
import { Skeleton } from "@/components/ui/skeleton";
import { Tabs, TabsList, TabsTrigger, TabsContent } from "@/components/ui/tabs";
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from "@/components/ui/table";
import {
  AlertDialog,
  AlertDialogAction,
  AlertDialogCancel,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogTitle,
} from "@/components/ui/alert-dialog";
import { PinChip } from "@/components/hardware/PinChip";
import {
  useBoards,
  useActiveBoard,
  useHardwareStatus,
  useValidateBoard,
  useApplyBoard,
  useCreateBoard,
  useUpdateBoard,
  useDeleteBoard,
  useI2cScan,
} from "@/services/queries";
import {
  mockBoardCandidates,
  mockActiveBoard,
  mockBoardPins,
  mockHardwareStatus,
  mockI2cScan,
} from "@/lib/hardwareMock";
import type { BoardProfile, BoardPin, BoardCandidate, ValidationReport, I2cScanResult } from "@/schemas";

type HwTab = "board" | "pinmap" | "peripherals" | "outputs" | "inputs" | "diagnostics";

const HW_TABS: { id: HwTab; label: string }[] = [
  { id: "board", label: "Board" },
  { id: "pinmap", label: "Pin Mapping" },
  { id: "peripherals", label: "Peripherals" },
  { id: "outputs", label: "Outputs" },
  { id: "inputs", label: "Inputs" },
  { id: "diagnostics", label: "Diagnostics" },
];

// Apply workflow state machine (DESIGN §7.8).
type ApplyState =
  | { phase: "idle" }
  | { phase: "validating" }
  | { phase: "review"; report: ValidationReport }
  | { phase: "confirm" }
  | { phase: "applying" }
  | { phase: "success"; board: string }
  | { phase: "rolled_back"; message: string };

export function Hardware() {
  const [tab, setTab] = React.useState<HwTab>("board");
  const [selectedCandidate, setSelectedCandidate] = React.useState<string | null>(null);
  const [pins, setPins] = React.useState<BoardPin[]>(mockBoardPins);
  const [validateReport, setValidateReport] = React.useState<ValidationReport | null>(null);
  const [apply, setApply] = React.useState<ApplyState>({ phase: "idle" });

  // Queries with mock fallback (mock mode — DESIGN §14.4).
  const boardsQuery = useBoards();
  const activeQuery = useActiveBoard();
  const statusQuery = useHardwareStatus();

  const boards: BoardCandidate[] = boardsQuery.data ?? mockBoardCandidates;
  const activeBoard: BoardProfile = activeQuery.data ?? mockActiveBoard;
  const status = statusQuery.data ?? mockHardwareStatus;

  const validateBoard = useValidateBoard();
  const applyBoard = useApplyBoard();
  const createBoard = useCreateBoard();
  const updateBoard = useUpdateBoard();
  const deleteBoard = useDeleteBoard();
  const i2cScan = useI2cScan();

  const conflicts = pins.filter((p) => p.conflict);
  const candidate = boards.find((b) => b.id === selectedCandidate) ?? null;

  // Map ValidationReport entries onto pin conflicts for inline rendering.
  const runValidation = () => {
    setApply({ phase: "validating" });
    validateBoard.mutate(activeBoard, {
      onSuccess: (report) => {
        setValidateReport(report);
        // Seed a conflict from the mock grid so inline errors are visible in mock mode.
        const conflictEntries = report.errors.filter((e) => e.id?.startsWith("gpio"));
        setPins((prev) =>
          prev.map((p) => {
            const hit = conflictEntries.find((e) => e.id === `gpio${p.gpio}`);
            if (hit) return { ...p, conflict: true, reason: hit.message };
            return p.conflict ? p : { ...p, conflict: false, reason: undefined };
          })
        );
        setApply({ phase: "review", report });
        setTab("pinmap");
      },
      onError: () => {
        // Fallback in mock mode: use the seeded mock conflict.
        const report: ValidationReport = {
          errors: [
            { code: "kDuplicateAssignment", id: "gpio26", message: "Duplicate or reserved GPIO assignment detected on GPIO 26.", is_error: true },
          ],
          warnings: [],
        };
        setValidateReport(report);
        setApply({ phase: "review", report });
        setTab("pinmap");
      },
    });
  };

  const confirmApply = () => {
    const targetId = candidate?.id ?? activeBoard.id;
    setApply({ phase: "applying" });
    applyBoard.mutate(targetId, {
      onSuccess: (result) => {
        if (result.rolled_back) {
          setApply({ phase: "rolled_back", message: result.message ?? "Apply failed and was rolled back." });
        } else {
          setApply({ phase: "success", board: result.active_board ?? targetId });
        }
      },
      onError: () => {
        setApply({ phase: "rolled_back", message: "Apply failed: no response from controller." });
      },
    });
  };

  const runI2cScan = () => {
    i2cScan.mutate(undefined, {
      onError: () => {
        // mock handled by render fallback
      },
    });
  };

  const scanResult: I2cScanResult | null = i2cScan.data ?? null;
  const unexpected = scanResult?.devices.filter((d) => d.unexpected) ?? [];

  return (
    <div className="space-y-6">
      {/* Header */}
      <div className="flex flex-col gap-4 sm:flex-row sm:items-center sm:justify-between">
        <div>
          <h1 className="text-2xl font-bold" style={{ margin: 0 }}>Hardware Abstraction Layer</h1>
          <p className="text-sm text-muted-foreground">
            Board profiles, MCU GPIO validation, I2C peripherals, and real-time diagnostics
          </p>
        </div>
        <Button onClick={runValidation} disabled={apply.phase === "validating"}>
          {apply.phase === "validating" ? "Validating…" : "Validate & Apply Profile"}
        </Button>
      </div>

      {/* Conflict summary banner (shown when conflicts exist) */}
      {conflicts.length > 0 && (
        <Alert variant="destructive">
          <AlertDescription>
            <strong>PinValidator Error:</strong> Duplicate or reserved GPIO assignment detected on{" "}
            {conflicts.map((c) => `GPIO ${c.gpio}`).join(", ")}.
          </AlertDescription>
        </Alert>
      )}

      {/* 6 Hardware Sub-Tabs */}
      <Tabs value={tab} onValueChange={(v) => setTab(v as HwTab)}>
        <TabsList className="flex flex-wrap">
          {HW_TABS.map((t) => (
            <TabsTrigger key={t.id} value={t.id}>
              {t.label}
            </TabsTrigger>
          ))}
        </TabsList>

        {/* ============ Board ============ */}
        <TabsContent value="board" className="space-y-4">
          <Card>
            <CardHeader>
              <div className="flex items-center gap-2">
                <CardTitle className="text-lg">Active Profile</CardTitle>
                <Badge variant="secondary" className="font-mono">{activeBoard.mcu.toUpperCase()}</Badge>
              </div>
            </CardHeader>
            <CardContent className="space-y-3">
              <h2 className="text-xl font-semibold">{activeBoard.name}</h2>
              <p className="text-sm text-muted-foreground">{activeBoard.description ?? "—"}</p>
              <div className="flex flex-wrap gap-2">
                <Button variant="secondary" size="sm" onClick={() => createBoard.mutate({ ...activeBoard, id: `${activeBoard.id}_copy`, name: `${activeBoard.name} (copy)` })}>
                  Duplicate
                </Button>
                <Button variant="secondary" size="sm" onClick={() => exportBoardJson(activeBoard)}>
                  Export JSON
                </Button>
              </div>
            </CardContent>
          </Card>

          <Card>
            <CardHeader>
              <CardTitle className="text-lg">Available Profiles</CardTitle>
            </CardHeader>
            <CardContent>
              <div className="grid gap-2 sm:grid-cols-2">
                {boards.map((b) => {
                  const isActive = b.id === activeBoard.id;
                  const isSelected = b.id === selectedCandidate;
                  return (
                    <div
                      key={b.id}
                      className={cn(
                        "flex items-center justify-between rounded-md border p-3",
                        isSelected ? "border-primary ring-2 ring-ring" : "border-border"
                      )}
                    >
                      <div className="min-w-0">
                        <div className="flex items-center gap-2">
                          <span className="truncate font-medium">{b.name}</span>
                          {b.builtIn && (
                            <Badge variant="outline" className="gap-1 text-[10px]">
                              <span aria-hidden>🔒</span> Built-in
                            </Badge>
                          )}
                        </div>
                        <p className="truncate text-xs text-muted-foreground">{b.desc}</p>
                      </div>
                      <Button
                        size="sm"
                        variant={isActive ? "secondary" : "default"}
                        disabled={isActive}
                        onClick={() => setSelectedCandidate(b.id)}
                      >
                        {isActive ? "Active" : isSelected ? "Selected" : "Load Candidate"}
                      </Button>
                    </div>
                  );
                })}
              </div>
            </CardContent>
          </Card>
        </TabsContent>

        {/* ============ Pin Mapping ============ */}
        <TabsContent value="pinmap" className="space-y-4">
          <div className="flex items-center justify-between">
            <h3 className="text-base font-medium">{activeBoard.name} — GPIO Resource Map</h3>
            <Button variant="secondary" size="sm" onClick={runValidation}>
              Run POST /boards/validate
            </Button>
          </div>

          {validateReport && validateReport.errors.length > 0 && (
            <Alert variant="destructive">
              <AlertDescription>
                <ul className="list-inside list-disc space-y-1">
                  {validateReport.errors.map((e, i) => (
                    <li key={i}>
                      <span className="font-mono text-xs">{e.code}</span> — {e.message}
                    </li>
                  ))}
                </ul>
              </AlertDescription>
            </Alert>
          )}

          <div className="grid grid-cols-[repeat(auto-fill,minmax(155px,1fr))] gap-2.5">
            {pins.map((pin) => (
              <PinChip
                key={pin.gpio}
                pin={pin}
                onAssign={(gpio) => setSelectedCandidate(`gpio${gpio}`)}
              />
            ))}
          </div>
        </TabsContent>

        {/* ============ Peripherals ============ */}
        <TabsContent value="peripherals" className="space-y-4">
          <Card>
            <CardHeader>
              <div className="flex items-center justify-between">
                <CardTitle className="text-base">I2C Bus 0 (SDA: GPIO 4, SCL: GPIO 16)</CardTitle>
                <Button size="sm" onClick={runI2cScan} disabled={i2cScan.isPending}>
                  {i2cScan.isPending ? "Scanning…" : "Scan I2C"}
                </Button>
              </div>
            </CardHeader>
            <CardContent className="space-y-3">
              {i2cScan.isPending && <Skeleton className="h-16 w-full" />}
              {scanResult && (
                <div className="flex flex-wrap gap-2">
                  {scanResult.devices.map((d) => (
                    <Badge
                      key={d.address}
                      variant={d.unexpected ? "destructive" : d.known ? "secondary" : "outline"}
                      className="font-mono"
                    >
                      {d.address_hex} {d.label}
                    </Badge>
                  ))}
                </div>
              )}
              {unexpected.length > 0 && (
                <Alert variant="destructive">
                  <AlertDescription>
                    Unexpected address{unexpected.length > 1 ? "es" : ""}: {unexpected.map((d) => d.address_hex).join(", ")} — not in the active profile.
                  </AlertDescription>
                </Alert>
              )}
              {!scanResult && !i2cScan.isPending && (
                <p className="text-sm text-muted-foreground">Run a scan to enumerate devices on the bus.</p>
              )}
            </CardContent>
          </Card>

          <Card>
            <CardHeader>
              <CardTitle className="text-base">Declared Peripherals</CardTitle>
            </CardHeader>
            <CardContent>
              <Table>
                <TableHeader>
                  <TableRow>
                    <TableHead>Peripheral ID</TableHead>
                    <TableHead>Type</TableHead>
                    <TableHead>Bus / Address</TableHead>
                    <TableHead>Channels</TableHead>
                    <TableHead>Status</TableHead>
                  </TableRow>
                </TableHeader>
                <TableBody>
                  {activeBoard.peripherals.map((p) => (
                    <TableRow key={p.id}>
                      <TableCell className="font-medium">{p.id}</TableCell>
                      <TableCell>{p.type}</TableCell>
                      <TableCell className="font-mono text-xs">
                        {p.bus ?? "—"}
                        {p.address != null && p.address >= 0 ? ` @ 0x${p.address.toString(16)}` : ""}
                      </TableCell>
                      <TableCell>{p.channels ?? "—"}</TableCell>
                      <TableCell>
                        {p.unverified ? (
                          <Badge variant="outline" className="text-warning">unverified</Badge>
                        ) : (
                          <Badge variant="secondary">ok</Badge>
                        )}
                      </TableCell>
                    </TableRow>
                  ))}
                </TableBody>
              </Table>
            </CardContent>
          </Card>
        </TabsContent>

        {/* ============ Outputs ============ */}
        <TabsContent value="outputs" className="space-y-4">
          <Card>
            <CardHeader>
              <CardTitle className="text-base">Hardware Outputs Direct Test</CardTitle>
            </CardHeader>
            <CardContent>
              {statusQuery.isLoading ? (
                <Skeleton className="h-24 w-full" />
              ) : (
                <Table>
                  <TableHeader>
                    <TableRow>
                      <TableHead>Output</TableHead>
                      <TableHead>Driver</TableHead>
                      <TableHead>State</TableHead>
                      <TableHead>Value</TableHead>
                      <TableHead className="text-right">Test</TableHead>
                    </TableRow>
                  </TableHeader>
                  <TableBody>
                    {(status.outputs ?? []).map((o) => (
                      <TableRow key={o.id}>
                        <TableCell className="font-medium">{o.name}</TableCell>
                        <TableCell className="text-xs text-muted-foreground">{o.driver}</TableCell>
                        <TableCell>
                          <Badge variant={o.state ? "default" : "outline"}>{o.state ? "ON" : "OFF"}</Badge>
                        </TableCell>
                        <TableCell>{o.value != null ? `${o.value}%` : "—"}</TableCell>
                        <TableCell className="text-right">
                          <Button size="xs" variant="outline">Toggle</Button>
                        </TableCell>
                      </TableRow>
                    ))}
                  </TableBody>
                </Table>
              )}
            </CardContent>
          </Card>
        </TabsContent>

        {/* ============ Inputs ============ */}
        <TabsContent value="inputs" className="space-y-4">
          <Card>
            <CardHeader>
              <CardTitle className="text-base">Raw Hardware Digital Inputs</CardTitle>
            </CardHeader>
            <CardContent>
              {statusQuery.isLoading ? (
                <Skeleton className="h-24 w-full" />
              ) : (
                <Table>
                  <TableHeader>
                    <TableRow>
                      <TableHead>Input</TableHead>
                      <TableHead>Source</TableHead>
                      <TableHead>State</TableHead>
                      <TableHead>Last Event</TableHead>
                    </TableRow>
                  </TableHeader>
                  <TableBody>
                    {(status.inputs ?? []).map((i) => (
                      <TableRow key={i.id}>
                        <TableCell className="font-medium">{i.name}</TableCell>
                        <TableCell className="text-xs text-muted-foreground">{i.source}</TableCell>
                        <TableCell>
                          <Badge variant={i.state ? "default" : "outline"}>{i.state ? "HIGH" : "LOW"}</Badge>
                        </TableCell>
                        <TableCell className="text-xs text-muted-foreground">{i.last_event ?? "—"}</TableCell>
                      </TableRow>
                    ))}
                  </TableBody>
                </Table>
              )}
            </CardContent>
          </Card>
        </TabsContent>

        {/* ============ Diagnostics ============ */}
        <TabsContent value="diagnostics" className="space-y-4">
          <div className="grid gap-4 sm:grid-cols-2 lg:grid-cols-4">
            <Stat label="Free Heap" value={formatBytes(status.free_heap)} />
            <Stat label="Min Free Heap" value={formatBytes(status.min_free_heap)} />
            <Stat label="MCU" value={status.mcu?.toUpperCase() ?? "—"} />
            <Stat label="Ethernet Link" value={status.ethernet_link ? "UP" : "DOWN"} />
          </div>
          <Card>
            <CardHeader>
              <CardTitle className="text-base">Peripheral Health</CardTitle>
            </CardHeader>
            <CardContent className="space-y-2">
              {Object.entries(status.pcf8574_health ?? {}).map(([id, errs]) => (
                <div key={id} className="flex items-center justify-between text-sm">
                  <span className="font-mono text-xs">{id}</span>
                  <Badge variant={errs === 0 ? "secondary" : "destructive"}>{errs === 0 ? "healthy" : `${errs} errors`}</Badge>
                </div>
              ))}
              <div className="flex items-center justify-between text-sm">
                <span className="font-mono text-xs">pca9685_0</span>
                <Badge variant={status.pca9685_present ? "secondary" : "destructive"}>
                  {status.pca9685_present ? "present" : "missing"}
                </Badge>
              </div>
            </CardContent>
          </Card>
        </TabsContent>
      </Tabs>

      {/* ============ Apply workflow: review confirm ============ */}
      <AlertDialog open={apply.phase === "confirm"} onOpenChange={(o) => !o && setApply({ phase: "idle" })}>
        <AlertDialogContent>
          <AlertDialogHeader>
            <AlertDialogTitle>Apply board profile?</AlertDialogTitle>
            <AlertDialogDescription>
              Applying this profile stops affected services and drives outputs to their configured
              safe states before re-initialising hardware. If anything fails, the previous profile
              is restored automatically (rollback). Target:{" "}
              <span className="font-mono">{candidate?.id ?? activeBoard.id}</span>.
            </AlertDialogDescription>
          </AlertDialogHeader>
          <AlertDialogFooter>
            <AlertDialogCancel onClick={() => setApply({ phase: "idle" })}>Cancel</AlertDialogCancel>
            <AlertDialogAction onClick={confirmApply}>Apply</AlertDialogAction>
          </AlertDialogFooter>
        </AlertDialogContent>
      </AlertDialog>

      {/* review → confirm trigger */}
      {apply.phase === "review" && (
        <Card className="border-primary">
          <CardHeader>
            <CardTitle className="text-base">Validation result</CardTitle>
          </CardHeader>
          <CardContent className="space-y-3">
            <p className="text-sm">
              {apply.report.errors.length} error{apply.report.errors.length === 1 ? "" : "s"},{" "}
              {apply.report.warnings.length} warning{apply.report.warnings.length === 1 ? "" : "s"}.
            </p>
            <div className="flex gap-2">
              <Button
                onClick={() => setApply({ phase: "confirm" })}
                disabled={apply.report.errors.length > 0}
              >
                {apply.report.errors.length > 0 ? "Fix errors to apply" : "Review & Apply"}
              </Button>
              <Button variant="ghost" onClick={() => setApply({ phase: "idle" })}>Dismiss</Button>
            </div>
          </CardContent>
        </Card>
      )}

      {/* applying / success / rolled_back feedback */}
      {apply.phase === "applying" && (
        <Alert><AlertDescription>Applying profile… outputs moving to safe states.</AlertDescription></Alert>
      )}
      {apply.phase === "success" && (
        <Alert><AlertDescription>Profile <span className="font-mono">{apply.board}</span> applied successfully.</AlertDescription></Alert>
      )}
      {apply.phase === "rolled_back" && (
        <Alert variant="destructive"><AlertDescription>{apply.message} The previous profile is still active.</AlertDescription></Alert>
      )}
    </div>
  );
}

function Stat({ label, value }: { label: string; value: string }) {
  return (
    <Card>
      <CardContent className="pt-6">
        <p className="text-xs text-muted-foreground">{label}</p>
        <p className="text-xl font-semibold tabular-nums">{value}</p>
      </CardContent>
    </Card>
  );
}

function formatBytes(n: number | undefined): string {
  if (n == null) return "—";
  if (n >= 1024 * 1024) return `${(n / 1024 / 1024).toFixed(1)} MB`;
  if (n >= 1024) return `${(n / 1024).toFixed(0)} KB`;
  return `${n} B`;
}

function exportBoardJson(profile: BoardProfile) {
  const blob = new Blob([JSON.stringify(profile, null, 2)], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = `${profile.id}.json`;
  a.click();
  URL.revokeObjectURL(url);
}