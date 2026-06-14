import { Badge } from "../ui/badge";
import { Switch } from "../ui/switch";
import { Panel } from "../shared/Panel";
import type { GameServerMode } from "../../originalPage";
import type { GameServerStatus } from "../../types";

interface Props {
  serverMode: GameServerMode;
  gameStatus: GameServerStatus | null;
  onSwitchMode: (mode: GameServerMode) => void;
}

export function GameServerTab({ serverMode, gameStatus, onSwitchMode }: Props) {
  return (
    <div className="grid max-w-2xl gap-3">
      <Panel title="Embedded server">
        <div className="flex items-start justify-between gap-4">
          <div className="space-y-0.5">
            <p className="text-sm font-medium">Use embedded local server</p>
            <p className="text-xs text-muted-foreground">
              Route the game's socket traffic to the built-in server instead of the remote Bondage
              Club server. Toggling reconnects the game immediately — you'll need to log in again on
              the other server.
            </p>
          </div>
          <Switch
            checked={serverMode === "local"}
            onCheckedChange={(checked) => onSwitchMode(checked ? "local" : "remote")}
          />
        </div>
      </Panel>
      <Panel
        title="Live status"
        action={
          <Badge variant={serverMode === "local" ? "default" : "outline"}>
            {serverMode === "local" ? "Local" : "Remote"}
          </Badge>
        }
      >
        <div className="grid grid-cols-2 gap-2">
          <div className="rounded-md border bg-muted px-3 py-2">
            <p className="text-xs text-muted-foreground">Online players</p>
            <p className="text-lg font-semibold tabular-nums">
              {gameStatus ? gameStatus.online : "—"}
            </p>
          </div>
          <div className="rounded-md border bg-muted px-3 py-2">
            <p className="text-xs text-muted-foreground">Active rooms</p>
            <p className="text-lg font-semibold tabular-nums">
              {gameStatus ? gameStatus.rooms : "—"}
            </p>
          </div>
        </div>
        <p className="mt-2.5 text-xs text-muted-foreground">
          Accounts persist under <code>&lt;cache dir&gt;/gameserver</code>. Your choice is
          remembered in this browser; the server's default applies only on first launch.
        </p>
      </Panel>
    </div>
  );
}