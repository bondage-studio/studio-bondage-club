import { useEffect, useState } from "react";
import { Button } from "../ui/button";
import { Window } from "../ui/window";
import { getPendingUpdate } from "../../api";
import { errorMessage } from "../../lib/utils";
import type { Userscript } from "../../types";

interface Props {
  script: Userscript;
  onApply: () => Promise<void> | void;
  onDismiss: () => Promise<void> | void;
  onClose: () => void;
}

// Review a fetched-but-unapplied update before it takes effect. Shows the current
// source alongside the new one; nothing changes unless the user clicks Apply.
export function PendingUpdateDialog({ script, onApply, onDismiss, onClose }: Props) {
  const [newSource, setNewSource] = useState("");
  const [loading, setLoading] = useState(true);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");

  useEffect(() => {
    let active = true;
    void (async () => {
      try {
        const pending = await getPendingUpdate(script.id);
        if (active) setNewSource(pending.source);
      } catch (err) {
        if (active) setError(errorMessage(err));
      } finally {
        if (active) setLoading(false);
      }
    })();
    return () => {
      active = false;
    };
  }, [script.id]);

  async function run(action: () => Promise<void> | void) {
    setBusy(true);
    setError("");
    try {
      await action();
      onClose();
    } catch (err) {
      setError(errorMessage(err));
      setBusy(false);
    }
  }

  return (
    <Window onClose={onClose} defaultWidth={760} defaultHeight={600} minWidth={520} minHeight={400}>
      <Window.Title>
        Update {script.name}: v{script.version ?? "?"} → v{script.pendingUpdate?.version ?? "?"}
      </Window.Title>

      <Window.Body className="overflow-hidden p-4">
        {loading ? (
          <p className="py-8 text-center text-sm text-muted-foreground">Loading update…</p>
        ) : (
          <div className="grid h-full grid-cols-2 gap-3">
            <div className="flex min-h-0 flex-col gap-1.5">
              <p className="text-xs font-semibold uppercase tracking-wide text-muted-foreground">
                Current (v{script.version ?? "?"})
              </p>
              <textarea
                readOnly
                value={script.source}
                spellCheck={false}
                className="min-h-0 flex-1 resize-none rounded-md border border-input bg-muted/40 p-2 font-mono text-[11px] leading-relaxed"
              />
            </div>
            <div className="flex min-h-0 flex-col gap-1.5">
              <p className="text-xs font-semibold uppercase tracking-wide text-muted-foreground">
                New (v{script.pendingUpdate?.version ?? "?"})
              </p>
              <textarea
                readOnly
                value={newSource}
                spellCheck={false}
                className="min-h-0 flex-1 resize-none rounded-md border border-input bg-background p-2 font-mono text-[11px] leading-relaxed"
              />
            </div>
          </div>
        )}
        {error && <p className="mt-2 text-xs text-destructive">{error}</p>}
      </Window.Body>

      <Window.Footer>
        <footer className="flex h-10 shrink-0 items-center justify-end gap-2 border-t bg-muted px-3">
          <Button variant="outline" size="sm" onClick={() => void run(onDismiss)} disabled={busy}>
            Dismiss
          </Button>
          <Button size="sm" onClick={() => void run(onApply)} disabled={busy || loading}>
            Apply update
          </Button>
        </footer>
      </Window.Footer>
    </Window>
  );
}
