import { ExternalLink, X } from "lucide-react";
import { Children, isValidElement, useRef, useState } from "react";
import type { CSSProperties, ReactElement, ReactNode } from "react";
import { cn } from "../../lib/utils";
import { WindowPortal } from "./window-portal";

type Slot = "title" | "actions" | "body" | "footer";

interface TitleProps {
  /** Leading icon shown before the title text. */
  icon?: ReactNode;
  children?: ReactNode;
}
interface SlotProps {
  children?: ReactNode;
  className?: string;
}

/** Tag a slot component so the parent <Window> can locate it among its children. */
function tagSlot<P>(component: (props: P) => ReactElement | null, slot: Slot) {
  (component as { __slot?: Slot }).__slot = slot;
  return component;
}

function isSlot<P>(node: ReactNode, slot: Slot): node is ReactElement<P> {
  return isValidElement(node) && (node.type as { __slot?: Slot })?.__slot === slot;
}

/**
 * Title-bar label. Use `icon` for a leading glyph.
 * `<Window.Title icon={<Foo />}>Label</Window.Title>`
 */
const WindowTitle = tagSlot<TitleProps>(() => null, "title");

/** Extra controls placed in the title bar, before the pop-out/close buttons. */
const WindowActions = tagSlot<SlotProps>(() => null, "actions");

/** Main scrollable content region (fills the available height). */
const WindowBody = tagSlot<SlotProps>(() => null, "body");

/** Pinned bottom slot (e.g. an action / status bar). */
const WindowFooter = tagSlot<SlotProps>(() => null, "footer");

export interface WindowProps {
  /** Plain-text title used for the popped-out native window's document.title. */
  documentTitle?: string;
  /**
   * Composed via slots:
   * `<Window.Title>`, `<Window.Actions>`, `<Window.Body>`, `<Window.Footer>`.
   * Any non-slot children are treated as body content.
   */
  children: ReactNode;
  onClose?: () => void;
  /** Show the "open in separate window" button. */
  poppable?: boolean;
  defaultWidth?: number;
  defaultHeight?: number;
  minWidth?: number;
  minHeight?: number;
  className?: string;
}

const titleBtn =
  "inline-flex h-7 w-9 items-center justify-center text-muted-foreground transition-colors hover:bg-accent hover:text-foreground";

function clamp(value: number, min: number, max: number): number {
  return Math.min(Math.max(value, min), max);
}

/**
 * A draggable, resizable floating window styled like a desktop config dialog.
 * Can pop itself out into a real separate browser window.
 *
 * Compose it with its slot subcomponents:
 *
 * ```tsx
 * <Window poppable onClose={...}>
 *   <Window.Title icon={<Settings />}>Configuration</Window.Title>
 *   <Window.Actions>{extraButtons}</Window.Actions>
 *   <Window.Body>{content}</Window.Body>
 *   <Window.Footer>{statusBar}</Window.Footer>
 * </Window>
 * ```
 */
export function Window({
  documentTitle,
  children,
  onClose,
  poppable = false,
  defaultWidth = 760,
  defaultHeight = 580,
  minWidth = 460,
  minHeight = 360,
  className
}: WindowProps) {
  const ref = useRef<HTMLDivElement>(null);
  const dragOffset = useRef<{ dx: number; dy: number } | null>(null);
  const resizeStart = useRef<{ x: number; y: number; w: number; h: number } | null>(null);
  const [pos, setPos] = useState<{ x: number; y: number } | null>(null);
  const [size, setSize] = useState({ w: defaultWidth, h: defaultHeight });
  const [popped, setPopped] = useState(false);
  const [styleText, setStyleText] = useState("");

  let titleEl: ReactElement<TitleProps> | undefined;
  let actionsEl: ReactElement<SlotProps> | undefined;
  let bodyEl: ReactElement<SlotProps> | undefined;
  let footerEl: ReactElement<SlotProps> | undefined;
  const looseChildren: ReactNode[] = [];
  Children.forEach(children, (child) => {
    if (isSlot<TitleProps>(child, "title")) titleEl = child;
    else if (isSlot<SlotProps>(child, "actions")) actionsEl = child;
    else if (isSlot<SlotProps>(child, "body")) bodyEl = child;
    else if (isSlot<SlotProps>(child, "footer")) footerEl = child;
    else looseChildren.push(child);
  });

  const titleNode = titleEl?.props.children;
  const docTitle =
    documentTitle ?? (typeof titleNode === "string" ? titleNode : "Configuration");

  function onDragMove(e: PointerEvent) {
    const o = dragOffset.current;
    if (!o) return;
    setPos({
      x: clamp(e.clientX - o.dx, 0, window.innerWidth - 120),
      y: clamp(e.clientY - o.dy, 0, window.innerHeight - 40)
    });
  }
  function onDragUp() {
    dragOffset.current = null;
    window.removeEventListener("pointermove", onDragMove);
    window.removeEventListener("pointerup", onDragUp);
  }
  function onTitlePointerDown(e: React.PointerEvent) {
    if (e.button !== 0 || !ref.current) return;
    const rect = ref.current.getBoundingClientRect();
    dragOffset.current = { dx: e.clientX - rect.left, dy: e.clientY - rect.top };
    setPos({ x: rect.left, y: rect.top });
    window.addEventListener("pointermove", onDragMove);
    window.addEventListener("pointerup", onDragUp);
  }

  function onResizeMove(e: PointerEvent) {
    const s = resizeStart.current;
    if (!s) return;
    setSize({
      w: clamp(s.w + (e.clientX - s.x), minWidth, window.innerWidth),
      h: clamp(s.h + (e.clientY - s.y), minHeight, window.innerHeight)
    });
  }
  function onResizeUp() {
    resizeStart.current = null;
    window.removeEventListener("pointermove", onResizeMove);
    window.removeEventListener("pointerup", onResizeUp);
  }
  function onResizePointerDown(e: React.PointerEvent) {
    if (e.button !== 0 || !ref.current) return;
    e.stopPropagation();
    const rect = ref.current.getBoundingClientRect();
    setPos({ x: rect.left, y: rect.top });
    resizeStart.current = { x: e.clientX, y: e.clientY, w: rect.width, h: rect.height };
    window.addEventListener("pointermove", onResizeMove);
    window.addEventListener("pointerup", onResizeUp);
  }

  function popOut() {
    const root = ref.current?.getRootNode();
    if (root instanceof ShadowRoot) {
      const css = Array.from(root.querySelectorAll("style"))
        .map((s) => s.textContent ?? "")
        .join("\n")
        // `:host` rules don't match in a normal document; retarget them at :root.
        .replaceAll(":host", ":root");
      setStyleText(css);
    }
    setPopped(true);
  }

  const chrome = (
    <>
      <header
        className={cn(
          "flex h-8 shrink-0 select-none items-center justify-between border-b bg-muted pl-3 pr-1",
          !popped && "cursor-move"
        )}
        onPointerDown={popped ? undefined : onTitlePointerDown}
      >
        <div className="flex min-w-0 items-center gap-2 text-foreground">
          {titleEl?.props.icon}
          <span className="truncate text-xs font-semibold">{titleNode}</span>
        </div>
        <div className="flex shrink-0 items-center" onPointerDown={(e) => e.stopPropagation()}>
          {actionsEl?.props.children}
          {poppable && !popped && (
            <button className={titleBtn} onClick={popOut} title="Open in separate window">
              <ExternalLink size={14} />
            </button>
          )}
          {onClose && (
            <button
              className={cn(titleBtn, "hover:bg-destructive hover:text-destructive-foreground")}
              onClick={onClose}
              title="Close"
            >
              <X size={15} />
            </button>
          )}
        </div>
      </header>
      <div className={cn("flex min-h-0 flex-1 flex-col", bodyEl?.props.className)}>
        {bodyEl ? bodyEl.props.children : looseChildren}
      </div>
      {footerEl?.props.children}
    </>
  );

  if (popped) {
    return (
      <WindowPortal
        documentTitle={docTitle}
        styleText={styleText}
        width={Math.round(size.w)}
        height={Math.round(size.h)}
        onClose={() => setPopped(false)}
      >
        <div className="flex h-screen w-screen flex-col overflow-hidden bg-background text-foreground">
          {chrome}
        </div>
      </WindowPortal>
    );
  }

  const style: CSSProperties = pos
    ? { left: pos.x, top: pos.y }
    : { left: "50%", top: "50%", transform: "translate(-50%, -50%)" };
  style.width = size.w;
  style.height = size.h;
  style.maxWidth = "calc(100vw - 1rem)";
  style.maxHeight = "calc(100vh - 1rem)";

  return (
    <div
      ref={ref}
      role="dialog"
      className={cn(
        // The shadow host owns the page-level z-index; within it, later-mounted
        // windows (editors) stack above earlier ones by DOM order.
        "fixed z-40 flex flex-col overflow-hidden rounded-lg border bg-background shadow-2xl",
        className
      )}
      style={style}
    >
      {chrome}
      {/* resize handle */}
      <div
        onPointerDown={onResizePointerDown}
        title="Resize"
        className="absolute bottom-0 right-0 z-10 h-4 w-4 cursor-nwse-resize"
        style={{
          background:
            "linear-gradient(135deg, transparent 0 50%, hsl(var(--muted-foreground) / 0.45) 50% 60%, transparent 60% 70%, hsl(var(--muted-foreground) / 0.45) 70% 80%, transparent 80%)"
        }}
      />
    </div>
  );
}

Window.Title = WindowTitle;
Window.Actions = WindowActions;
Window.Body = WindowBody;
Window.Footer = WindowFooter;
