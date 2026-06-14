import { Input } from "../ui/input";
import { FormField } from "../shared/FormField";
import { Panel } from "../shared/Panel";
import { IS_ANDROID_BUILD } from "../../lib/platform";
import type { AppConfig } from "../../types";

interface Props {
  form: AppConfig;
  onChange: (mutator: (draft: AppConfig) => void) => void;
}

export function ConnectionTab({ form, onChange }: Props) {
  return (
    <div className="grid max-w-2xl gap-3">
      <Panel title="Upstream">
        <div className="grid gap-3">
          <FormField label="Upstream URL">
            <Input
              value={form.upstream}
              onChange={(e) => onChange((d) => void (d.upstream = e.target.value))}
              spellCheck={false}
            />
          </FormField>
          <FormField label="SOCKS5 proxy">
            <Input
              value={form.socks5Proxy}
              onChange={(e) => onChange((d) => void (d.socks5Proxy = e.target.value))}
              placeholder="socks5://127.0.0.1:1080"
              spellCheck={false}
              autoComplete="off"
            />
          </FormField>
        </div>
      </Panel>
      {/* The Android app fixes the listener to its native localhost server. */}
      {!IS_ANDROID_BUILD && (
        <Panel title="Local listener">
          <div className="grid gap-3">
            <div className="grid grid-cols-[1fr_auto] gap-3">
              <FormField label="Host">
                <Input
                  value={form.server.host}
                  onChange={(e) => onChange((d) => void (d.server.host = e.target.value))}
                  spellCheck={false}
                />
              </FormField>
              <FormField label="Port">
                <Input
                  type="number"
                  min={1}
                  max={65535}
                  className="w-24"
                  value={form.server.port}
                  onChange={(e) => onChange((d) => void (d.server.port = Number(e.target.value)))}
                />
              </FormField>
            </div>
            <FormField label="Admin assets path">
              <Input
                value={form.server.adminBasePath}
                onChange={(e) => onChange((d) => void (d.server.adminBasePath = e.target.value))}
                spellCheck={false}
              />
            </FormField>
            <p className="text-xs text-muted-foreground">
              Host or port changes need a server restart; upstream and proxy apply instantly.
            </p>
          </div>
        </Panel>
      )}
    </div>
  );
}