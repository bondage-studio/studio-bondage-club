import { Panel } from "../shared/Panel";

// Android-only tab. Only built into the Android bundle (gated by androidOnly in
// App.tsx → IS_ANDROID_BUILD). Scaffold for native-app-specific controls; wire
// real settings here as they appear.
export function AndroidTab() {
  return (
    <div className="grid max-w-2xl gap-3">
      <Panel title="Native app">
        <p className="text-xs text-muted-foreground">
          Running inside the Studio Bondage Club Android app. Server, storage and
          listener settings are managed by the native app and hidden here.
        </p>
      </Panel>
    </div>
  );
}
