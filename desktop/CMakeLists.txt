# Linux desktop client: a CEF (Chromium Embedded Framework) native host that
# bundles the embedded server (sbc_core) and a Chromium window into one app.
#
# Added to the build via the top-level `add_subdirectory(desktop)` guarded by
# the SBC_BUILD_CEF_CLIENT option (Linux-only). CEF is downloaded + verified at
# configure time by DownloadCEF.cmake, which sets CEF_ROOT.

# 1. Acquire CEF (sets CEF_ROOT).
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/DownloadCEF.cmake")

# 2. Load the CEF CMake helpers shipped inside the distribution. They define the
#    CEF_* variables/macros (CEF_STANDARD_LIBS, CEF_BINARY_FILES, COPY_FILES,
#    ADD_LOGICAL_TARGET, SET_EXECUTABLE_TARGET_PROPERTIES, ...).
#    cef_variables.cmake probes C compiler flags, so the C language must be on
#    (the top-level project enables CXX only).
enable_language(C)

if(NOT DEFINED PROJECT_ARCH)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(PROJECT_ARCH "arm64")
  elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PROJECT_ARCH "x86_64")
  else()
    set(PROJECT_ARCH "x86")
  endif()
endif()

list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
find_package(CEF REQUIRED)

# The minimal distribution may ship Release only; fall back so the logical lib
# target and Debug-config builds still resolve.
if(NOT CEF_LIB_DEBUG)
  set(CEF_LIB_DEBUG "${CEF_LIB_RELEASE}")
endif()

# 3. Build the libcef_dll_wrapper static library from the distribution sources,
#    and a logical target wrapping libcef.so.
add_subdirectory("${CEF_ROOT}/libcef_dll" libcef_dll_wrapper)
ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")

# Stage the executable and all CEF runtime files into one directory so libcef.so
# (found via the $ORIGIN rpath), the .pak/.dat resources and the .bin snapshots
# sit next to the binary.
set(CEF_TARGET_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")

# 4. The client executable.
file(GLOB SBC_DESKTOP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_executable(studio-bondage-club-desktop ${SBC_DESKTOP_SOURCES})
SET_EXECUTABLE_TARGET_PROPERTIES(studio-bondage-club-desktop)
add_dependencies(studio-bondage-club-desktop libcef_dll_wrapper)

# sbc_core (Boost.Asio coroutines) and our own try/catch around server startup
# need exceptions + RTTI; re-enable them in case the CEF flags disabled them. The
# CEF helper also injects -std=c++17, but the desktop sources now include sbc_core
# headers (config.hpp uses std::span, C++20); append -std=c++20 last so it wins.
target_compile_options(studio-bondage-club-desktop PRIVATE -fexceptions -frtti -std=c++20)

# Optional libdbus-1: the preferred notification backend talks to the daemon
# directly over D-Bus (desktop/src/dbus_notifier.cpp, gated on SBC_HAVE_DBUS).
# When absent the build still succeeds and falls back to the notify-send CLI.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(DBUS IMPORTED_TARGET dbus-1)
endif()
if(TARGET PkgConfig::DBUS)
  target_compile_definitions(studio-bondage-club-desktop PRIVATE SBC_HAVE_DBUS)
  target_link_libraries(studio-bondage-club-desktop PRIVATE PkgConfig::DBUS)
  message(STATUS "Desktop notifications: libdbus-1 backend (notify-send fallback)")
else()
  message(STATUS "Desktop notifications: notify-send backend (libdbus-1 not found)")
endif()

target_include_directories(studio-bondage-club-desktop PRIVATE
  "${CEF_ROOT}"
  "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(studio-bondage-club-desktop PRIVATE
  sbc_core
  libcef_lib
  libcef_dll_wrapper
  Threads::Threads
  ${CEF_STANDARD_LIBS})

# Find libcef.so beside the executable at runtime.
set_target_properties(studio-bondage-club-desktop PROPERTIES
  INSTALL_RPATH "$ORIGIN"
  BUILD_WITH_INSTALL_RPATH TRUE
  RUNTIME_OUTPUT_DIRECTORY "${CEF_TARGET_OUT_DIR}")

# Copy the CEF binaries (libcef.so, swiftshader, *.bin snapshots, chrome-sandbox)
# and resources (icudtl.dat, *.pak, locales/) next to the executable post-build.
COPY_FILES(studio-bondage-club-desktop "${CEF_BINARY_FILES}"   "${CEF_BINARY_DIR}"   "${CEF_TARGET_OUT_DIR}")
COPY_FILES(studio-bondage-club-desktop "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")

# Note: no SET_LINUX_SUID_PERMISSIONS for chrome-sandbox — the client runs with
# no_sandbox (see desktop/src/main.cpp), so the SUID helper is unused. Shipping
# the sandbox is a documented future hardening step.
