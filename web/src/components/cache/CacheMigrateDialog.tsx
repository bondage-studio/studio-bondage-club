import { Button } from "../ui/button";
import { Window } from "../ui/window";

interface Props {
  oldDir: string;
  newDir: string;
  onChoose: (migrate: boolean) => void;
  onClose: () => void;
}

/** Confirms whether to migrate existing cache + game data on a cache-dir change. */
export function CacheMigrateDialog({ oldDir, newDir, onChoose, onClose }: Props) {
  return (
    <Window onClose={onClose} defaultWidth={460} defaultHeight={320} minWidth={380} minHeight={260}>
      <Window.Title>Move cache data?</Window.Title>
      <Window.Body className="overflow-y-auto p-4">
        <div className="grid gap-3 text-sm">
          <p className="text-muted-foreground">The cache directory is changing:</p>
          <div className="grid gap-1 rounded-md border bg-muted px-3 py-2 text-xs">
            <p>
              From: <span className="break-all text-foreground">{oldDir}</span>
            </p>
            <p>
              To: <span className="break-all text-foreground">{newDir}</span>
            </p>
          </div>
          <p>
            Migrate the existing cache <strong>and local game accounts</strong> to the new location,
            or start fresh there? Either way the change rebuilds the cache stores.
          </p>
        </div>
      </Window.Body>
      <Window.Footer>
        <footer className="flex h-10 shrink-0 items-center justify-end gap-2 border-t bg-muted px-3">
          <Button variant="outline" size="sm" onClick={() => onChoose(false)}>
            Start fresh
          </Button>
          <Button size="sm" onClick={() => onChoose(true)}>
            Migrate data
          </Button>
        </footer>
      </Window.Footer>
    </Window>
  );
}