# studio-bondage-club (C++)

A local reverse-proxy HTTP cache and admin panel for the Bondage Club browser
game, rewritten in modern C++ (C++20, RAII) from the original Go implementation.

It runs a local HTTP server that:

- reverse-proxies game asset requests to an upstream mirror and caches responses
  on disk (LevelDB), with ETag/Last-Modified revalidation, LRU eviction,
  policy-based routing, request coalescing, and stale-on-error fallback;
- proxies `/socket.io/` (HTTP long-poll **and** WebSocket) to the real game
  server, spoofing `Origin`/`Referer` so the origin-aware server accepts it;
- exposes a remote loader (`/https://example.com/...`) used by the browser
  service worker to route cross-origin fetches through the cache;
- serves the React 19 admin panel (unchanged from the original `web/`) and a
  REST + SSE API under `/api/`.

## Architecture

| Module | Responsibility |
| --- | --- |
| `src/common` | logging, URL (Boost.URL), SHA-256 (OpenSSL), HTTP header utilities |
| `src/net` | Asio io runtime, blocking-work pool, TLS context, HTTP client, SOCKS5 |
| `src/cache` | LevelDB store, policy/router, cache key, flight coalescing |
| `src/config` | config structs, validation, JSON, atomic store |
| `src/server` | HTTP server/session, App router, API/SSE, static assets, homepage, socket.io + WebSocket relay |
| `src/host` | provider interface; reverse-proxy and package-mode providers |
| `src/platform` | per-OS path resolution (Android-isolation seam) |

Networking is fully asynchronous using Boost.Beast/Asio with C++20 coroutines.
Blocking LevelDB/file I/O is offloaded to a dedicated thread pool. Config
hot-reload uses lock-free snapshots in three tiers (live / recreate / restart).

## Building

Requires CMake ≥ 3.21, a C++20 compiler, and [vcpkg](https://github.com/microsoft/vcpkg)
for dependencies (`boost-beast`, `boost-asio`, `boost-url`, `openssl`,
`leveldb`, `nlohmann-json`).

```sh
# One-time: bootstrap vcpkg (here vendored under ./.vcpkg)
git clone https://github.com/microsoft/vcpkg .vcpkg
./.vcpkg/bootstrap-vcpkg.sh

# Configure + build (CMakePresets uses ./.vcpkg as the toolchain)
cmake --preset default
cmake --build build/default

# Run tests
ctest --preset default        # or: ./build/default/sbc_tests
```

If your vcpkg lives elsewhere, configure manually:

```sh
cmake -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build
```

### Build the web panel

The C++ server serves `web/dist/`. Build it once with the existing frontend
toolchain:

```sh
cd web && npm install && npm run build
```

### Single self-contained binary (optional)

`-DSBC_EMBED_WEB=ON` compiles `web/dist/` into the executable (useful for
distribution and the future Android target), so it no longer needs the
`web/dist` directory at runtime:

```sh
cmake --preset default -DSBC_EMBED_WEB=ON
cmake --build build/default
```

## Running

```sh
./build/default/studio-bondage-club -config ./run/config.json
```

- `-config <path>` selects the JSON config file. When omitted, it defaults to
  `<user-config-dir>/studio-bondage-club/config.json`, auto-created on first run.
- The server listens on `http://127.0.0.1:8080/` by default. Open it in a
  browser to load the admin panel.
- By default `web/dist` is resolved relative to the working directory; override
  with the `SBC_WEB_ROOT` environment variable.

See `config.example.json` for a complete configuration with named stores and
cache rules.

## Cross-platform

Desktop (Linux/macOS/Windows) is the primary target. Platform-specific code is
isolated behind `src/platform`. Android (NDK) is a planned target; the embedded
web-assets option and the platform seam exist to support it.
