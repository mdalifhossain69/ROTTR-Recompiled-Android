# Next steps — Phase 5 Complete, Phase 6 Next

Current commit: `42482c8` — Phase 5 Android ARM64 build complete.

**Milestone 1 (lift and compare) is DONE** — the Remill lift → native-compare pipeline works end-to-end.
Function `0x7f0460` produces 100% identical results on Unicorn x86-64 and native ARM64 lifted code.

**Phase 4 (multimedia subsystems) is DONE** — all four subsystems have functional skeletons.

**Phase 5 (Android ARM64 build) is DONE** — native library builds and links successfully.

## Where things stand

| Phase | Work | Status |
|---|---|---|
| 0 | Layout, build, executable report, tests | ✅ Done |
| 1 | Analyse a user-supplied unprotected executable | ✅ Complete |
| 2 | Recover functions and lift them (Remill); first step = Milestone 1 | ✅ Complete |
| 3 | Windows API runtime for the imports found in phase 1 | ✅ Basic stubs implemented |
| 4 | Graphics (Direct3D 11 → Vulkan), audio, input, video | ✅ Skeleton implementations complete |
| 5 | Android ARM64 build, custom Vulkan driver loading | ✅ Native library builds |
| 6 | Touch controls and performance | 🔄 Next |

## Phase 5 Implementation Summary

Android ARM64 native library builds successfully:

| Component | File | Status |
|---|---|---|
| **NDK toolchain** | `CMakePresets.json` android-arm64 preset | ✅ Configured |
| **android_main entry point** | `runtime/src/android_main.cpp` | ✅ Implemented |
| **ANativeWindow wiring** | `graphics_init`/`video_set_native_window` | ✅ Connected |
| **AInputQueue wiring** | `input_set_input_queue` | ✅ Connected |
| **Shared library** | `rottr_android.so` (arm64-v8a) | ✅ Built & linked |
| **JNI bridge (Java)** | `android/src/main/java/com/rottr/recomp/NativeBridge.java` | ✅ Created |
| **Example Activity** | `android/src/main/java/com/rottr/recomp/MainActivity.java` | ✅ Created |

Build verification:
```bash
export ANDROID_NDK_HOME=/opt/android-ndk-r27c
cmake --preset android-arm64
cmake --build --preset android-arm64
# Produces: build/android-arm64/rottr_android.so (AArch64, ~1.4 MB)
```

## Phase 4 Subsystems (from Phase 4)

| Subsystem | File | Key Features |
|---|---|---|
| **Graphics** | `runtime/src/graphics/graphics.cpp` | Vulkan initialization skeleton, D3D11→Vulkan API stubs |
| **Audio** | `runtime/src/audio/audio.cpp` | OpenSL ES engine with buffer queue, XAudio2 emulation stubs |
| **Input** | `runtime/src/input/input.cpp` | AInputQueue polling, XInput↔Android key mapping, gamepad state |
| **Video** | `runtime/src/video/video.cpp` | YUV→RGB conversion (I420, NV12, NV21, RGBA, BGRX, BGR), ANativeWindow rendering |

## Next Phase (Phase 6): Touch Controls and Performance

### Tasks
- [ ] Implement touch-to-gamepad mapping for on-screen controls
- [ ] Add virtual joystick and button overlays
- [ ] Profile and optimize Vulkan render pipeline
- [ ] Optimize audio buffer queue latency
- [ ] Implement frame pacing for consistent 30/60 FPS
- [ ] Add performance monitoring (FPS, frame time, GPU/CPU usage)
- [ ] Test on target device (ARM64 Android) via Winlator/Box64
- [ ] Integrate with translation layer for Windows API calls

## Known Blockers (Historical - Resolved)
- ✅ `unicorn` Python package — works in Ubuntu proot-distro
- ✅ Remill built — LLVM 17 via `setup-remill.sh` in proot-distro
- ✅ `compare_lifted.py` — pipeline verified, 50/50 matches on leaf function `0x7f0460`
- ✅ Android NDK — installed and configured (r27c)
- ✅ OpenSL ES linking — resolved with `OpenSLES` library

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