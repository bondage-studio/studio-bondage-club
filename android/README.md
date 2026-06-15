# Studio Bondage Club — Android app

A WebView host that runs the C++ server in-process and browses it. The web
service worker is replaced by a native `shouldInterceptRequest` hook (see
`src/system/.../MainActivity.kt`), which is more reliable inside a WebView since
the server lives in the same process.

## Build flavors (engine)

Two product flavors, installable side-by-side (distinct `applicationId`s):

- **`system`** — hosts the game in the device's Android System WebView. Small
  APK, but the game breaks if the device's WebView is too old. `applicationId`
  `com.studio.bondageclub`; activity in `src/system/`.
- **`gecko`** — bundles its own Firefox engine (GeckoView) inside the APK, so
  rendering is independent of the device's WebView. Much larger APK (~tens of MB
  per ABI). `applicationId` `com.studio.bondageclub.gecko`; activity in
  `src/gecko/`.

The gecko flavor does **not** use the native intercept hook — GeckoView supports
service workers, so the web bundle's own service worker handles cross-origin
asset caching (the same path the desktop build uses). This works because the
gecko activity keeps GeckoView's native UA and omits the `StudioBC-Android` tag
that would otherwise make the web bundle skip its service worker.

## How it fits together

- `app/src/main/cpp/CMakeLists.txt` is the top-level CMake project Gradle builds.
  It `add_subdirectory`s the repo root to get `sbc_core` (built with
  `SBC_EMBED_WEB=ON`, so `web/dist` is baked into the `.so`) and wraps it in
  `libsbc_jni.so`.
- `app/build.gradle` drives that build via `externalNativeBuild`, chainloading
  the vendored vcpkg (`../.vcpkg`) onto the NDK toolchain so Boost/OpenSSL/LevelDB
  build for each ABI (`arm64-v8a`, `x86_64`).
- `NativeServer.kt` (shared, in `src/main`) ⇄ `jni_bridge.cpp` start/stop the
  server. The per-flavor `MainActivity.kt` hosts the engine: `src/system` uses the
  WebView + cross-origin → local-proxy intercept hook; `src/gecko` uses GeckoView
  + the web service worker. Both flavors embed the same `web/dist` bundle.

## Prerequisites

- Android SDK with `compileSdk 34`, **NDK `26.3.11579264`**, CMake `3.22.1+`.
  Point Gradle at the SDK via `ANDROID_SDK_ROOT` or an `android/local.properties`
  containing `sdk.dir=/path/to/Android/sdk`.
- Node.js (the build runs `npm install && npm run build` in `../web`).
- Python 3 (the embed step generates the asset translation unit).

## Build

```sh
cd android
./gradlew assembleSystemDebug   # -> app/build/outputs/apk/system/debug/app-system-debug.apk
./gradlew assembleGeckoDebug    # -> app/build/outputs/apk/gecko/debug/app-gecko-debug.apk
# (./gradlew assembleDebug builds both flavors)
```

The first build is slow: vcpkg compiles all native dependencies from source once
per ABI. Subsequent builds reuse the vcpkg binary cache.

## Install & run

```sh
adb install -r app/build/outputs/apk/system/debug/app-system-debug.apk
adb install -r app/build/outputs/apk/gecko/debug/app-gecko-debug.apk
adb logcat -s StudioBC        # server logs (spdlog -> logcat)
```

Inspect the `system` flavor's WebView from desktop Chrome at `chrome://inspect`.
Inspect the `gecko` flavor from desktop Firefox at `about:debugging` (remote
debugging is enabled in the GeckoView runtime).
