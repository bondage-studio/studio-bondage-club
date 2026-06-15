import { Panel } from "@/components/shared/Panel";
import { Switch } from "@/components/ui/switch";
import type { AppConfig } from "@/types";

interface Props {
  form: AppConfig;
  onChange: (mutator: (draft: AppConfig) => void) => void;
}

// Android-only tab. Only built into the Android bundle.
export function AndroidTab({ form, onChange }: Props) {
  // Older config files predate the field; treat absence as enabled (the default).
  const hardwareAcceleration = form.android?.hardwareAcceleration ?? true;

  return (
    <div className="grid max-w-2xl gap-3">
      <Panel title="Native app">
        <p className="text-xs text-muted-foreground">
          Running inside the Studio Bondage Club Android app. Server, storage and listener settings
          are managed by the native app and hidden here.
        </p>
      </Panel>
      <Panel title="Rendering">
        <div className="flex items-start justify-between gap-4">
          <div className="space-y-0.5">
            <p className="text-sm font-medium">Hardware acceleration</p>
            <p className="text-xs text-muted-foreground">
              Force GPU rendering in the bundled-browser (GeckoView) build — WebRender compositing
              and GPU-accelerated 2D canvas. Improves the game's frame rate on most devices. Turn it
              off if you see flickering or corrupted graphics. Takes effect after restarting the
              app, and has no effect on the system-WebView build.
            </p>
          </div>
          <Switch
            checked={hardwareAcceleration}
            onCheckedChange={(checked) =>
              onChange((d) => void (d.android = { hardwareAcceleration: checked }))
            }
          />
        </div>
      </Panel>
    </div>
  );
}
