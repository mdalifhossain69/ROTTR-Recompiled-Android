# Termux Setup & Testing Guide

This guide covers building the ROTTR-Recomp native library (`rottr_android.so`) in Termux and testing on an Android device.

## Prerequisites

- Android device with ARM64 (arm64-v8a) — Snapdragon 870+ recommended
- Termux from **F-Droid** (not Play Store)
- Storage permission granted: `termux-setup-storage`
- Legally owned copy of `ROTTR.exe` (for lift scripts — not needed for Android build)

## 1. Termux Setup

```bash
# Install build dependencies
pkg update && pkg upgrade -y
pkg install git cmake ninja clang python3 openjdk-17 gradle -y

# Install Android NDK r27c
cd ~
wget -q https://dl.google.com/android/repository/android-ndk-r27c-linux.zip -O ndk.zip
unzip -q ndk.zip -d /data/data/com.termux/files/usr/opt
export ANDROID_NDK_HOME=/data/data/com.termux/files/usr/opt/android-ndk-r27c
echo 'export ANDROID_NDK_HOME=/data/data/com.termux/files/usr/opt/android-ndk-r27c' >> ~/.bashrc
```

## 2. Clone & Build

```bash
git clone https://github.com/mdalifhossain69/ROTTR-Recomp.git
cd ROTTR-Recomp

# Build Android ARM64 native library
export ANDROID_NDK_HOME=/data/data/com.termux/files/usr/opt/android-ndk-r27c
cmake --preset android-arm64
cmake --build --preset android-arm64

# Verify output
ls -la build/android-arm64/rottr_android.so
# Should be ~1.5 MB, AArch64 ELF shared object
```

## 3. Run Unit Tests (Linux Host)

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
# Expected: 4/4 tests pass (guest, recon, functions, harness)
```

## 4. Deploy to Device for Testing

### Option A: Via ADB (recommended)

```bash
# On host machine with ADB
adb push build/android-arm64/rottr_android.so /data/local/tmp/

# Test library loads
adb shell LD_LIBRARY_PATH=/data/local/tmp /system/bin/linker64 /data/local/tmp/rottr_android.so
# Should print: "android_main starting" then wait for callbacks
```

### Option B: Build Android App Module

```bash
# In Termux (requires Android SDK + Gradle)
cd android
export ANDROID_SDK_ROOT=/path/to/android-sdk
./gradlew assembleDebug
# Output: app/build/outputs/apk/debug/app-debug.apk

# Install on device
adb install app/build/outputs/apk/debug/app-debug.apk
```

## 5. Test on Device

### Using Winlator (translation layer — Approach A reference)

1. Install **Winlator Ludashi 5.0** from GitHub releases
2. Create container: **Proton 10.0-6 arm64ec**, Turnip driver
3. Settings reference: `docs/winlator-ludashi-5.0.md`
4. Launch `ROTTR.exe` — verify 60+ FPS baseline

### Using Native Library (Approach C — this project)

The native library exposes JNI entry points. To test:

1. **Create a minimal test Activity** (Java/Kotlin):
   ```java
   // Load library
   System.loadLibrary("rottr_android");
   
   // Initialize with screen size
   NativeBridge.init(width, height);
   
   // Set surface from SurfaceView/TextureView
   NativeBridge.setSurface(surface);
   
   // Set input queue
   NativeBridge.setInputQueue(inputQueue);
   ```

2. **Verify touch controls work**:
   - Virtual left stick (movement) → bottom-left
   - Virtual right stick (camera) → bottom-right
   - A/B/X/Y buttons → right side
   - L1/R1 → top-right
   - Start/Back → top-center

3. **Monitor performance** (logcat):
   ```bash
   adb logcat -s ROTTR-Recomp ROTTR-TouchControls
   ```
   Look for:
   - `android_main starting`
   - `Touch control layout initialized: WxH`
   - `All subsystems initialized successfully`
   - `graphics_get_perf_stats` output (FPS, frame time)

### Expected Log Output

```
I/ROTTR-Recomp: android_main starting
I/ROTTR-Recomp: onNativeWindowCreated: 0x...
I/ROTTR-Recomp: Initializing subsystems: 1280x720
I/ROTTR-TouchControls: Touch control layout initialized: 1280x720
I/ROTTR-Recomp: All subsystems initialized successfully
```

## 6. Profiling & Debugging

### GPU/CPU Profiling (requires root or debuggable build)

```bash
# Simple FPS overlay via graphics API
# In test Activity, call periodically:
double fps, avg, min, max;
graphics_get_perf_stats(&fps, &avg, &min, &max);
Log.i("PERF", String.format("FPS: %.1f, Avg: %.2fms, Min: %.2f, Max: %.2f", fps, avg, min, max));
```

### Audio Backend Check

```bash
adb logcat -s ROTTR-Audio
# Look for: "AAudio" (low latency) or "OpenSL ES" (fallback)
```

### Frame Pacing Verification

Target frame times:
- **60 FPS**: 16.67ms per frame
- **30 FPS**: 33.33ms per frame

```bash
# In test Activity
graphics_set_target_fps(60);  // or 30
graphics_set_frame_pacing(true);
```

## 7. Troubleshooting

| Issue | Fix |
|-------|-----|
| `cmake` not found | `pkg install cmake` |
| `NDK not found` | Verify `ANDROID_NDK_HOME` points to extracted NDK |
| `ld.lld: error: undefined symbol` | Ensure `target_link_libraries` includes `vulkan`, `aaudio`, `OpenSLES` |
| Library loads but crashes | Check logcat for `LOGE` messages; verify JNI signatures match |
| Touch controls not responding | Verify `input_set_input_queue` called with valid `AInputQueue` |
| Low FPS | Try `graphics_set_target_fps(30)`; check `perf_get_stats` for GPU/CPU time |

## 8. Next Steps After Testing

1. **Report results** — device, SoC, Android version, FPS, issues
2. **Profile bottlenecks** — use `perf_get_stats` to identify GPU vs CPU limits
3. **Implement missing D3D11/XAudio2/XInput stubs** — connect lifted functions to real game imports
4. **Add touch overlay rendering** — draw `TouchControlRenderData` circles in Vulkan

## Useful Commands Reference

```bash
# Build
export ANDROID_NDK_HOME=~/android-ndk-r27c
cmake --preset android-arm64 && cmake --build --preset android-arm64

# Test
ctest --preset linux-debug

# Deploy
adb push build/android-arm64/rottr_android.so /data/local/tmp/

# Logcat
adb logcat -s ROTTR-Recomp ROTTR-TouchControls ROTTR-Audio

# Clean rebuild
rm -rf build/android-arm64 && cmake --preset android-arm64 && cmake --build --preset android-arm64
```