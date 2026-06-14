// Platform flags. Two layers, deliberately separate:
//
//   - PLATFORM / IS_ANDROID_BUILD: build-time. Set via Vite mode
//     (`vite build --mode android` → VITE_PLATFORM=android from .env.android).
//     Vite inlines import.meta.env.VITE_PLATFORM as a string literal, so
//     branches guarded by IS_ANDROID_BUILD are tree-shaken out of the bundle
//     that doesn't need them. Use this to strip whole features from a build.
//
//   - isAndroidRuntime(): runtime. The Android WebView host (MainActivity)
//     tags its user agent with "StudioBC-Android". Use this for dynamic tweaks
//     where a single bundle may run in either host.
export const PLATFORM: "web" | "android" =
  import.meta.env.VITE_PLATFORM === "android" ? "android" : "web";

export const IS_ANDROID_BUILD = PLATFORM === "android";

export function isAndroidRuntime(): boolean {
  return navigator.userAgent.includes("StudioBC-Android");
}
