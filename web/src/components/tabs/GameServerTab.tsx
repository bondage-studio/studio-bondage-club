import { Badge } from "@/components/ui/badge";
import { Input } from "@/components/ui/input";
import { Switch } from "@/components/ui/switch";
import { FormField } from "@/components/shared/FormField";
import { Panel } from "@/components/shared/Panel";
import { IS_ANDROID_BUILD } from "@/lib/platform";
import type { GameServerMode } from "@/originalPage";
import type { AppConfig, GameServerStatus } from "@/types";

interface Props {
  form: AppConfig;
  serverMode: GameServerMode;
  gameStatus: GameServerStatus | null;
  onSwitchMode: (mode: GameServerMode) => void;
  onChange: (mutator: (draft: AppConfig) => void) => void;
}

export function GameServerTab({ form, serverMode, gameStatus, onSwitchMode, onChange }: Props) {
  const defaultDir = `${form.cache.dir || "."}/gameserver`;
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
      {/* The Android app keeps the account database in app-internal storage. */}
      {!IS_ANDROID_BUILD && (
        <Panel title="Storage">
          <FormField label="Account storage path">
            <Input
              value={form.gameServerStoragePath}
              placeholder={defaultDir}
              spellCheck={false}
              onChange={(e) => onChange((d) => void (d.gameServerStoragePath = e.target.value))}
            />
          </FormField>
          <p className="mt-2.5 text-xs text-muted-foreground">
            Where the embedded server keeps its account database. Leave empty to use the default{" "}
            <code>{defaultDir}</code>. Saving a new location applies live — you'll be asked whether
            to migrate the existing accounts or start fresh; live game sockets reconnect either way.
          </p>
        </Panel>
      )}
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
          Accounts persist under <code>{form.gameServerStoragePath || defaultDir}</code>. Your
          choice is remembered in this browser; the server's default applies only on first launch.
        </p>
      </Panel>
    </div>
  );
}
