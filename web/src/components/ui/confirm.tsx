import { createContext, useCallback, useContext, useState } from "react";
import type { ReactNode } from "react";
import { ConfirmDialog } from "./ConfirmDialog";
import type { ConfirmDialogProps } from "./ConfirmDialog";

export type ConfirmOptions = Pick<
  ConfirmDialogProps,
  "title" | "body" | "confirmLabel" | "cancelLabel" | "destructive"
>;

type ConfirmFn = (options: ConfirmOptions) => Promise<boolean>;

const ConfirmContext = createContext<ConfirmFn | null>(null);

interface PendingConfirm {
  options: ConfirmOptions;
  resolve: (result: boolean) => void;
}

/**
 * Provides an imperative, promise-based confirm service. Wrap the app once:
 *
 * ```tsx
 * <ConfirmProvider><App /></ConfirmProvider>
 * ```
 *
 * then anywhere inside:
 *
 * ```tsx
 * const confirm = useConfirm();
 * if (await confirm({ title: "Delete?", destructive: true })) { ... }
 * ```
 *
 * Only one dialog is shown at a time; a second call replaces the first (the
 * superseded promise resolves to `false`).
 */
export function ConfirmProvider({ children }: { children: ReactNode }) {
  const [pending, setPending] = useState<PendingConfirm | null>(null);

  const confirm = useCallback<ConfirmFn>((options) => {
    return new Promise<boolean>((resolve) => {
      setPending((current) => {
        // A new request supersedes any in-flight one: cancel the old promise.
        current?.resolve(false);
        return { options, resolve };
      });
    });
  }, []);

  const settle = useCallback((result: boolean) => {
    setPending((current) => {
      current?.resolve(result);
      return null;
    });
  }, []);

  return (
    <ConfirmContext.Provider value={confirm}>
      {children}
      {pending && (
        <ConfirmDialog
          {...pending.options}
          onConfirm={() => settle(true)}
          onCancel={() => settle(false)}
        />
      )}
    </ConfirmContext.Provider>
  );
}

/** Returns the `confirm(options) => Promise<boolean>` function. */
export function useConfirm(): ConfirmFn {
  const ctx = useContext(ConfirmContext);
  if (!ctx) {
    throw new Error("useConfirm must be used within a ConfirmProvider");
  }
  return ctx;
}