import { Badge } from "@/components/ui/badge";
import { ThemeToggle } from "@/components/ThemeToggle";
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from "@/components/ui/card";
import { Separator } from "@/components/ui/separator";

// Phase 0 landing — the scaffold itself. Product screens arrive in Phases 6+;
// keep this honest and minimal (AGENTS §14: no placeholder features).
export default function App() {
  return (
    <div className="mx-auto flex min-h-svh w-full max-w-2xl flex-col gap-6 p-6">
      <header className="flex items-center justify-between gap-3">
        <div className="flex items-center gap-3">
          <h1 className="text-2xl font-semibold">Lumen</h1>
          <Badge variant="secondary">Phase 0</Badge>
        </div>
        <ThemeToggle />
      </header>

      <Separator />

      <Card>
        <CardHeader>
          <CardTitle className="text-lg">Foundation scaffold</CardTitle>
          <CardDescription>
            Firmware, host tests, schemas and this web UI build from day one.
          </CardDescription>
        </CardHeader>
        <CardContent>
          <ul className="list-inside list-disc space-y-1 text-sm text-muted-foreground">
            <li>ESP-IDF firmware builds and runs on the reference board</li>
            <li>Host unit tests run via PlatformIO</li>
            <li>JSON schemas validate with fixtures</li>
            <li>Web UI: Vite + React + strict TypeScript + Tailwind v4 + shadcn/ui</li>
          </ul>
          <p className="mt-4 text-sm text-muted-foreground">
            Product screens ship in Phases 6+ — see{" "}
            <code className="font-mono text-xs">.opencode/plans/</code>.
          </p>
        </CardContent>
      </Card>
    </div>
  );
}
