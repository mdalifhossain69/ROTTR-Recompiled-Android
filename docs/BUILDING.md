# Build and test guide

## Android application

The Android application lives in android/. From that directory run:

    ./gradlew assembleDebug

The APK is normally written under android/build/outputs/apk/debug/. The GitHub Actions workflow is the authoritative CI build recipe. Check the latest workflow run before describing an APK as successfully built.

A successful APK build verifies compilation and packaging only. It does not verify that ROTTR boots or renders.

## Native CMake and host tests

Inspect CMakePresets.json and the root CMakeLists.txt for the exact presets and dependencies in the checked-out revision.

Typical host workflow:

    cmake --preset linux-debug
    cmake --build --preset linux-debug
    ctest --preset linux-debug --output-on-failure

Android native build:

    cmake --preset android-arm64
    cmake --build --preset android-arm64

Set ANDROID_NDK_HOME to an installed compatible NDK if the preset/toolchain requires it. Do not assume an optional dependency is available just because CMake can detect it on a developer machine.

## Graphics compiler dependency

DXBC inspection is available without vkd3d-shader. Actual DXBC-to-SPIR-V compilation is conditional on a compatible vkd3d-shader header and library being found by CMake. The repository does not currently vendor or build that dependency for Android ARM64. Without it, the compiler adapter reports CompilerUnavailable.

## What tests mean

- Host unit tests validate the algorithms and backend contracts they cover.
- Android CI validates the configured Android build and packaging.
- Neither proves Windows x86-64 game execution, complete D3D11 compatibility, actual ROTTR draw submission or playable gameplay.
- Device validation must record device, Android version, GPU driver, game build, scene, logs and reproducible steps.

## Repository hygiene

Keep proprietary game executables and assets out of Git. Use only a legitimate user-owned installation and preserve Steam/EOS/DRM checks.
