# android-arm64-box64.toolchain.cmake
#
# Custom toolchain for building ROTTR-Recomp for Android arm64-v8a directly
# inside an aarch64 Termux installation.
#
# The stock Android NDK (r27c) only ships a linux-x86_64 prebuilt clang, which
# cannot execute natively on an aarch64 Android/Termux device. This toolchain
# therefore wraps the NDK clang/clang++ with box64 (x86_64-on-aarch64 emulation)
# so the cross-compiler runs on-device.
#
# Usage (inside Termux):
#   export ANDROID_NDK_HOME=/data/data/com.termux/files/usr/opt/android-ndk-r27c
#   cmake -S . -B build/android-arm64 -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=$PWD/scripts/android-arm64-box64.toolchain.cmake \
#     -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-31 \
#     -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/android-arm64

set(CMAKE_SYSTEM_NAME Linux)   # do NOT use "Android" (triggers CMake's built-in Android toolchain override)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_SYSTEM_VERSION 31)
set(ANDROID TRUE)              # manual, so CMakeLists' if(ANDROID) builds the Android runtime
set(ANDROID_ABI arm64-v8a)
set(ANDROID_PLATFORM android-31)
set(ANDROID_NATIVE_API_LEVEL 31)

if(NOT DEFINED ANDROID_NDK_HOME)
    if(DEFINED ENV{ANDROID_NDK_HOME})
        set(ANDROID_NDK_HOME "$ENV{ANDROID_NDK_HOME}")
    else()
        message(FATAL_ERROR "ANDROID_NDK_HOME must be set (e.g. /data/data/com.termux/files/usr/opt/android-ndk-r27c)")
    endif()
endif()
set(NDK "${ANDROID_NDK_HOME}")
set(TOOLCHAIN_BIN "${NDK}/toolchains/llvm/prebuilt/linux-x86_64/bin")
set(NDK_SYSROOT "${TOOLCHAIN_BIN}/../sysroot")

# Wrapper scripts that run the NDK's x86_64 clang under box64 on this aarch64 host.
set(WRAPPER_DIR "${CMAKE_CURRENT_LIST_DIR}/ndk-box64")
set(CMAKE_C_COMPILER   "${WRAPPER_DIR}/clang")
set(CMAKE_CXX_COMPILER "${WRAPPER_DIR}/clang++")

# Target triple so the NDK clang produces AArch64 Android objects.
set(CMAKE_C_COMPILER_TARGET   "aarch64-linux-android31")
set(CMAKE_CXX_COMPILER_TARGET "aarch64-linux-android31")

set(CMAKE_SYSROOT "${NDK_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH "${NDK_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Find the NDK toolchain libs (libc++, liblog, libvulkan, libaaudio, OpenSLES, etc.)
set(CMAKE_ANDROID_NDK "${NDK}")
set(CMAKE_ANDROID_NDK_TOOLCHAIN_VERSION clang)
set(CMAKE_ANDROID_STL_TYPE c++_shared)

# Android NDK library search paths for arm64-v8a, android-31.
list(APPEND CMAKE_FIND_ROOT_PATH
    "${NDK}/platforms/android-31/arch-arm64/usr"
    "${NDK}/sources/cxx-stl/llvm-libc++/libs/arm64-v8a"
    "${NDK}/sources/cxx-stl/llvm-libc++abi/libs/arm64-v8a"
    "${NDK}/sysroot/usr/lib/aarch64-linux-android/31"
)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)

message(STATUS "Box64 Android toolchain: NDK=${NDK}")
message(STATUS "C compiler: ${CMAKE_C_COMPILER}")
message(STATUS "CXX compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "Sysroot: ${CMAKE_SYSROOT}")
message(STATUS "Target: aarch64-linux-android31 (arm64-v8a)")
