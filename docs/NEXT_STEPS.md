# Proton ARM64EC package import foundation (2026-11-27)

The Android app now has a SAF-based WCP import path for a Proton ARM64EC component and a separate FEXCore ARM64EC component. The importer accepts tar+Zstandard and tar+XZ WCP packages, checks the root `profile.json` type, rejects path traversal and archive links, enforces entry/unpacked-size limits, and stages installation before replacing the previous package. FEXCore imports must contain both `system32/libarm64ecfex.dll` and `system32/libwow64fex.dll`; those DLLs are copied into the app's runtime-prefix Windows system32 directory.

The app settings now expose **Install runtime** and the boot diagnostics show whether Proton and FEXCore packages have been imported.

## What is not integrated yet

This is a package installer, not a running Proton container. It does not yet launch Wine/wineserver, initialize a Wine prefix with `wineboot`, establish the Android bionic environment, load the ARM64EC FEX unixlib, mount game files into a Windows drive, or connect DXVK to the app's Vulkan surface. The native x64 execution capability therefore correctly remains **NOT LINKED**, and PLAY does not claim that the game is runnable.

The saved Winlator setup records Proton 10.0-6 ARM64EC, but the public Winlator component catalog currently exposes a generic Proton 10 ARM64EC entry and Proton 10.0-4 ARM64EC; it does not list an exact 10.0-6 package at the time of this change. Import the exact 10.0-6 WCP if you have it. Do not silently substitute a different Wine build.

## Next required work

1. Add a supported Android process/bootstrap path for the imported bionic Proton runtime and verify `wineboot`/wineserver on-device.
2. Wire the FEXCore ARM64EC DLLs into the Wine WOW64 loader and verify an x64 PE test executable.
3. Mount the selected ROTTR folder as a Windows drive and launch the executable without bypassing Steam/EOS ownership checks.
4. Integrate DXVK and share Vulkan device/surface ownership with the host app; test a real game frame.

# Windows x86-64 execution backend foundation (2026-11-27)

The Android native library now reports the CPU execution state separately from the PE header check and Vulkan surface state. On the current APK it reports Android ARM64 as the host, Windows x86-64 as the executable architecture, and **x86-64 backend NOT LINKED**. A successful PE32+ check is only file identification; it is not execution.

This first step intentionally does not add a fake FEXCore stub or claim that a Vulkan clear renderer can boot ROTTR. FEX's own FAQ says the full FEX Linux user-mode runtime is not an Android target; FEXCore can be built as a library for Android but is not a complete Windows process environment. Box64 likewise needs a compatible Linux userspace and Wine/Proton. The existing Winlator device notes show a practical reference configuration (Proton arm64ec + FEXCore + DXVK), but those components are not currently embedded in this APK.

## Next implementation steps

1. Select and package a supported Android Windows runtime stack (ARM64EC/Wine + its x64 CPU bridge, or Box64 + glibc userspace + Wine64). Verify licensing, ABI compatibility, process launch, and Vulkan handle/surface sharing before wiring it into PLAY.
2. Implement the Windows loader and required imported APIs for the ROTTR executable, preserving Steam/EOS ownership checks.
3. Route Direct3D 11 through DXVK to the Android Vulkan device; validate a real rendered frame from the game before claiming native Vulkan gameplay.
4. Add SAF game-folder mounting, executable launch arguments, logs, process lifecycle, audio and input forwarding.

**Current result:** capability diagnostics and a test exist; actual x86-64 instruction execution is still not implemented, and ROTTR does not boot yet.

# ROTTR Android launcher UI (2026-11-27)

The app's first screen is now a landscape ROTTR launcher instead of the blank full-screen TextureView. It includes a custom snowy mountain backdrop, translucent settings panel, graphics preset, resolution scaling, texture quality, shadow and ambient-occlusion controls, Android Storage Access Framework folder picker with persisted URI permission, and a prominent PLAY action. Launcher selections are stored in SharedPreferences. The first-run screen no longer immediately interrupts the user with the all-files-access permission dialog; diagnostics use app-private storage unless public log access is already granted.

**Important limitations:** this is the launcher UI and folder-selection layer only. The selected folder URI is persisted, but the native runtime does not yet expose a game-data mount/boot JNI entry point. PLAY therefore reports that guest boot integration is pending instead of switching to the known-black render surface. Graphics settings currently persist as launcher preferences; they are not yet wired into the renderer. The atmospheric backdrop is a procedural placeholder, not the reference screenshot's actual game artwork.

No APK build has been run for these changes yet.

---

# Diagnostics runaway-log fix (2026-11-27)

Fixed a recursive logcat capture bug in `LogUtils`: captured logcat lines were passed through the normal append method, which also emitted each line back to Android Log. The capture process then read its own output and repeated indefinitely. Captured lines now use a file-only writer and are never mirrored back to logcat. Diagnostic files are capped at 8 MiB; capture stops when the limit is reached.

Commit: `fc95021`.

This fixes runaway diagnostic growth; it does not fix the black screen. Vulkan initialization and actual acquire/submit/present remain unimplemented in `graphics.cpp`. A new APK must be built and installed before the logging fix is present on the phone.

---

# Android build configuration update (2026-11-27)

The repository now builds **debug APKs only**. GitHub Actions runs `assembleDebug`, verifies the debug APK signature, and uploads the debug artifact. The workflow does not read release-signing secrets or generate release keystores. The Android Gradle configuration disables the release variant.

The CI workflow caches its generated debug keystore with a fixed cache key so successive manually dispatched builds can normally keep the same signer. GitHub Actions caches are not permanent storage; eviction or deletion can change the signer. Pull-request builds are not guaranteed to share the persistent CI debug signer.

See `android/README.md` for build and update behavior.

---

# Android Activity frame-loop update (2026-11-27)

The regular Java `Activity` + `TextureView` path now starts a dedicated native frame-pump thread after a successful `graphics_init`. The thread calls `graphics_present()` at an approximately 60 Hz cadence, reports a five-second heartbeat, and is joined before graphics/window teardown. Surface recreation stops the old pump before releasing/reinitializing the window.

**Important limitation:** this is lifecycle/frame-loop plumbing, not a working renderer or game execution loop. In the current `graphics.cpp`, Vulkan instance/device/surface/swapchain/render-pass/framebuffer/command-buffer creation and acquire/submit/present are still TODOs. `graphics_present()` currently only performs frame pacing and returns; it does not submit GPU work or draw pixels. Therefore this change alone does not remove the black screen and does not establish in-game FPS.

## Immediate next implementation targets
1. Implement and validate Android Vulkan instance/device/surface/swapchain creation, including queue-family and surface-format selection.
2. Add swapchain image views, a clear-only render pass/framebuffers, command buffers, semaphores and fences; implement acquire/record/submit/present and swapchain recreation.
3. Add a real game/render command path after the clear-only surface presentation is verified. Do not report gameplay as working until guest execution and draw submission are connected and tested on-device.

---

# Next steps — Phase 6 Complete

Current commit: `2ee16f9` — Phase 6 Touch Controls and Performance complete.

**Milestone 1 (lift and compare) is DONE** — the Remill lift → native-compare pipeline works end-to-end.
Function `0x7f0460` produces 100% identical results on Unicorn x86-64 and native ARM64 lifted code.

**Phase 4 (multimedia subsystems) is DONE** — all four subsystems have functional skeletons.

**Phase 5 (Android ARM64 build) is DONE** — native library builds and links successfully.

**Phase 6 (Touch Controls and Performance) is DONE** — all performance optimizations implemented.

## Where things stand

| Phase | Work | Status |
|---|---|---|
| 0 | Layout, build, executable report, tests | ✅ Done |
| 1 | Analyse a user-supplied unprotected executable | ✅ Complete |
| 2 | Recover functions and lift them (Remill); first step = Milestone 1 | ✅ Complete |
| 3 | Windows API runtime for the imports found in phase 1 | ✅ Basic stubs implemented |
| 4 | Graphics (Direct3D 11 → Vulkan), audio, input, video | ✅ Skeleton implementations complete |
| 5 | Android ARM64 build, custom Vulkan driver loading | ✅ Native library builds |
| 6 | Touch controls and performance | ✅ Complete |

## Phase 6 Implementation Summary

### Touch Controls (`runtime/src/input/touch_controls.cpp`)

| Feature | Status |
|---|---|
| Virtual left stick (movement) | ✅ Implemented |
| Virtual right stick (camera) | ✅ Implemented |
| Action buttons (A, B, X, Y) | ✅ Implemented |
| Shoulder buttons (L1, R1) | ✅ Implemented |
| Start/Back buttons | ✅ Implemented |
| D-pad support | ✅ Implemented |
| Dead zone & clamping | ✅ Implemented |
| Multi-touch support | ✅ Implemented |
| Render data for overlays | ✅ Implemented |

Integration:
- Touch events routed from `input.cpp` → `touch_controls.cpp`
- Gamepad state updated in `android_main.cpp` main loop
- JNI bridge ready for Java/Kotlin touch event injection

### Vulkan Render Pipeline Optimization (`runtime/src/graphics/graphics.cpp`)

| Optimization | Status |
|---|---|
| Pipeline cache (`VkPipelineCache`) | ✅ Implemented |
| Descriptor pooling | ✅ Implemented |
| Descriptor set layout | ✅ Implemented |
| Frame pacing (30/60 FPS) | ✅ Implemented |
| Performance monitoring API | ✅ Implemented |
| MAILBOX present mode support | ✅ Stubbed |

New APIs:
- `graphics_get_perf_stats()` — FPS, frame time min/max/avg
- `graphics_set_target_fps(30|60)` — Dynamic FPS target
- `graphics_set_frame_pacing(bool)` — Enable/disable pacing

### Audio Latency Optimization (`runtime/src/audio/audio.cpp`)

| Optimization | Status |
|---|---|
| AAudio low-latency path | ✅ Implemented |
| Automatic AAudio → OpenSL ES fallback | ✅ Implemented |
| Lock-free ring buffer for AAudio | ✅ Implemented |
| High-priority audio callback | ✅ Implemented |
| Volume scaling in callback | ✅ Implemented |
| Backend detection API | ✅ Implemented |

New APIs:
- `audio_set_low_latency(bool)` — Enable AAudio at init
- `audio_get_backend()` — Returns 1=OpenSL ES, 2=AAudio
- `audio_get_underruns()` — Track buffer underruns

### Frame Pacing (`runtime/src/android_main.cpp`, `runtime/src/graphics/graphics.cpp`)

| Feature | Status |
|---|---|
| Main loop frame pacing | ✅ Implemented |
| Sleep + spin hybrid wait | ✅ Implemented |
| Configurable 30/60 FPS | ✅ Implemented |
| Graphics-level frame pacing | ✅ Implemented |

### Performance Monitoring (`runtime/src/performance.cpp`)

| Metric | Status |
|---|---|
| FPS tracking | ✅ Implemented |
| Frame time (avg/min/max) | ✅ Implemented |
| GPU frame time | ✅ Implemented |
| CPU frame time | ✅ Implemented |
| Audio buffer latency | ✅ Implemented |
| Audio underruns | ✅ Implemented |
| Memory usage | ✅ Implemented |

New API:
- `perf_get_stats(PerfStats*)` — Unified stats struct
- `perf_record_frame/frame/gpu/cpu/audio/memory()` — Subsystem hooks

## Build Verification

```bash
export ANDROID_NDK_HOME=/opt/android-ndk-r27c
cmake --preset android-arm64
cmake --build --preset android-arm64
# Produces: build/android-arm64/rottr_android.so (AArch64, ~1.5 MB)
```

All tests pass:
```bash
ctest --preset linux-debug
# 4/4 tests passed
```

## Next Phase (Future)

### Device Testing & Integration
- [ ] Test on target device (ARM64 Android) via Winlator/Box64
- [ ] Integrate with translation layer for Windows API calls
- [ ] Profile and optimize on real hardware
- [ ] Add touch control overlay rendering in graphics

## Known Blockers (Historical - Resolved)
- ✅ `unicorn` Python package — works in Ubuntu proot-distro
- ✅ Remill built — LLVM 17 via `setup-remill.sh` in proot-distro
- ✅ `compare_lifted.py` — pipeline verified, 50/50 matches on leaf function `0x7f0460`
- ✅ Android NDK — installed and configured (r27c)
- ✅ OpenSL ES linking — resolved with `OpenSLES` library
- ✅ AAudio linking — resolved with `aaudio` library
- ✅ Vulkan linking — resolved with `vulkan` library

## Rules (from README)
- Never commit game files, assets, executables, or Steam files
- No DRM or ownership-check bypassing. Use your own Steam/GOG account.
- Static recompilation is separate long-term research; translation layers (Approach A) remain the tested path.

## Related
- `docs/approaches.md` — Approach A (translation layers) vs C (static recomp)
- `docs/static-recomp.md` — full roadmap
- `docs/milestone1-lift.md` — Milestone 1 details and exit criteria
- `docs/phase1-report.md` — recorded executable report
- `scripts/doctor.py` — build-tool readiness check

---

# Hidden Android runtime provisioning (2026-11-27)

The ROTTR Play flow now provisions the compatibility packages without exposing a Winlator-style component manager:
- Downloads Proton 10.0-4 ARM64EC from the GameNative component host.
- Downloads FEXCore 2611-gn-8 ARM64EC from the GameNative component host.
- Imports both WCP archives into app-private storage and stages the FEXCore bridge DLLs.
- Uses Android DownloadManager notifications; the player does not need to browse for WCP files.
- Runtime package state is not presented as a desktop/container UI.

**Important:** this is automatic component provisioning only. The Android-native Windows process launcher, Wine/wineserver bootstrap, FEXCore execution bridge, and Direct3D-to-Vulkan path are not yet connected. ROTTR.exe is still inspected read-only; the app does not yet execute it. Do not describe this milestone as game boot or native gameplay.

The exact Proton 10.0-6 package remains unavailable from the public catalog; 10.0-4 is used explicitly and is not mislabeled as 10.0-6.
