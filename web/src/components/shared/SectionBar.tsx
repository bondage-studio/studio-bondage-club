import { RefreshCw } from "lucide-react";
import { Button } from "../ui/button";

export function SectionBar({
  dirty,
  busy,
  onRefresh,
  onSave
}: {
  dirty: boolean;
  busy: boolean;
  onRefresh: () => void;
  onSave: () => void;
}) {
  return (
    <div className="flex shrink-0 items-center gap-2">
      <Button variant="outline" size="sm" onClick={onRefresh} disabled={busy}>
        <RefreshCw size={14} />
        Refresh
      </Button>
      <Button size="sm" onClick={onSave} disabled={busy || !dirty}>
        Save
      </Button>
    </div>
  );
}
