import { HardDrive } from "lucide-react";
import { formatBytes } from "@/lib/utils";
import type { StoreStat } from "@/types";

export function StoreCard({ store }: { store: StoreStat }) {
  return (
    <div className="rounded-md border bg-muted px-3 py-2 text-sm">
      <div className="mb-1.5 flex items-center gap-1.5 font-medium">
        <HardDrive className="h-3.5 w-3.5 text-muted-foreground" />
        {store.name}
      </div>
      <div className="flex gap-4 text-xs text-muted-foreground">
        <span>
          <span className="font-medium text-foreground">
            {store.stats.entries.toLocaleString()}
          </span>{" "}
          entries
        </span>
        <span className="font-medium text-foreground">{formatBytes(store.stats.bytes)}</span>
      </div>
    </div>
  );
}
