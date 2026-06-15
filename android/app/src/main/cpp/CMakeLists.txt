# Gradle invokes this once per ABI. It builds the shared JNI wrapper around the
# server core.
cmake_minimum_required(VERSION 3.21)
project(sbc_android CXX)

get_filename_component(SBC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)

# Android embeds web/dist into the .so and has no host test runner.
set(SBC_EMBED_WEB ON CACHE BOOL "" FORCE)
set(SBC_BUILD_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory("${SBC_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/sbc")

add_library(sbc_jni SHARED jni_bridge.cpp)
target_link_libraries(sbc_jni PRIVATE sbc_core)

# liblog backs spdlog's Android sink (__android_log_write).
find_library(log-lib log)
target_link_libraries(sbc_jni PRIVATE ${log-lib})
