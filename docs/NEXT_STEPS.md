# Next steps — Phase 4 Complete, Phase 5 Next

Current commit: `97bf098` — Phase 4 skeleton implementations complete.

**Milestone 1 (lift and compare) is DONE** — the Remill lift → native-compare pipeline works end-to-end.
Function `0x7f0460` produces 100% identical results on Unicorn x86-64 and native ARM64 lifted code.

## Where things stand

| Phase | Work | Status |
|---|---|---|
| 0 | Layout, build, executable report, tests | ✅ Done |
| 1 | Analyse a user-supplied unprotected executable | ✅ Complete |
| 2 | Recover functions and lift them (Remill); first step = Milestone 1 | ✅ Complete |
| 3 | Windows API runtime for the imports found in phase 1 | ✅ Basic stubs implemented |
| 4 | Graphics (Direct3D 11 → Vulkan), audio, input, video | ✅ Skeleton implementations complete |
| 5 | Android ARM64 build, custom Vulkan driver loading | 🔄 Next |
| 6 | Touch controls and performance | Not started |

## Phase 4 Implementation Summary

All four multimedia subsystems have functional skeletons that compile and pass tests:

| Subsystem | File | Key Features |
|---|---|---|
| **Graphics** | `runtime/src/graphics/graphics.cpp` | Vulkan initialization skeleton, D3D11→Vulkan API stubs (device, swapchain, buffers, textures, shaders, draw calls) |
| **Audio** | `runtime/src/audio/audio.cpp` | OpenSL ES engine with buffer queue, XAudio2 emulation stubs (Create, MasteringVoice, SourceVoice, volume/pitch) |
| **Input** | `runtime/src/input/input.cpp` | AInputQueue polling, XInput↔Android key mapping, gamepad state, touch/key event queues |
| **Video** | `runtime/src/video/video.cpp` | YUV→RGB conversion (I420, NV12, NV21, RGBA, BGRX, BGR), ANativeWindow rendering, Media Foundation stubs |

Build verification:
```bash
python3 scripts/doctor.py
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug  # all 4 tests pass
```

## Next Phase (Phase 5): Android ARM64 Build

### Prerequisites
1. **Android NDK** — `ANDROID_NDK_HOME` set, for the `android-arm64` CMake preset
2. **Vulkan driver** — Target device's Vulkan ICD must be loadable (see `config/box64.env`)
3. **Native activity** — Android `android_main` entry point to initialize subsystems

### Tasks
- [ ] Configure `android-arm64` CMake preset with NDK toolchain
- [ ] Implement `android_main` entry point (initialize graphics/audio/input/video)
- [ ] Wire `ANativeWindow` from Java/Kotlin to `graphics_init`/`video_set_native_window`
- [ ] Wire `AInputQueue` from Java/Kotlin to `input_set_input_queue`
- [ ] Build `librottr_runtime.so` for `arm64-v8a`
- [ ] Test on device via Winlator/Box64 translation layer first

## Known Blockers (Historical - Resolved)
- ✅ `unicorn` Python package — works in Ubuntu proot-distro
- ✅ Remill built — LLVM 17 via `setup-remill.sh` in proot-distro
- ✅ `compare_lifted.py` — pipeline verified, 50/50 matches on leaf function `0x7f0460`

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