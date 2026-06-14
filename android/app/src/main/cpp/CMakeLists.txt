# Top-level CMake project for the Android native build. Gradle's
# externalNativeBuild invokes this once per ABI with the NDK toolchain (vcpkg is
# chainloaded onto it from app/build.gradle). It pulls in the full server core
# from the repo and wraps it in the JNI shared library the app loads.
cmake_minimum_required(VERSION 3.21)
project(sbc_android CXX)

# cpp -> main -> src -> app -> android -> <repo root>
get_filename_component(SBC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)

# Force the server core's build options for Android: embed web/dist into the .so
# (no on-device asset files) and skip the host-only test target.
set(SBC_EMBED_WEB ON CACHE BOOL "" FORCE)
set(SBC_BUILD_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory("${SBC_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/sbc")

add_library(sbc_jni SHARED jni_bridge.cpp)
target_link_libraries(sbc_jni PRIVATE sbc_core)

# liblog backs spdlog's Android sink (__android_log_write).
find_library(log-lib log)
target_link_libraries(sbc_jni PRIVATE ${log-lib})
