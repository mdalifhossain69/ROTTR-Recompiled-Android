# ROTTR-Recomp

Static recompilation of *Rise of the Tomb Raider* (x86-64 Windows) to ARM64 Android.

**Status: Phases 1-6 complete.** The lift → native-compare pipeline works end-to-end (function 0x7f0460 verified 100% match). Android ARM64 native library (`rottr_android.so`) builds with touch controls, Vulkan optimization, AAudio low-latency path, frame pacing, and performance monitoring.

## Quick Start

```bash
# Build for Linux (development/testing)
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug

# Build for Android ARM64
export ANDROID_NDK_HOME=/opt/android-ndk-r27c
cmake --preset android-arm64
cmake --build --preset android-arm64
# Output: build/android-arm64/rottr_android.so
```

## Rules

- No game files, assets, executables or Steam files in this repository.
- No DRM or ownership-check bypassing. Use your own Steam/GOG account.
- Static recompilation (Approach C) is the active path. Translation layers (Approach A) remain in `docs/approaches.md` for reference.

## Phases

| Phase | Work | Status |
|-------|------|--------|
| 0 | Layout, build, executable report, tests | ✅ Done |
| 1 | Analyse user-supplied unprotected executable | ✅ Complete |
| 2 | Recover functions and lift via Remill | ✅ Complete (30 functions, 0 failed) |
| 3 | Windows API runtime for imports | ✅ Basic stubs |
| 4 | Graphics, Audio, Input, Video skeletons | ✅ Complete |
| 5 | Android ARM64 build, NDK integration | ✅ `rottr_android.so` builds |
| 6 | Touch controls, performance optimization | ✅ Complete |

### Phase 6 Highlights

- **Touch Controls**: Virtual left/right sticks, 8 buttons (A/B/X/Y/L1/R1/Start/Back), D-pad, dead zones, multi-touch
- **Vulkan**: Pipeline cache, descriptor pooling, MAILBOX present mode support, configurable 30/60 FPS
- **Audio**: AAudio low-latency path with automatic OpenSL ES fallback, lock-free ring buffer
- **Frame Pacing**: Hybrid sleep+spin wait in main loop and graphics present
- **Performance**: Unified monitoring (FPS, frame time min/avg/max, GPU/CPU time, audio buffer/underruns, memory)

## Project Structure

```
runtime/src/
  graphics/graphics.cpp      # D3D11 → Vulkan translation
  graphics/graphics_types.h  # Vulkan/D3D11 type definitions
  audio/audio.cpp            # XAudio2 → OpenSL ES/AAudio
  audio/audio_types.h        # Audio type definitions
  input/input.cpp            # XInput → Android AInput mapping
  input/touch_controls.cpp   # Touch-to-gamepad mapping
  input/input_types.h        # Input type definitions
  video/video.cpp            # YUV→RGB conversion, ANativeWindow rendering
  video/video_types.h        # Video type definitions
  performance.cpp            # Unified performance monitoring
  android_main.cpp           # Native activity entry point
  win32_stubs.cpp            # Windows API stubs
  remill_intrinsics.cpp      # Native aarch64 Remill intrinsics
  guest.c                    # Guest CPU state
include/
  performance.h              # Performance monitoring API
scripts/
  recon.py                   # PE32+ analysis
  functions.py               # Function range extraction
  lift_functions.py          # Remill lifting
  build_harness.py           # Lifted IR → native .so
  compare_lifted.py          # Unicorn x86-64 vs native comparison
  doctor.py                  # Build-tool readiness check
  setup-remill.sh            # Build Remill (LLVM 17)
android/
  src/main/java/com/rottr/recomp/
    NativeBridge.java        # JNI bridge
    MainActivity.java        # Example Activity
  build.gradle               # Android module
CMakeLists.txt               # Build configuration (linux-debug, android-arm64)
```

## Key Verification

Function **0x7f0460** produces **50/50 identical results** between:
- Unicorn x86-64 emulator (reference)
- Native ARM64 lifted code (via Remill)

Pipeline verified with 10, 50, and 30 input test regimes — zero differences.

## Documentation

| File | Purpose |
|------|---------|
| `docs/approaches.md` | Approach A (translation) vs C (static recomp) |
| `docs/static-recomp.md` | Full roadmap with phase status |
| `docs/milestone1-lift.md` | Milestone 1 technical details |
| `docs/phase1-report.md` | Executable analysis report |
| `docs/test-results.md` | Device test results |
| `docs/winlator-ludashi-5.0.md` | Reference Winlator settings |
| `docs/NEXT_STEPS.md` | Current phase status and next actions |

## Android Integration

The native library exposes JNI methods via `NativeBridge.java`:

```java
NativeBridge.init(width, height);           // Initialize subsystems
NativeBridge.setSurface(surface);           // Set ANativeWindow for rendering
NativeBridge.setInputQueue(inputQueue);     // Set AInputQueue for touch/key events
NativeBridge.releaseSurface();              // Release surface
NativeBridge.shutdown();                    // Cleanup
```

See `android/src/main/java/com/rottr/recomp/MainActivity.java` for a complete Activity implementation.

## Requirements

- Android NDK r27c+
- CMake 3.22+
- Python 3.10+ (for lift scripts)
- Remill (built via `scripts/setup-remill.sh` — requires Ubuntu/proot-distro)
- Own copy of `ROTTR.exe` (placed in `bin_input/`, gitignored)

## License

Research project. Not affiliated with Crystal Dynamics, Square Enix, or Valve.