import { FormField } from "@/components/shared/FormField";
import { Panel } from "@/components/shared/Panel";
import { Input } from "@/components/ui/input";
import { Switch } from "@/components/ui/switch";
import type { AppConfig, DesktopSettings } from "@/types";

interface Props {
  form: AppConfig;
  onChange: (mutator: (draft: AppConfig) => void) => void;
}

const DEFAULTS: DesktopSettings = {
  hardwareAcceleration: true,
  windowWidth: 1280,
  windowHeight: 800,
  rememberWindowSize: true,
};

// Desktop-only tab. The desktop reuses the web bundle, so it is shown by runtime
// detection (isDesktopRuntime) rather than a build flag.
export function DesktopTab({ form, onChange }: Props) {
  const cfg = form.desktop ?? DEFAULTS;
  // Mutate via a single helper so older configs that predate the section get a
  // fully-populated object on first edit.
  const set = (patch: Partial<DesktopSettings>) =>
    onChange((d) => void (d.desktop = { ...DEFAULTS, ...d.desktop, ...patch }));

  return (
    <div className="grid max-w-2xl gap-3">
      <Panel title="Native app">
        <p className="text-xs text-muted-foreground">
          Running inside the Studio Bondage Club desktop app. Server, storage and listener settings
          are managed by the native app and hidden here.
        </p>
      </Panel>
      <Panel title="Window">
        <div className="grid grid-cols-2 gap-3">
          <FormField label="Width (px)">
            <Input
              type="number"
              min={1}
              value={cfg.windowWidth}
              onChange={(e) => set({ windowWidth: Number(e.target.value) })}
            />
          </FormField>
          <FormField label="Height (px)">
            <Input
              type="number"
              min={1}
              value={cfg.windowHeight}
              onChange={(e) => set({ windowHeight: Number(e.target.value) })}
            />
          </FormField>
        </div>
        <div className="mt-3 flex items-start justify-between gap-4">
          <div className="space-y-0.5">
            <p className="text-sm font-medium">Remember window size</p>
            <p className="text-xs text-muted-foreground">
              Save the window size back into the config when you resize the desktop window.
            </p>
          </div>
          <Switch
            checked={cfg.rememberWindowSize}
            onCheckedChange={(checked) => set({ rememberWindowSize: checked })}
          />
        </div>
      </Panel>
      <Panel title="Rendering">
        <div className="flex items-start justify-between gap-4">
          <div className="space-y-0.5">
            <p className="text-sm font-medium">Hardware acceleration</p>
            <p className="text-xs text-muted-foreground">
              Use GPU compositing in the Chromium window. Improves the game's frame rate on most
              machines. Turn it off if you see flickering or corrupted graphics. Takes effect after
              restarting the app.
            </p>
          </div>
          <Switch
            checked={cfg.hardwareAcceleration}
            onCheckedChange={(checked) => set({ hardwareAcceleration: checked })}
          />
        </div>
      </Panel>
    </div>
  );
}
