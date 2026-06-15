# vcpkg.cmake's triplet auto-detection falls back to the *host* processor for
# Android cross-builds (it doesn't map ANDROID_ABI), which would pick the wrong
# triplet. Select it from Android Gradle's per-ABI ANDROID_ABI, then chainload the
# NDK toolchain through vcpkg.

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

# vcpkg.cmake includes this before triplet/arch detection.
if(NOT DEFINED VCPKG_CHAINLOAD_TOOLCHAIN_FILE)
    if(DEFINED ANDROID_NDK)
        set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${ANDROID_NDK}/build/cmake/android.toolchain.cmake")
    elseif(DEFINED CMAKE_ANDROID_NDK)
        set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_ANDROID_NDK}/build/cmake/android.toolchain.cmake")
    endif()
endif()

# vcpkg's child dependency builds locate the NDK through ANDROID_NDK_HOME.
if(NOT DEFINED ENV{ANDROID_NDK_HOME})
    if(DEFINED ANDROID_NDK)
        set(ENV{ANDROID_NDK_HOME} "${ANDROID_NDK}")
    elseif(DEFINED CMAKE_ANDROID_NDK)
        set(ENV{ANDROID_NDK_HOME} "${CMAKE_ANDROID_NDK}")
    endif()
endif()

# Overlay triplets pin the Android API level to minSdk 24; builtin triplets target
# API 28 and can reference symbols missing from the android-24 sysroot.
if(NOT DEFINED VCPKG_OVERLAY_TRIPLETS)
    set(VCPKG_OVERLAY_TRIPLETS "${CMAKE_CURRENT_LIST_DIR}/vcpkg-triplets" CACHE STRING "")
endif()
if(NOT DEFINED ENV{VCPKG_OVERLAY_TRIPLETS})
    set(ENV{VCPKG_OVERLAY_TRIPLETS} "${CMAKE_CURRENT_LIST_DIR}/vcpkg-triplets")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../../../../.vcpkg/scripts/buildsystems/vcpkg.cmake")
