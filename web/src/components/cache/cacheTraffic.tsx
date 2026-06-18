import { cn } from "@/lib/utils";
import type { CacheOutcome, CacheOutcomeMap } from "@/types";

// Outcome render order (best → worst → neutral) with a color used for both the
// distribution bars and the legend swatches. `hit` (green) and `revalidated`
// (blue) are deliberately different hues so a cache hit reads apart from a 304
// revalidation at a glance.
export const OUTCOME_META: ReadonlyArray<{
  key: CacheOutcome;
  label: string;
  /** Solid bar/swatch background. */
  color: string;
  /** Text color for emphasis on light/dark backgrounds. */
  text: string;
}> = [
  {
    key: "hit",
    label: "Hit",
    color: "bg-emerald-500",
    text: "text-emerald-600 dark:text-emerald-400",
  },
  {
    key: "revalidated",
    label: "Revalidated (304)",
    color: "bg-sky-500",
    text: "text-sky-600 dark:text-sky-400",
  },
  {
    key: "stale",
    label: "Stale (upstream down)",
    color: "bg-amber-500",
    text: "text-amber-600 dark:text-amber-400",
  },
  { key: "miss", label: "Miss", color: "bg-rose-500", text: "text-rose-600 dark:text-rose-400" },
  {
    key: "uncached",
    label: "Uncacheable",
    color: "bg-orange-400",
    text: "text-orange-600 dark:text-orange-400",
  },
  {
    key: "bypass",
    label: "Bypass",
    color: "bg-zinc-400",
    text: "text-zinc-600 dark:text-zinc-400",
  },
];

// "Served from cache" outcomes — no upstream body download, so they count toward
// the hit rate and the saved-bandwidth figure.
export const CACHE_HIT_OUTCOMES: CacheOutcome[] = ["hit", "revalidated", "stale"];
// Outcomes that actually downloaded a body from upstream.
export const DOWNLOAD_OUTCOMES: CacheOutcome[] = ["miss", "uncached"];

export function sumKeys(map: CacheOutcomeMap, keys: CacheOutcome[]): number {
  return keys.reduce((acc, k) => acc + (map[k] ?? 0), 0);
}

// Hit rate over cacheable requests (cache-served vs downloaded); bypassed
// requests are excluded from the denominator. Null when there is nothing to rate.
export function hitRate(counts: CacheOutcomeMap): number | null {
  const served = sumKeys(counts, CACHE_HIT_OUTCOMES);
  const downloaded = sumKeys(counts, DOWNLOAD_OUTCOMES);
  const cacheable = served + downloaded;
  return cacheable > 0 ? served / cacheable : null;
}

export function hitRateColor(rate: number | null): string {
  if (rate === null) return "text-muted-foreground";
  if (rate >= 0.8) return "text-emerald-600 dark:text-emerald-400";
  if (rate >= 0.5) return "text-amber-600 dark:text-amber-400";
  return "text-rose-600 dark:text-rose-400";
}

export function formatPercent(rate: number | null): string {
  return rate === null ? "—" : `${(rate * 100).toFixed(1)}%`;
}

export function formatDuration(ms: number): string {
  const total = Math.max(0, Math.round(ms / 1000));
  if (total < 60) return `${total}s`;
  const m = Math.floor(total / 60);
  const s = total % 60;
  if (m < 60) return s ? `${m}m ${s}s` : `${m}m`;
  const h = Math.floor(m / 60);
  return `${h}h ${m % 60}m`;
}

// Horizontal stacked bar of outcome counts. When `highlight` is set, every other
// segment is dimmed so the focused outcome stands out.
export function DistributionBar({
  counts,
  highlight,
  className,
}: {
  counts: CacheOutcomeMap;
  highlight?: CacheOutcome | null;
  className?: string;
}) {
  const total = OUTCOME_META.reduce((acc, o) => acc + (counts[o.key] ?? 0), 0);
  return (
    <div className={cn("flex h-2 w-full overflow-hidden rounded-full bg-muted", className)}>
      {total > 0 &&
        OUTCOME_META.map((o) => {
          const n = counts[o.key] ?? 0;
          if (n === 0) return null;
          const dimmed = highlight != null && highlight !== o.key;
          return (
            <div
              key={o.key}
              className={cn(o.color, dimmed && "opacity-25")}
              style={{ width: `${(n / total) * 100}%` }}
              title={`${o.label}: ${n.toLocaleString()}`}
            />
          );
        })}
    </div>
  );
}
