# Linux desktop client (CEF)

A native Linux desktop app for `studio-bondage-club`. It bundles the in-process
embedded server (`sbc_core`) and a Chromium window via
[CEF](https://bitbucket.org/chromiumembedded/cef) into one executable, so the
game launches like an app instead of "start the CLI, then open a browser".

## Design — a native host, like Android

The client is a *native host*, mirroring the Android app (`android/`) rather than
a plain browser-window wrapper. Three things are enforced by the host (C++), not
by page JavaScript:

1. **Request interception** — cross-origin `GET`/`HEAD` are rewritten to the
   local server's `/<full-url>` loader (cache + Origin/Referer spoofing), so the
   page never installs a service worker. See `SbcClient::OnBeforeResourceLoad`.
2. **Native RPC bridge** — `window.__sbcNativeRpc` (`{ postMessage, onmessage }`)
   is injected into each page and relayed straight into
   `EmbeddedServer::deliver_rpc_frame`, bypassing the localhost WebSocket. Every
   frame carries the capability token the backend verifies. See
   `SbcRenderProcessHandler` (injection, mirroring the Android gecko content
   script) and `SbcClient` (browser-side relay).
3. **Service-worker skip** — the web bundle detects the `StudioBC-Desktop` user
   agent (`isNativeRuntime()`) and disables the SW + uses the bridge transport.

The same `web/dist` (web mode) the CLI serves is reused — the bundle adapts at
runtime, so a plain browser hitting the same server still gets the SW + WebSocket.

## Layout

| File | Responsibility |
| --- | --- |
| `src/main.cpp` | subprocess dispatch, server start, `CefInitialize`, message loop |
| `src/sbc_app.*` | `CefApp`: creates the window; hands back the render handler |
| `src/sbc_render_process.*` | renderer: injects `__sbcNativeRpc`, page⇄native IPC |
| `src/sbc_client.*` | browser: lifespan, title, interception, native RPC relay |
| `src/window_delegate.*` | Views top-level window hosting the browser view |
| `src/sbc_ipc.hpp` | shared `CefProcessMessage` names |
| `cmake/DownloadCEF.cmake` | fetch + verify + extract the CEF distribution |

## Building & running

CEF is **auto-downloaded** at configure time (the minimal linux64/linuxarm64
distribution, verified against its published SHA1, cached under
`desktop/third_party/cef/`).

```sh
# Build the web panel first (the server serves web/dist):
cd web && npm install && npm run build && cd ..

# Configure + build the client (downloads CEF on first configure):
cmake --preset linux-desktop
cmake --build build/linux-desktop

# Run it (libcef.so + resources are staged next to the binary):
./build/linux-desktop/desktop/studio-bondage-club-desktop
#   -config <path>   optional; defaults to <user-config-dir>/studio-bondage-club/config.json
```

### Options

- `-DCEF_VERSION=<build string>` — pin a specific CEF build instead of resolving
  the latest stable from the CDN index (e.g.
  `120.1.10+g3ce3184+chromium-120.0.6099.129`).
- `-DCEF_ROOT=<dir>` — use a pre-extracted CEF SDK and skip the download entirely.

### Notes

- Runs with `no_sandbox` (no SUID `chrome-sandbox` helper to install). Shipping
  the sandbox is a future hardening step.
- Single executable: it re-launches itself for Chromium's renderer/gpu/utility
  subprocesses (`CefExecuteProcess`), so only the browser process starts the
  server.
- Linux-only for now (macOS/Windows clients are not built here).
