import {
  Children,
  isValidElement,
  useId,
  useState,
  type KeyboardEvent,
  type ReactElement,
  type ReactNode,
} from "react";
import { cn } from "@/lib/utils";

interface EntryProps {
  /** Tab button label; a ReactNode so it can include a badge/dirty dot. */
  name: ReactNode;
  /** Stable id used for selection; defaults to the entry's index. */
  value?: string;
  disabled?: boolean;
  /** Extra classes for this entry's body container. */
  className?: string;
  children: ReactNode;
}

// Marker component: TabPanel reads its props to build the strip + body, so the
// element itself never renders directly. Typed via its annotation so JSX still
// checks the props without an unused parameter.
const TabEntry: (props: EntryProps) => null = () => null;

interface TabPanelProps {
  /** Controlled active value. Omit for uncontrolled (use `defaultValue`). */
  value?: string;
  /** Initial active value when uncontrolled; defaults to the first entry. */
  defaultValue?: string;
  onValueChange?: (value: string) => void;
  className?: string;
  children: ReactNode;
}

function TabPanelRoot({ value, defaultValue, onValueChange, className, children }: TabPanelProps) {
  const idBase = useId();
  const entries = Children.toArray(children).filter(
    (c): c is ReactElement<EntryProps> => isValidElement(c) && c.type === TabEntry,
  );
  const valueOf = (entry: ReactElement<EntryProps>, index: number) =>
    entry.props.value ?? String(index);

  const [internal, setInternal] = useState(defaultValue ?? "");
  const controlled = value !== undefined;
  const wanted = controlled ? value : internal;
  // Resolve to a real entry so an unset/stale value falls back to the first tab.
  const activeEntry = entries.find((e, i) => valueOf(e, i) === wanted) ?? entries[0] ?? null;
  const activeValue = activeEntry ? valueOf(activeEntry, entries.indexOf(activeEntry)) : "";

  const setValue = (next: string) => {
    if (!controlled) setInternal(next);
    onValueChange?.(next);
  };

  function onKeyDown(e: KeyboardEvent<HTMLButtonElement>) {
    if (e.key !== "ArrowRight" && e.key !== "ArrowLeft") return;
    const tabs = Array.from(
      e.currentTarget.parentElement?.querySelectorAll<HTMLButtonElement>(
        '[role="tab"]:not([disabled])',
      ) ?? [],
    );
    const i = tabs.indexOf(e.currentTarget);
    const next = tabs[i + (e.key === "ArrowRight" ? 1 : -1)];
    if (next) {
      e.preventDefault();
      next.focus();
      next.click();
    }
  }

  return (
    <div className={cn("flex min-w-0 flex-col", className)}>
      <div role="tablist" className="flex flex-wrap items-end gap-1">
        {entries.map((entry, i) => {
          const v = valueOf(entry, i);
          const active = v === activeValue;
          return (
            <button
              key={v}
              type="button"
              role="tab"
              id={`${idBase}-tab-${v}`}
              aria-selected={active}
              aria-controls={`${idBase}-panel-${v}`}
              tabIndex={active ? 0 : -1}
              disabled={entry.props.disabled}
              onClick={() => setValue(v)}
              onKeyDown={onKeyDown}
              className={cn(
                // -mb-px overlaps the body's top border so the active tab's opaque
                // fill erases the seam beneath it (the folder-merge effect). max-w
                // + wrap let long names break onto multiple lines.
                "relative -mb-px max-w-[15rem] whitespace-normal break-words rounded-t-md border px-3 py-1.5 text-center text-xs font-medium leading-tight transition-colors focus-visible:z-20 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring disabled:pointer-events-none disabled:opacity-50",
                active
                  ? "z-10 border-border border-b-transparent bg-muted text-foreground"
                  : "border-transparent text-muted-foreground hover:bg-muted/60 hover:text-foreground",
              )}
            >
              {entry.props.name}
            </button>
          );
        })}
      </div>
      {activeEntry && (
        <div
          key={activeValue}
          role="tabpanel"
          id={`${idBase}-panel-${activeValue}`}
          aria-labelledby={`${idBase}-tab-${activeValue}`}
          className={cn(
            "flex flex-col gap-3 rounded-b-md rounded-tr-md border bg-muted p-3",
            activeEntry.props.className,
          )}
        >
          {activeEntry.props.children}
        </div>
      )}
    </div>
  );
}

export const TabPanel = Object.assign(TabPanelRoot, { Entry: TabEntry });
