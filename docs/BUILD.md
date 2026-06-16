# Build guide

This document explains how to build `studio-bondage-club`, and in particular how
to produce a **statically linked** build (a self-contained binary) versus a
**dynamic runtime** build (one that links the shared system C/C++ runtime).

- [Prerequisites](#prerequisites)
- [Two linkage axes](#two-linkage-axes)
- [Quick start](#quick-start)
- [Static-linked version](#static-linked-version)
- [Dynamic-runtime version](#dynamic-runtime-version)
- [Verifying the linkage](#verifying-the-linkage)
- [Build options reference](#build-options-reference)
- [Presets reference](#presets-reference)

## Prerequisites

- CMake ≥ 3.21 and [Ninja](https://ninja-build.org/)
- A C++20 compiler (MSVC 2022, GCC ≥ 11, or Clang ≥ 14 / Apple Clang)
- [vcpkg](https://github.com/microsoft/vcpkg) for the C++ dependencies
  (`boost-beast`, `boost-asio`, `boost-url`, `openssl`, `leveldb`,
  `nlohmann-json`, `spdlog`)

One-time vcpkg bootstrap (vendored under `./.vcpkg`, which the presets expect):

```sh
git clone https://github.com/microsoft/vcpkg .vcpkg
./.vcpkg/bootstrap-vcpkg.sh      # bootstrap-vcpkg.bat on Windows
```

## Two linkage axes

"Static vs dynamic" actually has **two independent knobs**. Understanding them is
the key to choosing the right build:

| Axis | What it controls | How you set it |
| --- | --- | --- |
| **Dependency linkage** | Whether Boost/OpenSSL/LevelDB/… are baked in (`.a`/`.lib`) or loaded at runtime (`.so`/`.dylib`/`.dll`) | The **vcpkg triplet** (`VCPKG_TARGET_TRIPLET` / `VCPKG_LIBRARY_LINKAGE`) |
| **Runtime linkage** | Whether the C/C++ standard runtime (libstdc++/libgcc, or the MSVC CRT) is baked in | The CMake option **`SBC_STATIC_RUNTIME`** (and, on MSVC, `CMAKE_MSVC_RUNTIME_LIBRARY`, which the option drives) |

What the stock vcpkg triplets give you out of the box:

| Triplet | Dependencies | Runtime (CRT) |
| --- | --- | --- |
| `x64-linux`, `arm64-osx` (default on Linux/macOS) | **static** | dynamic |
| `x64-windows` (default on Windows) | dynamic | dynamic |
| `x64-windows-static` | **static** | **static** |
| `x64-windows-static-md` | **static** | dynamic |

So on **Linux/macOS the dependencies are already linked statically** by default —
only the C/C++ runtime is shared. On **Windows the default is fully dynamic**, and
you opt into static linkage by choosing a `*-static` triplet.

## Quick start

```sh
cmake --preset default          # static deps + dynamic runtime (Release)
cmake --build build/default
ctest --preset default          # or: ./build/default/studio-bondage-club
```

Build the web panel once (the server serves `web/dist/`):

```sh
cd web && npm install && npm run build
```

## Static-linked version

A static build links the dependencies **and** the C/C++ runtime into the
executable, so it runs on machines without the matching shared libraries
installed. Use it for distribution.

### Linux

The default triplet already links the dependencies statically; add
`SBC_STATIC_RUNTIME=ON` to bake in libstdc++/libgcc as well:

```sh
cmake --preset static
cmake --build build/static
```

> **Fully static (incl. libc).** To statically link glibc too, add
> `-DCMAKE_EXE_LINKER_FLAGS=-static`. This is **not recommended** for this
> server: glibc's NSS/DNS resolver (`getaddrinfo`) does not work reliably when
> statically linked. Prefer a [musl](https://musl.libc.org/)-based toolchain
> (e.g. Alpine) if you need a truly static binary.

### Windows (MSVC)

Use the static triplet, which links both the dependencies and the MSVC CRT
statically (`/MT`). The `SBC_STATIC_RUNTIME=ON` carried by the preset keeps the
CRT static:

```bat
cmake --preset static-windows
cmake --build build/static-windows --config Release
```

Equivalent manual invocation:

```bat
cmake -B build/static -G Ninja ^
  -DCMAKE_TOOLCHAIN_FILE=.vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
  -DSBC_STATIC_RUNTIME=ON
cmake --build build/static --config Release
```

### macOS

macOS ships **no static `libSystem`/libc**, so a fully static binary is not
possible. The default triplet already links the vcpkg dependencies statically;
`SBC_STATIC_RUNTIME` is a no-op on Apple platforms (libc++ stays shared, which is
fine — it is part of the OS):

```sh
cmake --preset static       # deps static; libc++/libSystem remain shared
cmake --build build/static
```

## Dynamic-runtime version

A dynamic-runtime build links against the **shared** C/C++ runtime (and,
typically, shared dependencies). The binary is smaller and picks up runtime/OpenSSL
security updates from the system, at the cost of requiring those libraries to be
present at runtime.

### Windows (MSVC)

The default `x64-windows` triplet gives shared dependencies and the dynamic CRT
(`/MD`):

```bat
cmake --preset dynamic-windows
cmake --build build/dynamic-windows --config Release
```

If you want **static dependencies but a dynamic CRT** (a common distribution
choice — no extra DLLs except the OS-provided VC++ runtime), use the
`x64-windows-static-md` triplet instead:

```bat
cmake -B build/dyn-md -G Ninja ^
  -DCMAKE_TOOLCHAIN_FILE=.vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md ^
  -DSBC_STATIC_RUNTIME=OFF
cmake --build build/dyn-md --config Release
```

### Linux

The default triplet already uses the dynamic runtime — just leave
`SBC_STATIC_RUNTIME=OFF` (the default):

```sh
cmake --preset dynamic
cmake --build build/dynamic
```

To also link the **dependencies** dynamically, build vcpkg with a dynamic
triplet (e.g. `x64-linux-dynamic`), or install the libraries from your
distribution and configure without the vcpkg toolchain so CMake's `find_package`
picks up the system shared libraries.

### macOS

The default build already links the runtime dynamically. To link the
dependencies dynamically too, use Homebrew packages via the `system` preset (no
vcpkg):

```sh
brew install boost openssl@3 leveldb nlohmann-json spdlog snappy
cmake --preset system
cmake --build build/system
```

## Verifying the linkage

Check what the produced binary actually depends on:

```sh
# Linux
ldd build/static/studio-bondage-club

# macOS
otool -L build/static/studio-bondage-club
```

```bat
:: Windows (from a Developer Command Prompt)
dumpbin /dependents build\static-windows\studio-bondage-club.exe
```

A fully static binary lists only the OS loader / kernel libs; a dynamic build
lists `libstdc++`, `libssl`/`libcrypto`, `VCRUNTIME140.dll`, etc.

## Build options reference

| Option | Default | Effect |
| --- | --- | --- |
| `SBC_STATIC_RUNTIME` | `OFF` | Statically link the C/C++ runtime (libstdc++/libgcc, or the MSVC CRT via `/MT`). No-op on macOS. |
| `SBC_EMBED_WEB` | `OFF` | Compile `web/dist/` into the executable so it needs no `web/dist` directory at runtime. |
| `SBC_BUILD_TESTS` | `ON` | Build the `sbc_tests` target and register it with CTest. |
| `SBC_BUILD_CEF_CLIENT` | `OFF` | Build the Linux CEF desktop client (`desktop/`). Auto-downloads CEF; Linux-only. See below. |
| `CMAKE_INTERPROCEDURAL_OPTIMIZATION` | `OFF` | Enable link-time optimisation (LTO) for smaller/faster Release builds. |
| `VCPKG_TARGET_TRIPLET` | host default | Selects the dependency linkage (see the triplet table above). |

Combine freely, e.g. a self-contained single-file distribution build:

```sh
cmake --preset static -DSBC_EMBED_WEB=ON -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON
cmake --build build/static
```

## Presets reference

| Configure preset | Triplet | Runtime | Notes |
| --- | --- | --- | --- |
| `default` | host default (static deps on Linux/macOS) | dynamic | Release; everyday build |
| `debug` | host default | dynamic | Debug build |
| `static` | host default | **static** (`SBC_STATIC_RUNTIME=ON`) | Self-contained; deps already static on Linux/macOS |
| `static-windows` | `x64-windows-static` | **static** | Windows-only (host-gated); fully static deps + CRT |
| `dynamic` | host default | dynamic | Explicit shared runtime |
| `dynamic-windows` | `x64-windows` | dynamic | Windows-only; shared deps + shared CRT |
| `linux-desktop` | host default | dynamic | Linux-only; builds the CEF desktop client (`SBC_BUILD_CEF_CLIENT=ON`) |
| `system` | none (Homebrew) | dynamic | macOS, no vcpkg |

Run any of them with `cmake --preset <name>` then `cmake --build build/<name>`.

## Desktop client (Linux, CEF)

The `desktop/` module builds `studio-bondage-club-desktop`: a native Linux app
that bundles the embedded server and a Chromium window (via the Chromium Embedded
Framework) into one executable, so the game launches like an app. It is a
*native host* like the Android app — it intercepts cross-origin requests through
the local loader and drives RPC over an injected `window.__sbcNativeRpc` bridge
(see `desktop/README.md`).

CEF is **auto-downloaded** at configure time (the minimal linux64/linuxarm64
distribution, SHA1-verified, cached under `desktop/third_party/cef/`):

```sh
cd web && npm install && npm run build && cd ..   # the server serves web/dist
cmake --preset linux-desktop                       # downloads CEF on first configure
cmake --build build/linux-desktop
./build/linux-desktop/desktop/studio-bondage-club-desktop
```

- `-DCEF_VERSION=<build>` pins a CEF build instead of resolving the latest stable.
- `-DCEF_ROOT=<dir>` consumes a pre-extracted CEF SDK and skips the download.

`libcef.so` and the CEF resources are staged next to the binary, found at runtime
via an `$ORIGIN` rpath.