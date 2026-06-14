import { Badge } from "../ui/badge";
import { Panel } from "../shared/Panel";
import { cn } from "../../lib/utils";
import type { AppConfig, ConfigResponse, Mode } from "../../types";

const modeLabels: Record<Mode, string> = {
  reverse_proxy_cache: "Reverse proxy",
  package_cache: "Package cache"
};

interface Props {
  form: AppConfig;
  snapshot: ConfigResponse;
  onChange: (mutator: (draft: AppConfig) => void) => void;
}

export function ModeTab({ form, snapshot, onChange }: Props) {
  return (
    <div className="grid max-w-2xl gap-3">
      <Panel title="Operating mode">
        <div className="grid grid-cols-2 gap-2">
          {(["reverse_proxy_cache", "package_cache"] as Mode[]).map((m) => (
            <button
              key={m}
              disabled={m === "package_cache"}
              onClick={() => onChange((d) => void (d.mode = m))}
              className={cn(
                "flex flex-col items-start gap-1 rounded-md border p-3 text-left text-sm transition-colors",
                form.mode === m
                  ? "border-primary bg-primary/10 font-medium text-primary ring-1 ring-primary"
                  : "hover:border-border hover:bg-accent",
                m === "package_cache" && "cursor-not-allowed opacity-50"
              )}
            >
              <span>{modeLabels[m]}</span>
              {m === "package_cache" && (
                <Badge variant="outline" className="text-xs">
                  Coming soon
                </Badge>
              )}
            </button>
          ))}
        </div>
      </Panel>
      <Panel title="Capabilities">
        <div className="grid gap-2">
          {snapshot.status.capabilities.map((cap) => (
            <div key={cap.id} className="flex items-start gap-3">
              <span
                className={cn(
                  "mt-1 h-2 w-2 shrink-0 rounded-full",
                  cap.enabled ? "bg-emerald-500" : "bg-muted-foreground/40"
                )}
              />
              <div>
                <p className="text-sm font-medium">{cap.label}</p>
                <p className="text-xs text-muted-foreground">{cap.description}</p>
              </div>
            </div>
          ))}
        </div>
      </Panel>
    </div>
  );
}