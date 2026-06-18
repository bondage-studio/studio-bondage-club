cmake_minimum_required(VERSION 3.21)

# CMP0091: select the MSVC runtime via CMAKE_MSVC_RUNTIME_LIBRARY (abstract,
# generator-expression friendly) instead of munging CMAKE_CXX_FLAGS. Must be set
# before project(). Default is NEW at 3.21, but make the intent explicit so the
# SBC_STATIC_RUNTIME option below works regardless of policy defaults.
cmake_policy(SET CMP0091 NEW)

# CMP0167 (CMake 3.30+): the built-in FindBoost module is removed in favor of
# the BoostConfig.cmake config package that Boost itself ships (>= 1.70). Opt in
# so find_package(Boost) uses Boost's own config package. Guarded because the
# policy does not exist on older CMake, where setting an unknown policy errors.
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

project(studio-bondage-club CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for clangd / IDE tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    Debug Release RelWithDebInfo MinSizeRel)
endif()

option(SBC_EMBED_WEB     "Embed web/dist into the binary instead of reading from disk" OFF)
option(SBC_BUILD_TESTS   "Build unit tests" ON)

option(SBC_BUILD_CEF_CLIENT "Build the Linux CEF desktop client (desktop/)" OFF)

# Runtime (C/C++ standard library) linkage. This is independent of how the vcpkg
# dependencies are linked — that is governed by the vcpkg triplet (see docs/BUILD.md).
#   ON -> static runtime: libstdc++/libgcc (GCC) or the static MSVC CRT are baked
#         into the binary, giving a self-contained executable.
#  OFF -> dynamic runtime: the binary links the shared system C/C++ runtime.
# Note: macOS has no static libSystem, so this is a no-op on Apple platforms.
option(SBC_STATIC_RUNTIME "Statically link the C/C++ runtime (libstdc++/libgcc, MSVC CRT)" OFF)

# Fully static link, including libc itself (-static). Intended for musl/Linux,
# where a static libc yields a self-contained binary that runs on any distro.
# Not for glibc (static glibc is fragile) and a no-op on Apple/MSVC.
option(SBC_FULLY_STATIC "Link the executable fully statically, including libc (-static; musl/Linux)" OFF)

if(SBC_STATIC_RUNTIME AND MSVC)
  # MultiThreaded[Debug]  -> /MT[d]  (static CRT)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
elseif(MSVC)
  # MultiThreaded[Debug]DLL -> /MD[d] (dynamic CRT, the MSVC default)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

find_package(Boost REQUIRED COMPONENTS system url)
# TLS + crypto backend: OpenSSL everywhere except Windows, where we use the
# OS-native Schannel (via the header-only wintls wrapper) and CNG (bcrypt) so no
# OpenSSL is linked or shipped. wintls is header-only with no CMake config, so it
# is consumed purely through the vcpkg include path + the system libs below.
if(NOT WIN32)
  if(APPLE AND NOT OPENSSL_ROOT_DIR AND NOT DEFINED CACHE{OPENSSL_ROOT_DIR})
    find_program(BREW_EXECUTABLE brew)
    if(BREW_EXECUTABLE)
      execute_process(
        COMMAND "${BREW_EXECUTABLE}" --prefix openssl@3
        OUTPUT_VARIABLE _brew_openssl_prefix
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
      if(_brew_openssl_prefix AND IS_DIRECTORY "${_brew_openssl_prefix}")
        set(OPENSSL_ROOT_DIR "${_brew_openssl_prefix}" CACHE PATH "OpenSSL root (Homebrew)")
        message(STATUS "Using Homebrew OpenSSL at ${OPENSSL_ROOT_DIR}")
      endif()
    endif()
  endif()
  find_package(OpenSSL REQUIRED)
endif()
find_package(leveldb CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(Threads REQUIRED)
# Content-Encoding decoders for upstream fetches: gzip/deflate via zlib, brotli
# via the vcpkg "unofficial-brotli" config (decode-only — brotlidec + the shared
# brotlicommon; the encoder is not linked since we cache/serve identity bodies).
find_package(ZLIB REQUIRED)
find_package(unofficial-brotli CONFIG REQUIRED)

file(GLOB_RECURSE SBC_CORE_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(REMOVE_ITEM SBC_CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

# Drop platform-specific path implementations we are not building for.
if(NOT WIN32)
  list(FILTER SBC_CORE_SOURCES EXCLUDE REGEX "platform/paths_win\\.cpp$")
endif()
if(NOT ANDROID)
  list(FILTER SBC_CORE_SOURCES EXCLUDE REGEX "platform/paths_android\\.cpp$")
endif()
if(WIN32 OR ANDROID)
  list(FILTER SBC_CORE_SOURCES EXCLUDE REGEX "platform/paths_posix\\.cpp$")
endif()

add_library(sbc_core STATIC ${SBC_CORE_SOURCES})
target_include_directories(sbc_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(sbc_core PUBLIC cxx_std_20)
target_link_libraries(sbc_core PUBLIC
  Boost::system
  Boost::url
  leveldb::leveldb
  nlohmann_json::nlohmann_json
  spdlog::spdlog
  Threads::Threads
  ZLIB::ZLIB
  unofficial::brotli::brotlidec
  unofficial::brotli::brotlicommon)

if(WIN32)
  # Schannel (Secur32/Crypt32) backs wintls; bcrypt/CNG backs SHA-256 + PBKDF2.
  target_link_libraries(sbc_core PUBLIC Secur32 Crypt32 bcrypt)
else()
  target_link_libraries(sbc_core PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()

target_compile_definitions(sbc_core PUBLIC
  BOOST_ASIO_NO_DEPRECATED
  BOOST_BEAST_USE_STD_STRING_VIEW)

# Native RPC bridge: the in-process channel that lets a native host (the Android
# WebView, the desktop CEF window) drive the RPC dispatcher directly, bypassing
# the localhost WebSocket. Compiled into sbc_core for those hosts only.
if(ANDROID OR SBC_BUILD_CEF_CLIENT)
  target_compile_definitions(sbc_core PUBLIC SBC_NATIVE_RPC)
endif()

# Desktop-only config (window geometry, GPU compositing) and the in-process
# config-change forwarder the CEF host uses to react to edits. Compiled into
# sbc_core only for the desktop build (a CLI built in the same configure shares
# this sbc_core and carries the unused section, which is harmless).
if(SBC_BUILD_CEF_CLIENT)
  target_compile_definitions(sbc_core PUBLIC SBC_DESKTOP)
endif()

if(WIN32)
  target_compile_definitions(sbc_core PUBLIC _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

if(MSVC)
  target_compile_options(sbc_core PUBLIC /bigobj)
endif()

if(SBC_EMBED_WEB)
  include(cmake/EmbedWebAssets.cmake)
  embed_web_assets(sbc_core "${CMAKE_CURRENT_SOURCE_DIR}/web/dist")
  target_compile_definitions(sbc_core PUBLIC SBC_EMBED_WEB=1)
endif()

# Executable. On Android the only consumer of sbc_core is the JNI shared library
# (see android/app/src/main/cpp/CMakeLists.txt), which provides its own entry
# point — the desktop CLI executable, with its signal-driven wait loop, is not
# built there.
if(NOT ANDROID)
  add_executable(studio-bondage-club "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
  target_link_libraries(studio-bondage-club PRIVATE sbc_core)
endif()

# Runtime linkage (C/C++ standard library)
# MSVC is handled via CMAKE_MSVC_RUNTIME_LIBRARY above. For GCC / non-Apple Clang
# we bundle the C++ runtime into the executable. macOS ships no static libSystem,
# so a static runtime is not supported there and the flags are skipped.
function(sbc_apply_static_runtime target)
  if(SBC_STATIC_RUNTIME AND NOT MSVC AND NOT APPLE)
    target_link_options(${target} PRIVATE -static-libgcc -static-libstdc++)
  endif()
  # Fully static (libc included). -static implies the runtime is static too, so
  # it subsumes the flags above; harmless when both are set.
  if(SBC_FULLY_STATIC AND NOT MSVC AND NOT APPLE)
    target_link_options(${target} PRIVATE -static)
  endif()
endfunction()
if(NOT ANDROID)
  sbc_apply_static_runtime(studio-bondage-club)
endif()

# Release size / speed optimisations
# Generator expressions keep these correct for both single- and multi-config
# generators (Ninja, Makefiles, Visual Studio, Ninja Multi-Config), where
# CMAKE_BUILD_TYPE is empty and the config is only known at build time.
set(_sbc_release_cfg "$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>,$<CONFIG:RelWithDebInfo>>")

if(NOT MSVC)
  # Hide symbols not explicitly exported and split functions/data into their own
  # sections so the linker can dead-strip everything unreferenced.
  target_compile_options(sbc_core PRIVATE
    $<${_sbc_release_cfg}:-fvisibility=hidden -ffunction-sections -fdata-sections>)
endif()

# Executable-only link options. Skipped on Android, where the desktop CLI
# executable is not built (the JNI shared library carries its own link flags).
if(NOT ANDROID)
  if(APPLE)
    # -dead_strip: drop unreferenced sections; -S: strip debug symbols.
    target_link_options(studio-bondage-club PRIVATE
      $<${_sbc_release_cfg}:-Wl,-dead_strip>
      $<$<CONFIG:Release,MinSizeRel>:-Wl,-S>)
  elseif(MSVC)
    # /OPT:REF eliminates unreferenced code/data; /OPT:ICF folds identical COMDATs.
    target_link_options(studio-bondage-club PRIVATE
      $<${_sbc_release_cfg}:/OPT:REF /OPT:ICF>)
  elseif(UNIX)
    target_link_options(studio-bondage-club PRIVATE
      $<${_sbc_release_cfg}:-Wl,--gc-sections>
      $<$<CONFIG:Release,MinSizeRel>:-Wl,-s>)
  endif()
endif()

# Link-time optimisation (inter-procedural). Opt in with:
#   cmake … -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON
if(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
  set_target_properties(sbc_core PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
  if(NOT ANDROID)
    set_target_properties(studio-bondage-club
      PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()
endif()

# Tests (host-only — not built for Android, which has no test runner on device)
if(SBC_BUILD_TESTS AND NOT ANDROID AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
  enable_testing()
  file(GLOB SBC_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
  if(SBC_TEST_SOURCES)
    add_executable(sbc_tests ${SBC_TEST_SOURCES})
    target_link_libraries(sbc_tests PRIVATE sbc_core)
    sbc_apply_static_runtime(sbc_tests)
    add_test(NAME sbc_tests COMMAND sbc_tests)
  endif()
endif()

if(SBC_BUILD_CEF_CLIENT)
  if(NOT (UNIX AND NOT APPLE AND NOT ANDROID))
    message(FATAL_ERROR "SBC_BUILD_CEF_CLIENT is currently supported on Linux only")
  endif()
  add_subdirectory(desktop)
endif()
