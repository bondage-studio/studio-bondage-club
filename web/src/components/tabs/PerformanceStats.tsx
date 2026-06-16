// Live optimization stats for the Optimizations tab. Polls collectPerfStats()
// once a second (the loader runs in the same module graph / shadow DOM, so the
// state is read directly — no RPC) and renders it as recharts time-series plus
// per-character skip-ratio bars. Replaces the old window.tps() console dump.

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
import { collectPerfStats, type PerfStats } from "@/optimizations/stats";

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

function fmt(n: number, digits = 1): string {
  return n.toFixed(digits);
}

export function PerformanceStats() {
  const [stats, setStats] = useState<PerfStats | null>(null);

  useEffect(() => {
    const tick = () => setStats(collectPerfStats());
    tick();
    const timer = window.setInterval(tick, 1000);
    return () => window.clearInterval(timer);
  }, []);

  return (
    <Panel
      title="Live stats"
      action={
        <span className="flex items-center gap-1.5 text-[11px] text-muted-foreground">
          <span className="h-1.5 w-1.5 animate-pulse rounded-full bg-primary" />
          live · 1s
        </span>
      }
    >
      {!stats?.hasData ? (
        <p className="py-6 text-center text-xs text-muted-foreground">
          No data yet. Enter a chat room to record tick timings. Enable{" "}
          <span className="font-medium">Lazy canvas</span> in an active profile for per-character
          skip stats.
        </p>
      ) : (
        <div className="grid gap-4">
          <WindowSummary stats={stats} />
          <div className="grid gap-4 @lg:grid-cols-2">
            <TrendChart
              title="Ticks / sec"
              data={stats.series}
              dataKey="tps"
              color={TPS_COLOR}
              unit=""
            />
            <TrendChart
              title="ms / tick"
              data={stats.series}
              dataKey="mspt"
              color={MSPT_COLOR}
              unit="ms"
            />
          </div>
          <CharacterBars stats={stats} />
        </div>
      )}
    </Panel>
  );
}

function WindowSummary({ stats }: { stats: PerfStats }) {
  return (
    <div>
      <div className="mb-1 text-[11px] font-medium uppercase tracking-wide text-muted-foreground">
        Tick rate &amp; cost per window
      </div>
      <table className="w-full text-xs tabular-nums">
        <thead>
          <tr className="text-[11px] uppercase tracking-wide text-muted-foreground">
            <th className="py-1 text-left font-medium">window</th>
            <th className="py-1 text-right font-medium">tps</th>
            <th className="py-1 text-right font-medium">avg</th>
            <th className="py-1 text-right font-medium">p90</th>
            <th className="py-1 text-right font-medium">max</th>
            <th className="w-6 py-1 text-right font-normal normal-case">ms</th>
          </tr>
        </thead>
        <tbody>
          {stats.windows.map((w) => (
            <tr key={w.label} className="border-t border-border/60">
              <td className="py-1 text-left text-muted-foreground">{w.label}</td>
              <td className="py-1 text-right font-medium">{fmt(w.tps, 1)}</td>
              {w.count > 0 ? (
                <>
                  <td className="py-1 text-right">{fmt(w.avg)}</td>
                  <td className="py-1 text-right">{fmt(w.p90)}</td>
                  <td className="py-1 text-right">{fmt(w.max)}</td>
                  <td />
                </>
              ) : (
                <td className="py-1 text-right text-muted-foreground" colSpan={4}>
                  no ticks
                </td>
              )}
            </tr>
          ))}
        </tbody>
      </table>
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
  dataKey: "tps" | "mspt";
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

function CharacterBars({ stats }: { stats: PerfStats }) {
  if (stats.characters.length === 0) {
    return (
      <p className="text-[11px] text-muted-foreground">
        No characters tracked yet (enable Lazy canvas and enter a chat room).
      </p>
    );
  }
  return (
    <div>
      <div className="mb-1.5 text-[11px] font-medium uppercase tracking-wide text-muted-foreground">
        Canvas rebuilds skipped (per character)
      </div>
      <div className="grid gap-1.5">
        {stats.characters.map((c) => {
          const total = c.drawTimes + c.skipTimes;
          const pct = Math.round(c.skipRatio * 100);
          return (
            <div key={c.id} className="flex items-center justify-between gap-2 text-xs">
              <span className="truncate">
                {c.label}
                <span className="ml-1.5 text-[11px] tabular-nums text-muted-foreground">
                  {c.skipTimes}/{total} skipped · {fmt(c.avgDraw)}ms draw · {c.lastSeenSec}s ago
                </span>
              </span>
              <span className="shrink-0 font-semibold tabular-nums text-primary">{pct}%</span>
            </div>
          );
        })}
      </div>
    </div>
  );
}