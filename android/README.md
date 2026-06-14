# Studio Bondage Club — Android app

A WebView host that runs the C++ server in-process and browses it. The web
service worker is replaced by a native `shouldInterceptRequest` hook (see
`MainActivity.kt`), which is more reliable inside a WebView since the server
lives in the same process.

## How it fits together

- `app/src/main/cpp/CMakeLists.txt` is the top-level CMake project Gradle builds.
  It `add_subdirectory`s the repo root to get `sbc_core` (built with
  `SBC_EMBED_WEB=ON`, so `web/dist` is baked into the `.so`) and wraps it in
  `libsbc_jni.so`.
- `app/build.gradle` drives that build via `externalNativeBuild`, chainloading
  the vendored vcpkg (`../.vcpkg`) onto the NDK toolchain so Boost/OpenSSL/LevelDB
  build for each ABI (`arm64-v8a`, `x86_64`).
- `NativeServer.kt` ⇄ `jni_bridge.cpp` start/stop the server; `MainActivity.kt`
  hosts the WebView and the cross-origin → local-proxy intercept hook.

## Prerequisites

- Android SDK with `compileSdk 34`, **NDK `26.3.11579264`**, CMake `3.22.1+`.
  Point Gradle at the SDK via `ANDROID_SDK_ROOT` or an `android/local.properties`
  containing `sdk.dir=/path/to/Android/sdk`.
- Node.js (the build runs `npm install && npm run build` in `../web`).
- Python 3 (the embed step generates the asset translation unit).

## Build

```sh
cd android
./gradlew assembleDebug
# -> app/build/outputs/apk/debug/app-debug.apk
```

The first build is slow: vcpkg compiles all native dependencies from source once
per ABI. Subsequent builds reuse the vcpkg binary cache.

## Install & run

```sh
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb logcat -s StudioBC        # server logs (spdlog -> logcat)
```

Inspect the WebView from desktop Chrome at `chrome://inspect`.
