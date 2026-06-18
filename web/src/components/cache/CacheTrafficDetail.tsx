import { useCallback, useEffect, useState } from "react";
import { Copy, RefreshCw } from "lucide-react";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { getCacheTrafficDetail } from "@/api";
import { cn, errorMessage, formatBytes } from "@/lib/utils";
import type { CacheOutcome, CacheResourceRow, CacheTrafficDetail } from "@/types";
import {
  DistributionBar,
  OUTCOME_META,
  formatPercent,
  hitRate,
  hitRateColor,
  sumKeys,
} from "./cacheTraffic";

const ALL_OUTCOME_KEYS: CacheOutcome[] = OUTCOME_META.map((o) => o.key);

// Sentinel URL the backend uses for resources beyond the per-host tracking cap
// (SBC_TRAFFIC_MAX_RESOURCES_PER_HOST). Their individual names are not retained.
const OTHER_BUCKET = "(other)";

// Display the URL path (+query), dropping the origin since the host is already
// the context. Falls back to the raw string for non-absolute URLs.
function displayPath(url: string): string {
  try {
    const u = new URL(url);
    return u.pathname + u.search || "/";
  } catch {
    return url;
  }
}

function statusBadgeClass(status: number): string {
  if (status >= 500) return "text-rose-600 dark:text-rose-400";
  if (status >= 400) return "text-amber-600 dark:text-amber-400";
  if (status >= 300) return "text-sky-600 dark:text-sky-400";
  if (status >= 200) return "text-emerald-600 dark:text-emerald-400";
  return "text-muted-foreground";
}

function ResourceRow({
  row,
  active,
  countLabel,
}: {
  row: CacheResourceRow;
  active: CacheOutcome | null;
  countLabel: number;
}) {
  const [copied, setCopied] = useState(false);
  const totalBytes = sumKeys(row.bytes, ALL_OUTCOME_KEYS);
  const isOther = row.url === OTHER_BUCKET;

  function copy() {
    void navigator.clipboard?.writeText(row.url).then(
      () => {
        setCopied(true);
        setTimeout(() => setCopied(false), 1200);
      },
      () => {},
    );
  }

  return (
    <div className="grid grid-cols-[1fr_auto] items-center gap-2 rounded-md border px-2 py-1.5">
      <div className="min-w-0">
        <div className="flex items-center gap-1.5">
          {!isOther && row.lastStatus > 0 && (
            <span
              className={cn(
                "shrink-0 text-[10px] font-semibold tabular-nums",
                statusBadgeClass(row.lastStatus),
              )}
              title={`Last HTTP status ${row.lastStatus}`}
            >
              {row.lastStatus}
            </span>
          )}
          {isOther ? (
            <span
              className="truncate text-[11px] italic text-muted-foreground"
              title="Resources beyond the per-host tracking limit — individual names are not retained. Rebuild with a higher SBC_TRAFFIC_MAX_RESOURCES_PER_HOST to see more."
            >
              other resources (tracking limit reached)
            </span>
          ) : (
            <button
              type="button"
              onClick={copy}
              title={`${row.url}\nClick to copy`}
              className="group flex min-w-0 items-center gap-1 text-left"
            >
              <span className="truncate font-mono text-[11px]">{displayPath(row.url)}</span>
              <Copy
                size={10}
                className="shrink-0 text-muted-foreground opacity-0 transition-opacity group-hover:opacity-100"
              />
              {copied && <span className="shrink-0 text-[10px] text-emerald-500">copied</span>}
            </button>
          )}
        </div>
        <div className="mt-1">
          <DistributionBar counts={row.counts} highlight={active} />
        </div>
      </div>
      <div className="flex shrink-0 items-center gap-2 text-right">
        <Badge variant="secondary" className="text-[10px] tabular-nums">
          {countLabel.toLocaleString()}×
        </Badge>
        <span className="w-16 text-right text-[11px] tabular-nums text-muted-foreground">
          {formatBytes(totalBytes)}
        </span>
      </div>
    </div>
  );
}

export function CacheTrafficDetail({ host }: { host: string }) {
  const [detail, setDetail] = useState<CacheTrafficDetail | null>(null);
  const [error, setError] = useState("");
  const [active, setActive] = useState<CacheOutcome | null>(null);

  const refresh = useCallback(async () => {
    try {
      setDetail(await getCacheTrafficDetail(host));
      setError("");
    } catch (err) {
      setError(errorMessage(err));
    }
  }, [host]);

  // Fetch on open and poll while expanded so the drill-down stays live like the
  // aggregate stream above it.
  useEffect(() => {
    void refresh();
    const id = setInterval(() => void refresh(), 2000);
    return () => clearInterval(id);
  }, [refresh]);

  if (error) {
    return (
      <div className="flex items-center justify-between rounded-md border border-dashed bg-background px-3 py-2 text-xs text-muted-foreground">
        <span>Failed to load detail: {error}</span>
        <Button variant="ghost" size="sm" className="h-6 px-2" onClick={() => void refresh()}>
          <RefreshCw size={12} />
          Retry
        </Button>
      </div>
    );
  }

  if (!detail) {
    return (
      <div className="rounded-md border border-dashed bg-background px-3 py-2 text-xs text-muted-foreground">
        Loading resources…
      </div>
    );
  }

  const counts = detail.total.counts;
  let resources = detail.resources;
  if (active) {
    resources = resources
      .filter((r) => (r.counts[active] ?? 0) > 0)
      .sort((a, b) => (b.counts[active] ?? 0) - (a.counts[active] ?? 0));
  }

  return (
    <div className="grid gap-2 rounded-md border bg-background p-2">
      {/* Category filter chips — click one to list only the resources behind that
          outcome (and highlight it in every bar). */}
      <div className="flex flex-wrap gap-1.5">
        <button
          type="button"
          onClick={() => setActive(null)}
          className={cn(
            "rounded-full border px-2 py-0.5 text-[10px] transition-colors",
            active === null ? "border-foreground bg-foreground text-background" : "hover:bg-muted",
          )}
        >
          All {detail.total.requests.toLocaleString()}
        </button>
        {OUTCOME_META.map((o) => {
          const n = counts[o.key] ?? 0;
          if (n === 0) return null;
          const selected = active === o.key;
          return (
            <button
              key={o.key}
              type="button"
              onClick={() => setActive(selected ? null : o.key)}
              className={cn(
                "flex items-center gap-1 rounded-full border px-2 py-0.5 text-[10px] transition-colors",
                selected ? "border-foreground bg-muted" : "hover:bg-muted",
              )}
            >
              <span className={cn("inline-block h-2 w-2 rounded-sm", o.color)} />
              <span className={cn(selected && o.text)}>{o.label}</span>
              <span className="font-medium tabular-nums">{n.toLocaleString()}</span>
            </button>
          );
        })}
        <span
          className={cn(
            "ml-auto self-center text-[10px] font-semibold tabular-nums",
            hitRateColor(hitRate(counts)),
          )}
        >
          {formatPercent(hitRate(counts))} hit
        </span>
      </div>

      {/* Resource list */}
      {resources.length === 0 ? (
        <p className="px-1 text-[11px] text-muted-foreground">No resources for this filter.</p>
      ) : (
        <div className="grid max-h-80 gap-1 overflow-y-auto pr-1">
          {resources.map((r) => (
            <ResourceRow
              key={r.url}
              row={r}
              active={active}
              countLabel={active ? (r.counts[active] ?? 0) : r.requests}
            />
          ))}
        </div>
      )}
    </div>
  );
}
