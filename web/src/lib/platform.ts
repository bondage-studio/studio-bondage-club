// Platform flags. Two layers, deliberately separate:
//
//   - PLATFORM / IS_ANDROID_BUILD: build-time. Set via Vite mode
//     (`vite build --mode android` -> VITE_PLATFORM=android from .env.android).
//     Vite inlines import.meta.env.VITE_PLATFORM as a string literal, so
//     branches guarded by IS_ANDROID_BUILD are tree-shaken out of the bundle
//     that doesn't need them. Use this to strip whole features from a build.
//
//   - isAndroidRuntime() / isNativeRuntime(): runtime. A native host tags its
//     user agent — the Android WebView (MainActivity) with "StudioBC-Android",
//     the desktop CEF window with "StudioBC-Desktop". Use these for dynamic
//     tweaks where a single bundle may run in a plain browser or a native host.
export const PLATFORM: "web" | "android" =
  import.meta.env.VITE_PLATFORM === "android" ? "android" : "web";

export const IS_ANDROID_BUILD = PLATFORM === "android";

export function isAndroidRuntime(): boolean {
  return navigator.userAgent.includes("StudioBC-Android");
}

// isNativeRuntime reports whether the bundle is running inside one of our native
// hosts (Android WebView or desktop CEF), as opposed to a plain browser. Native
// hosts intercept cross-origin requests themselves and drive RPC over an injected
// bridge, so the service worker and localhost WebSocket are redundant there.
export function isNativeRuntime(): boolean {
  return /StudioBC-(Android|Desktop)/.test(navigator.userAgent);
}
