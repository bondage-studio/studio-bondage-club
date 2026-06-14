# CMake toolchain shim used as CMAKE_TOOLCHAIN_FILE for the Android build.
#
# vcpkg.cmake's triplet auto-detection falls back to the *host* processor for
# Android cross-builds (it doesn't map ANDROID_ABI), which would pick the wrong
# triplet. So we select the vcpkg android triplet from the per-ABI ANDROID_ABI
# that the Android Gradle Plugin passes (-DANDROID_ABI=...), then chainload the
# NDK toolchain through vcpkg and hand off to vcpkg.cmake.

if(NOT DEFINED VCPKG_TARGET_TRIPLET AND DEFINED ANDROID_ABI)
    if(ANDROID_ABI STREQUAL "arm64-v8a")
        set(VCPKG_TARGET_TRIPLET "arm64-android" CACHE STRING "")
    elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
        set(VCPKG_TARGET_TRIPLET "arm-neon-android" CACHE STRING "")
    elseif(ANDROID_ABI STREQUAL "x86_64")
        set(VCPKG_TARGET_TRIPLET "x64-android" CACHE STRING "")
    elseif(ANDROID_ABI STREQUAL "x86")
        set(VCPKG_TARGET_TRIPLET "x86-android" CACHE STRING "")
    endif()
endif()

# vcpkg.cmake includes this before its own triplet/arch detection, giving us the
# real Android cross-toolchain (CMAKE_SYSTEM_NAME=Android, the NDK compilers).
if(NOT DEFINED VCPKG_CHAINLOAD_TOOLCHAIN_FILE)
    if(DEFINED ANDROID_NDK)
        set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${ANDROID_NDK}/build/cmake/android.toolchain.cmake")
    elseif(DEFINED CMAKE_ANDROID_NDK)
        set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_ANDROID_NDK}/build/cmake/android.toolchain.cmake")
    endif()
endif()

# The chainload above only steers *this* (consumer) configure. vcpkg builds the
# manifest dependencies — and runs detect_compiler — in its own sub-processes
# using the triplet's toolchain, and the *-android triplets carry no chainload,
# so vcpkg falls back to its bundled scripts/toolchains/android.cmake. That
# toolchain locates the NDK solely via the ANDROID_NDK_HOME env var; without it
# vcpkg searches a bogus default path and aborts ("Could not find android ndk").
# Export it (inherited by the child vcpkg process) from the NDK AGP passes us.
if(NOT DEFINED ENV{ANDROID_NDK_HOME})
    if(DEFINED ANDROID_NDK)
        set(ENV{ANDROID_NDK_HOME} "${ANDROID_NDK}")
    elseif(DEFINED CMAKE_ANDROID_NDK)
        set(ENV{ANDROID_NDK_HOME} "${CMAKE_ANDROID_NDK}")
    endif()
endif()

# Use our overlay triplets, which pin the Android API level to 24 to match the
# app's minSdk (app/build.gradle). vcpkg's builtin android triplets default to
# API 28, so deps would otherwise be built against android-28 and reference
# fortify/stdio symbols missing from the android-24 sysroot, breaking the JNI
# link. Set the cache var (steers this consumer configure) and export the env var
# (inherited by vcpkg's child build subprocesses, which re-resolve the triplet).
if(NOT DEFINED VCPKG_OVERLAY_TRIPLETS)
    set(VCPKG_OVERLAY_TRIPLETS "${CMAKE_CURRENT_LIST_DIR}/vcpkg-triplets" CACHE STRING "")
endif()
if(NOT DEFINED ENV{VCPKG_OVERLAY_TRIPLETS})
    set(ENV{VCPKG_OVERLAY_TRIPLETS} "${CMAKE_CURRENT_LIST_DIR}/vcpkg-triplets")
endif()

# cpp -> main -> src -> app -> android -> <repo root>/.vcpkg/...
include("${CMAKE_CURRENT_LIST_DIR}/../../../../../.vcpkg/scripts/buildsystems/vcpkg.cmake")
