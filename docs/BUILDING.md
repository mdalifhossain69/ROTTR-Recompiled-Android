# Build and test guide

This document describes commands and repository build configuration. It does not claim those commands were run successfully in the current session.

## Android application

From `android/`:

```bash
./gradlew assembleDebug
```

The APK is normally written under `android/build/outputs/apk/debug/`. Check the actual build output or a manually dispatched workflow run before reporting an APK as built.

The repository's GitHub Actions workflow must remain manual-only (`workflow_dispatch`). Do not add push or pull-request triggers.

A successful APK build verifies compilation and packaging only. It does not verify that ROTTR executes or renders.

## Native CMake and host tests

Inspect `CMakePresets.json` and the root `CMakeLists.txt` for the exact presets and dependencies in the checked-out revision.

Typical host workflow:

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug --output-on-failure
```

Android native build:

```bash
cmake --preset android-arm64
cmake --build --preset android-arm64
```

Set `ANDROID_NDK_HOME` to a compatible installed NDK when required by the preset/toolchain. Optional libraries detected on one developer machine are not automatically available in the Android build.

## Current CPU backend status

The repository contains a limited, bounds-checked x86-64 interpreter and an associated test source/CTest target. The supported subset is not a complete x86-64 implementation. The test source being present does not mean it has passed a build or test run.

The selected primary direction is project-owned x86-64-to-AArch64 DBT/JIT. As of this documentation snapshot, there is no AArch64 code emitter, executable-memory manager, translated block cache or on-device DBT execution path. The interpreter remains the only implemented x86-64 instruction execution code in the repository.

## Graphics compiler dependency

DXBC inspection is available without vkd3d-shader. DXBC-to-SPIR-V compilation is conditional on a compatible vkd3d-shader header and library being found by CMake. The repository does not currently vendor or build that dependency for Android ARM64. Without it, the compiler adapter reports `CompilerUnavailable`.

## What tests mean

- Host unit tests validate only the code paths they cover, when actually run.
- Android builds validate compilation and packaging, not Windows x86-64 execution.
- Neither proves complete D3D11 compatibility, actual ROTTR draw submission or playable gameplay.
- Device validation must record device, Android version, GPU driver, game build, scene, logs and reproducible steps.

## Repository hygiene

Keep proprietary game executables and assets out of Git. Use only a legitimate user-owned installation and preserve Steam/EOS/DRM checks.
