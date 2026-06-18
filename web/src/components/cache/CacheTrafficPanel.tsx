import { useState } from "react";
import { ChevronDown, ChevronRight, RotateCcw } from "lucide-react";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Panel } from "@/components/shared/Panel";
import { resetCacheTraffic } from "@/api";
import { cn, errorMessage, formatBytes } from "@/lib/utils";
import type { CacheTraffic } from "@/types";
import { CacheTrafficDetail } from "./CacheTrafficDetail";
import {
  CACHE_HIT_OUTCOMES,
  DOWNLOAD_OUTCOMES,
  DistributionBar,
  OUTCOME_META,
  formatDuration,
  formatPercent,
  hitRate,
  hitRateColor,
  sumKeys,
} from "./cacheTraffic";

export function CacheTrafficPanel({ traffic }: { traffic: CacheTraffic | null | undefined }) {
  const [busy, setBusy] = useState(false);
  const [status, setStatus] = useState("");
  const [expanded, setExpanded] = useState<string | null>(null);

  async function onReset() {
    setBusy(true);
    setStatus("");
    try {
      await resetCacheTraffic();
      // Counters clear server-side; the next live stream frame (≤2s) reflects it.
      setExpanded(null);
      setStatus("Counters reset");
    } catch (err) {
      setStatus(errorMessage(err));
    } finally {
      setBusy(false);
    }
  }

  const total = traffic?.total;
  const hasData = !!total && total.requests > 0;
  const overallRate = total ? hitRate(total.counts) : null;
  const savedBytes = total ? sumKeys(total.bytes, CACHE_HIT_OUTCOMES) : 0;
  const downloadedBytes = total ? sumKeys(total.bytes, DOWNLOAD_OUTCOMES) : 0;
  const windowMs = traffic ? traffic.nowMs - traffic.sinceMs : 0;

  return (
    <Panel
      title="Hit rate by domain"
      action={
        <div className="flex items-center gap-1.5">
          {traffic && (
            <span className="text-[10px] text-muted-foreground">
              last {formatDuration(windowMs)}
            </span>
          )}
          <Button
            variant="ghost"
            size="sm"
            className="h-6 px-2"
            onClick={() => void onReset()}
            disabled={busy}
          >
            <RotateCcw size={12} />
            Reset
          </Button>
        </div>
      }
    >
      {!hasData ? (
        <p className="text-xs text-muted-foreground">
          {traffic
            ? "No requests recorded yet — browse the game to collect data."
            : "Waiting for data…"}
        </p>
      ) : (
        <div className="grid gap-3">
          {/* Overall summary */}
          <div className="grid grid-cols-3 gap-2">
            <div className="rounded-md border bg-muted px-3 py-2">
              <div className="text-[10px] uppercase tracking-wide text-muted-foreground">
                Hit rate
              </div>
              <div className={cn("text-xl font-semibold tabular-nums", hitRateColor(overallRate))}>
                {formatPercent(overallRate)}
              </div>
              <div className="text-[10px] text-muted-foreground">
                {total!.requests.toLocaleString()} requests
              </div>
            </div>
            <div className="rounded-md border bg-muted px-3 py-2">
              <div className="text-[10px] uppercase tracking-wide text-muted-foreground">
                Served from cache
              </div>
              <div className="text-xl font-semibold tabular-nums text-emerald-600 dark:text-emerald-400">
                {formatBytes(savedBytes)}
              </div>
              <div className="text-[10px] text-muted-foreground">bandwidth saved</div>
            </div>
            <div className="rounded-md border bg-muted px-3 py-2">
              <div className="text-[10px] uppercase tracking-wide text-muted-foreground">
                Downloaded
              </div>
              <div className="text-xl font-semibold tabular-nums">
                {formatBytes(downloadedBytes)}
              </div>
              <div className="text-[10px] text-muted-foreground">from upstream</div>
            </div>
          </div>

          {/* Overall distribution + legend */}
          <div className="grid gap-1.5">
            <DistributionBar counts={total!.counts} />
            <div className="flex flex-wrap gap-x-3 gap-y-1">
              {OUTCOME_META.map((o) => {
                const n = total!.counts[o.key] ?? 0;
                if (n === 0) return null;
                return (
                  <span
                    key={o.key}
                    className="flex items-center gap-1 text-[10px] text-muted-foreground"
                  >
                    <span className={cn("inline-block h-2 w-2 rounded-sm", o.color)} />
                    {o.label}
                    <span className="font-medium text-foreground">{n.toLocaleString()}</span>
                  </span>
                );
              })}
            </div>
          </div>

          {/* Per-domain table — click a row to drill into its resources */}
          <div className="grid gap-1">
            <div className="grid grid-cols-[1fr_auto_auto] items-center gap-2 px-1 text-[10px] uppercase tracking-wide text-muted-foreground">
              <span>Domain</span>
              <span className="w-14 text-right">Hit rate</span>
              <span className="w-20 text-right">Saved</span>
            </div>
            <div className="grid max-h-96 gap-1 overflow-y-auto pr-1">
              {traffic!.hosts.map((row) => {
                const host = row.host;
                if (!host) return null;
                const rate = hitRate(row.counts);
                const open = expanded === host;
                return (
                  <div key={host} className="grid gap-1">
                    <button
                      type="button"
                      onClick={() => setExpanded(open ? null : host)}
                      className={cn(
                        "grid grid-cols-[auto_1fr_auto_auto] items-center gap-2 rounded-md border px-2 py-1.5 text-left transition-colors hover:bg-muted/60",
                        open && "bg-muted/60",
                      )}
                    >
                      {open ? (
                        <ChevronDown size={14} className="shrink-0 text-muted-foreground" />
                      ) : (
                        <ChevronRight size={14} className="shrink-0 text-muted-foreground" />
                      )}
                      <div className="min-w-0">
                        <div className="flex items-center gap-1.5">
                          <span className="truncate text-xs font-medium" title={host}>
                            {host}
                          </span>
                          <Badge variant="secondary" className="shrink-0 text-[10px]">
                            {row.requests.toLocaleString()}
                          </Badge>
                        </div>
                        <div className="mt-1">
                          <DistributionBar counts={row.counts} />
                        </div>
                      </div>
                      <span
                        className={cn(
                          "w-14 text-right text-xs font-semibold tabular-nums",
                          hitRateColor(rate),
                        )}
                      >
                        {formatPercent(rate)}
                      </span>
                      <span className="w-20 text-right text-xs tabular-nums text-muted-foreground">
                        {formatBytes(sumKeys(row.bytes, CACHE_HIT_OUTCOMES))}
                      </span>
                    </button>
                    {open && <CacheTrafficDetail host={host} />}
                  </div>
                );
              })}
            </div>
          </div>

          {status && <p className="text-xs text-muted-foreground">{status}</p>}
        </div>
      )}
    </Panel>
  );
}
