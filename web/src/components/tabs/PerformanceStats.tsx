import { useEffect, useState } from "react";
import {
  Area,
  AreaChart,
  CartesianGrid,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from "recharts";
import { Panel } from "@/components/shared/Panel";
import { collectPerfStats, type CharStat, type PerfStats } from "@/optimizations/stats";
import { getOptimizationStatus, type OptimizationStatus } from "@/optimizations/state";

// Literal colors are shadow-DOM-safe; the teal matches --primary so the charts
// read as part of the panel.
const TPS_COLOR = "hsl(168 58% 28%)";
const MSPT_COLOR = "#d97706";

// recharts renders its tooltip inside the chart wrapper (in our shadow tree, not
// portaled to document.body), so themed inline styles apply cleanly here.
const TOOLTIP_STYLE = {
  background: "hsl(var(--card))",
  border: "1px solid hsl(var(--border))",
  borderRadius: 6,
  fontSize: 11,
  color: "hsl(var(--foreground))",
  padding: "4px 8px",
} as const;

const TRIGGER_LABEL: Record<string, string> = {
  default: "always on",
  idle: "idle trigger",
  background: "tab in background",
};

function fmt(n: number, digits = 1): string {
  return n.toFixed(digits);
}

export function PerformanceStats() {
  const [stats, setStats] = useState<PerfStats | null>(null);
  const [status, setStatus] = useState<OptimizationStatus | null>(null);

  useEffect(() => {
    const tick = () => {
      setStats(collectPerfStats());
      setStatus(getOptimizationStatus());
    };
    tick();
    const timer = window.setInterval(tick, 1000);
    return () => window.clearInterval(timer);
  }, []);

  return (
    <div className="grid gap-3">
      <StatusBanner status={status} />
      <CharacterCost stats={stats} />
      <FrameTiming stats={stats} />
    </div>
  );
}

function StatusBanner({ status }: { status: OptimizationStatus | null }) {
  let dot = "bg-muted-foreground/50";
  let text = (
    <span className="text-muted-foreground">Loading optimizer status…</span>
  );
  if (status) {
    if (!status.enabled) {
      dot = "bg-muted-foreground/50";
      text = (
        <span className="text-muted-foreground">
          Optimizations are <span className="font-medium text-foreground">off</span> — every hook
          passes through untouched.
        </span>
      );
    } else if (status.activeProfileId) {
      dot = "animate-pulse bg-primary";
      text = (
        <span>
          Optimizing with{" "}
          <span className="font-semibold text-foreground">{status.activeProfileName}</span>
          <span className="ml-1 text-muted-foreground">
            ({TRIGGER_LABEL[status.activeTrigger ?? "default"]})
          </span>
        </span>
      );
    } else {
      dot = "bg-amber-500";
      text = (
        <span className="text-amber-700">
          Enabled, but no rule matches right now — nothing is applied.
        </span>
      );
    }
  }
  return (
    <div className="flex items-center gap-2 rounded-md border bg-card px-3 py-2 text-xs">
      <span className={`h-2 w-2 shrink-0 rounded-full ${dot}`} />
      {text}
    </div>
  );
}

function CharacterCost({ stats }: { stats: PerfStats | null }) {
  const chars = stats?.characters ?? [];
  return (
    <Panel
      title="Render cost by character"
      action={
        chars.length > 0 ? (
          <span className="text-[11px] tabular-nums text-muted-foreground">
            ms per rebuild · {chars.length}
          </span>
        ) : undefined
      }
    >
      {chars.length === 0 ? (
        <p className="py-4 text-center text-xs text-muted-foreground">
          No characters drawn yet — the heaviest renderers show up here as characters render.
          Enable <span className="font-medium">Lazy canvas</span> to also see skipped rebuilds.
        </p>
      ) : (
        <div className="grid gap-2.5">
          {chars.map((c, i) => (
            <CharacterRow key={c.id} c={c} max={chars[0].avgDraw} rank={i + 1} />
          ))}
        </div>
      )}
    </Panel>
  );
}

function CharacterRow({ c, max, rank }: { c: CharStat; max: number; rank: number }) {
  const width = max > 0 ? Math.max(2, (c.avgDraw / max) * 100) : 0;
  const skipPct = Math.round(c.skipRatio * 100);
  return (
    <div className="text-xs">
      <div className="flex items-baseline justify-between gap-2">
        <span className="min-w-0 truncate">
          <span className="mr-1 tabular-nums text-muted-foreground">{rank}.</span>
          {c.label}
        </span>
        <span className="shrink-0 font-semibold tabular-nums text-primary">
          {fmt(c.avgDraw)} ms
        </span>
      </div>
      <div className="my-1 h-1.5 w-full overflow-hidden rounded-full bg-muted">
        <div className="h-full rounded-full bg-primary" style={{ width: `${width}%` }} />
      </div>
      <div className="flex flex-wrap gap-x-2 text-[11px] tabular-nums text-muted-foreground">
        <span>{c.drawTimes} rebuilds</span>
        <span>· {skipPct}% skipped</span>
        <span className="ml-auto">{c.lastSeenSec}s ago</span>
      </div>
    </div>
  );
}

function FrameTiming({ stats }: { stats: PerfStats | null }) {
  const hasFrames = !!stats && stats.windows.some((w) => w.count > 0);
  const w5 = stats?.windows[1];
  const w30 = stats?.windows[2];
  return (
    <Panel
      title="Frame timing"
      action={
        <span className="flex items-center gap-1.5 text-[11px] text-muted-foreground">
          <span className="h-1.5 w-1.5 animate-pulse rounded-full bg-primary" />
          live · 1s
        </span>
      }
    >
      {!hasFrames ? (
        <p className="py-4 text-center text-xs text-muted-foreground">
          No frames recorded yet — waiting for the game to start rendering.
        </p>
      ) : (
        <div className="grid gap-3">
          <div className="grid grid-cols-3 gap-2">
            <StatTile label="FPS" value={fmt(w5?.fps ?? 0, 1)} />
            <StatTile label="ms / frame" value={fmt(w5?.avg ?? 0)} accent={MSPT_COLOR} />
            <StatTile label="Peak ms · 30s" value={fmt(w30?.max ?? 0)} accent={MSPT_COLOR} />
          </div>
          <div className="grid gap-3 @lg:grid-cols-2">
            <TrendChart title="FPS" data={stats!.series} dataKey="fps" color={TPS_COLOR} unit="" />
            <TrendChart title="ms / frame" data={stats!.series} dataKey="frameMs" color={MSPT_COLOR} unit="ms" />
          </div>
        </div>
      )}
    </Panel>
  );
}

function StatTile({ label, value, accent }: { label: string; value: string; accent?: string }) {
  return (
    <div className="rounded-md border bg-background px-2.5 py-1.5">
      <div className="text-[10px] uppercase tracking-wide text-muted-foreground">{label}</div>
      <div className="text-lg font-semibold tabular-nums" style={accent ? { color: accent } : undefined}>
        {value}
      </div>
    </div>
  );
}

function TrendChart({
  title,
  data,
  dataKey,
  color,
  unit,
}: {
  title: string;
  data: PerfStats["series"];
  dataKey: "fps" | "frameMs";
  color: string;
  unit: string;
}) {
  const gradientId = `grad-${dataKey}`;
  return (
    <div>
      <div className="mb-1 text-[11px] font-medium uppercase tracking-wide text-muted-foreground">
        {title}
      </div>
      <div className="h-28 w-full">
        <ResponsiveContainer width="100%" height="100%">
          <AreaChart data={data} margin={{ top: 4, right: 4, bottom: 0, left: -24 }}>
            <defs>
              <linearGradient id={gradientId} x1="0" y1="0" x2="0" y2="1">
                <stop offset="0%" stopColor={color} stopOpacity={0.35} />
                <stop offset="100%" stopColor={color} stopOpacity={0} />
              </linearGradient>
            </defs>
            <CartesianGrid stroke="hsl(var(--border))" strokeDasharray="3 3" vertical={false} />
            <XAxis
              dataKey="agoSec"
              reversed
              type="number"
              domain={[0, 59]}
              ticks={[60, 45, 30, 15, 0]}
              tickFormatter={(v: number) => (v === 0 ? "now" : `-${v}s`)}
              tick={{ fontSize: 10, fill: "hsl(var(--muted-foreground))" }}
              stroke="hsl(var(--border))"
            />
            <YAxis
              width={40}
              allowDecimals={false}
              tick={{ fontSize: 10, fill: "hsl(var(--muted-foreground))" }}
              stroke="hsl(var(--border))"
            />
            <Tooltip
              contentStyle={TOOLTIP_STYLE}
              labelFormatter={(v) => (Number(v) === 0 ? "now" : `${Number(v)}s ago`)}
              formatter={(value) => [`${fmt(Number(value))}${unit}`, title]}
            />
            <Area
              type="monotone"
              dataKey={dataKey}
              stroke={color}
              strokeWidth={1.5}
              fill={`url(#${gradientId})`}
              isAnimationActive={false}
              dot={false}
            />
          </AreaChart>
        </ResponsiveContainer>
      </div>
    </div>
  );
}