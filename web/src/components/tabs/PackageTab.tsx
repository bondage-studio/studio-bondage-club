import { Input } from "../ui/input";
import { FormField } from "../shared/FormField";
import { Panel } from "../shared/Panel";
import type { AppConfig, ConfigResponse } from "../../types";

interface Props {
  form: AppConfig;
  snapshot: ConfigResponse;
  onChange: (mutator: (draft: AppConfig) => void) => void;
}

export function PackageTab({ form, snapshot, onChange }: Props) {
  return (
    <div className="grid max-w-2xl gap-3">
      <Panel title="Package source">
        <div className="grid gap-3">
          <FormField label="Package directory">
            <Input
              value={form.package.dir}
              onChange={(e) => onChange((d) => void (d.package.dir = e.target.value))}
              spellCheck={false}
            />
          </FormField>
          <FormField label="Manifest URL">
            <Input
              value={form.package.manifestUrl}
              onChange={(e) => onChange((d) => void (d.package.manifestUrl = e.target.value))}
              spellCheck={false}
            />
          </FormField>
        </div>
      </Panel>
      <Panel title="Diagnostics">
        <div className="grid gap-1 text-xs text-muted-foreground">
          <p>
            Config: <span className="text-foreground">{snapshot.configPath}</span>
          </p>
          <p>
            Runtime: <span className="text-foreground">{snapshot.status.mode}</span>
          </p>
        </div>
      </Panel>
    </div>
  );
}