import { useEffect, useState } from "react";
import type { ReactNode } from "react";
import { createPortal } from "react-dom";

export interface WindowPortalProps {
  /** Text for the native window's document.title. */
  documentTitle?: string;
  /** CSS to inject into the new window's <head> (e.g. the shadow root's styles). */
  styleText?: string;
  width?: number;
  height?: number;
  /** Called when the popped-out window is closed (by the user or programmatically). */
  onClose?: () => void;
  children: ReactNode;
}

/**
 * Renders its children into a genuine separate browser window opened via
 * `window.open`. Same-origin, so the configuration API stays reachable.
 */
export function WindowPortal({
  documentTitle = "Configuration",
  styleText = "",
  width = 800,
  height = 600,
  onClose,
  children
}: WindowPortalProps) {
  const [container, setContainer] = useState<HTMLElement | null>(null);

  useEffect(() => {
    const left = Math.max(0, Math.round((window.screen.availWidth - width) / 2));
    const top = Math.max(0, Math.round((window.screen.availHeight - height) / 2));
    const child = window.open(
      "",
      "studio-config-popout",
      `popup=yes,width=${width},height=${height},left=${left},top=${top}`
    );
    if (!child) {
      // Popup blocked — fall back to the in-page window.
      onClose?.();
      return;
    }

    child.document.title = documentTitle;
    const style = child.document.createElement("style");
    style.textContent = styleText;
    child.document.head.appendChild(style);

    const root = child.document.createElement("div");
    root.setAttribute("data-studio-popout-root", "");
    child.document.body.appendChild(root);
    setContainer(root);

    // Poll from the opener window — a timer scheduled inside the popup dies with it.
    const poll = window.setInterval(() => {
      if (child.closed) onClose?.();
    }, 400);

    return () => {
      window.clearInterval(poll);
      if (!child.closed) child.close();
    };
    // Intentionally run once; size/title changes after open are not re-applied.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  return container ? createPortal(children, container) : null;
}