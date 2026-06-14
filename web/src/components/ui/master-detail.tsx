import { ChevronLeft, ChevronRight } from "lucide-react";
import { useEffect, useRef, useState } from "react";
import type { ReactNode } from "react";
import { cn } from "../../lib/utils";

export interface MasterDetailItem {
  /** Stable identity; compared against `activeKey`. */
  key: string;
  /** Leading glyph (e.g. a lucide icon). */
  icon?: ReactNode;
  label: string;
  /** Sub-line shown under the label in the narrow (single-column) layout. */
  description?: string;
  /** Amber "unsaved changes" dot. */
  badge?: boolean;
}

interface MasterDetailProps {
  items: MasterDetailItem[];
  activeKey: string;
  onSelect: (key: string) => void;
  /** The configuration panel for the active item. */
  detail: ReactNode;
  /**
   * Width (px) of the component's own box below which it collapses to the
   * single-column, slide-between-pages layout. Measured on the component, not
   * the viewport, so it works inside the resizable floating window too.
   */
  breakpoint?: number;
  /** Text next to the back chevron on the detail page (narrow layout). */
  backLabel?: string;
  className?: string;
}

/**
 * Master/detail layout with a built-in responsive mode.
 *
 * - Wide: a fixed master list on the left, the detail panel filling the rest —
 *   the classic side-by-side config layout.
 * - Narrow (e.g. a phone-sized window): the master list takes the full width;
 *   picking an item slides over to a second "page" holding the detail panel,
 *   with a back affordance to slide back to the list.
 *
 * The narrow breakpoint is detected from the component's own measured width via
 * a ResizeObserver, so it reacts to the surrounding window being resized rather
 * than the browser viewport.
 */
export function MasterDetail({
  items,
  activeKey,
  onSelect,
  detail,
  breakpoint = 560,
  backLabel = "Back",
  className,
}: MasterDetailProps) {
  const rootRef = useRef<HTMLDivElement>(null);
  const [narrow, setNarrow] = useState(false);
  // Which page is shown in the narrow layout; ignored when wide (both visible).
  const [page, setPage] = useState<"master" | "detail">("master");

  useEffect(() => {
    const el = rootRef.current;
    if (!el) return;
    const ro = new ResizeObserver((entries) => {
      setNarrow(entries[0].contentRect.width < breakpoint);
    });
    ro.observe(el);
    return () => ro.disconnect();
  }, [breakpoint]);

  function handleSelect(key: string) {
    onSelect(key);
    setPage("detail");
  }

  const dot = (
    <span className="h-1.5 w-1.5 shrink-0 rounded-full bg-amber-500" title="Unsaved changes" />
  );

  // Wide: compact single-line rows with an active accent.
  const wideList = (
    <nav className="flex w-40 shrink-0 flex-col gap-px border-r bg-muted p-1.5">
      {items.map(({ key, icon, label, badge }) => {
        const active = key === activeKey;
        return (
          <button
            key={key}
            onClick={() => handleSelect(key)}
            className={cn(
              "relative flex items-center gap-2 rounded-md px-2.5 py-1.5 text-left text-sm transition-colors",
              active
                ? "bg-primary/10 font-medium text-primary"
                : "text-muted-foreground hover:bg-accent hover:text-foreground",
            )}
          >
            {active && (
              <span className="absolute inset-y-1 left-0 w-0.5 rounded-r-full bg-primary" />
            )}
            <span className={cn("shrink-0", active ? "text-primary" : "text-muted-foreground")}>
              {icon}
            </span>
            {label}
            {badge && <span className="ml-auto">{dot}</span>}
          </button>
        );
      })}
    </nav>
  );

  const narrowList = (
    <nav className="w-full bg-muted p-2">
      <div className="divide-y overflow-hidden rounded-lg border bg-card">
        {items.map(({ key, icon, label, description, badge }) => (
          <button
            key={key}
            onClick={() => handleSelect(key)}
            className="flex w-full items-center gap-3 px-3 py-3 text-left text-foreground transition-colors hover:bg-accent"
          >
            <span className="shrink-0 text-muted-foreground">{icon}</span>
            <span className="flex min-w-0 flex-1 flex-col gap-0.5">
              <span className="flex items-center gap-1.5 text-sm font-medium">
                {label}
                {badge && dot}
              </span>
              {description && (
                <span className="line-clamp-2 text-xs text-muted-foreground">{description}</span>
              )}
            </span>
            <ChevronRight size={16} className="shrink-0 text-muted-foreground" />
          </button>
        ))}
      </div>
    </nav>
  );

  // Wide: classic side-by-side layout.
  if (!narrow) {
    return (
      <div ref={rootRef} className={cn("flex min-h-0 w-full flex-1", className)}>
        {wideList}
        <div className="flex min-w-0 flex-1 flex-col">{detail}</div>
      </div>
    );
  }

  // Narrow: two full-width pages on a sliding track.
  return (
    <div ref={rootRef} className={cn("relative min-h-0 w-full flex-1 overflow-hidden", className)}>
      <div
        className="flex h-full w-[200%] transition-transform duration-300 ease-out motion-reduce:transition-none"
        style={{ transform: page === "detail" ? "translateX(-50%)" : "translateX(0)" }}
      >
        <div className="flex h-full w-1/2 min-w-0 flex-col overflow-y-auto">{narrowList}</div>
        <div className="flex h-full w-1/2 min-w-0 flex-col">
          <button
            onClick={() => setPage("master")}
            className="flex h-9 shrink-0 items-center gap-1 border-b bg-muted pl-1.5 pr-3 text-sm font-medium text-foreground transition-colors hover:bg-accent"
          >
            <ChevronLeft size={18} className="text-muted-foreground" />
            {backLabel}
          </button>
          <div className="flex min-h-0 flex-1 flex-col">{detail}</div>
        </div>
      </div>
    </div>
  );
}
