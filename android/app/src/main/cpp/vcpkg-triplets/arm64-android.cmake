# Overlay triplet: identical to vcpkg's builtin arm64-android, but pins the
# Android API level to 24 (VCPKG_CMAKE_SYSTEM_VERSION) so dependencies are built
# against the same platform the app links against (app/build.gradle: minSdk 24).
# vcpkg's builtin android triplets default to API 28, which pulls in fortify /
# stdio symbols (e.g. __sendto_chk, fwrite_unlocked) absent from the android-24
# sysroot, breaking the JNI link. Keep this in lockstep with minSdk.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Android)
set(VCPKG_CMAKE_SYSTEM_VERSION 24)
set(VCPKG_MAKE_BUILD_TRIPLET "--host=aarch64-linux-android")
set(VCPKG_CMAKE_CONFIGURE_OPTIONS -DANDROID_ABI=arm64-v8a)