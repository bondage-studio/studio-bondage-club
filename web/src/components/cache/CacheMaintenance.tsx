import { useCallback, useEffect, useState } from "react";
import { RefreshCw, RotateCcw } from "lucide-react";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Panel } from "@/components/shared/Panel";
import { expireCache, listVersions } from "@/api";
import type { CacheVersion } from "@/types";

function errorMessage(err: unknown): string {
  return err instanceof Error ? err.message : String(err);
}

export function CacheMaintenance() {
  const [versions, setVersions] = useState<CacheVersion[]>([]);
  const [busy, setBusy] = useState(false);
  const [status, setStatus] = useState("");

  const refresh = useCallback(async () => {
    try {
      setVersions(await listVersions());
    } catch (err) {
      setStatus(errorMessage(err));
    }
  }, []);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  async function expire(filter: { version?: string }, label: string) {
    setBusy(true);
    setStatus("");
    try {
      const { expired } = await expireCache(filter);
      setStatus(
        `${label}: ${expired} ${expired === 1 ? "entry" : "entries"} marked for revalidation`,
      );
      await refresh();
    } catch (err) {
      setStatus(errorMessage(err));
    } finally {
      setBusy(false);
    }
  }

  return (
    <Panel
      title="Revalidate (soft-expire)"
      action={
        <div className="flex items-center gap-1.5">
          <Button
            variant="ghost"
            size="sm"
            className="h-6 px-2"
            onClick={() => void refresh()}
            disabled={busy}
          >
            <RefreshCw size={12} />
            Refresh
          </Button>
          <Button
            variant="outline"
            size="sm"
            className="h-6 px-2"
            onClick={() => void expire({}, "Revalidate all")}
            disabled={busy}
          >
            <RotateCcw size={12} />
            Revalidate all
          </Button>
        </div>
      }
    >
      <div className="grid gap-2">
        <p className="text-xs text-muted-foreground">
          Marks entries stale without deleting bodies — the next request revalidates via ETag (304
          keeps the body). Use it to force a refresh against upstream.
        </p>
        {versions.length === 0 ? (
          <p className="text-xs text-muted-foreground">No version-tagged entries cached yet.</p>
        ) : (
          <div className="grid gap-1.5">
            {versions.map((v) => (
              <div
                key={v.version}
                className="flex items-center justify-between rounded-md border px-3 py-1.5 text-sm"
              >
                <div className="flex items-center gap-2">
                  <span className="font-medium">{v.version}</span>
                  <Badge variant="secondary">{v.entries.toLocaleString()} entries</Badge>
                </div>
                <Button
                  variant="ghost"
                  size="sm"
                  className="h-6 px-2"
                  onClick={() => void expire({ version: v.version }, `Expire ${v.version}`)}
                  disabled={busy}
                >
                  <RotateCcw size={12} />
                  Revalidate
                </Button>
              </div>
            ))}
          </div>
        )}
        {status && <p className="text-xs text-muted-foreground">{status}</p>}
      </div>
    </Panel>
  );
}
