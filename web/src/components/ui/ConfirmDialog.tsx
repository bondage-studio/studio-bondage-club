import { useEffect, useRef } from "react";
import type { ReactNode } from "react";
import { Button } from "./button";
import { Window } from "./window";

export interface ConfirmDialogProps {
  title?: string;
  /** Main body content (string or rich nodes). */
  body?: ReactNode;
  confirmLabel?: string;
  cancelLabel?: string;
  /** Style the confirm button as a destructive action. */
  destructive?: boolean;
  onConfirm: () => void;
  onCancel: () => void;
}

/**
 * A non-blocking confirmation dialog styled like the rest of the config panel.
 * Unlike `window.confirm`, it never freezes the page and works inside the shadow
 * DOM / popped-out windows / Electron. It's a pure presentational component;
 * drive it imperatively through `useConfirm()` (see `confirm.tsx`).
 */
export function ConfirmDialog({
  title = "Are you sure?",
  body,
  confirmLabel = "Confirm",
  cancelLabel = "Cancel",
  destructive = false,
  onConfirm,
  onCancel
}: ConfirmDialogProps) {
  const confirmRef = useRef<HTMLButtonElement>(null);

  // Focus the confirm action on open and wire Enter/Escape so the dialog is
  // keyboard-operable without trapping the rest of the page.
  useEffect(() => {
    confirmRef.current?.focus();
    function onKeyDown(event: KeyboardEvent) {
      if (event.key === "Escape") {
        event.preventDefault();
        onCancel();
      } else if (event.key === "Enter") {
        event.preventDefault();
        onConfirm();
      }
    }
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, [onConfirm, onCancel]);

  return (
    <Window onClose={onCancel} defaultWidth={420} defaultHeight={220} minWidth={340} minHeight={180}>
      <Window.Title>{title}</Window.Title>
      <Window.Body className="overflow-y-auto p-4">
        <div className="text-sm text-foreground">{body}</div>
      </Window.Body>
      <Window.Footer>
        <footer className="flex h-10 shrink-0 items-center justify-end gap-2 border-t bg-muted px-3">
          <Button variant="outline" size="sm" onClick={onCancel}>
            {cancelLabel}
          </Button>
          <Button
            ref={confirmRef}
            variant={destructive ? "destructive" : "default"}
            size="sm"
            onClick={onConfirm}
          >
            {confirmLabel}
          </Button>
        </footer>
      </Window.Footer>
    </Window>
  );
}