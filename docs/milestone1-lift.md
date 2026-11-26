# Milestone 1: lift and compare a few functions

Goal: prove the translation path works on real code from the game before scaling it up.

## Inputs
- `generated/functions.csv`, from `scripts/functions.py`, on the owner's machine.
- The owner's own `ROTTR.exe`, kept out of Git (`bin_input/`).

## Steps
1. Build Remill with `scripts/setup-remill.sh` (not run yet).
2. Pick 10 small primary functions from `functions.csv`, starting with the smallest.
3. Lift each one to LLVM IR with Remill.
4. Compile the IR for `arm64-v8a` with the NDK, using the `android-arm64` preset.
5. Run each function on the same inputs on x86-64 and on ARM64, and compare the results.

## Exit criteria
- All 10 functions produce identical results on both architectures.
- Each step is recorded with commit hash, device, and NDK version.

## Not in this milestone
- Imports (Steam, Bink, Windows API), graphics, audio and input.
- Any change to Steam ownership checks.

## Graphics choice
The owner reports that the game supports both DirectX 11 and 12 on PC. DirectX 11 is the
target for the Android port, because the DXVK-style translation layers already used with
Winlator handle it and it is the simpler path. DirectX 12 would need VKD3D-Proton on top.
This choice has not yet been checked against the game binary.

## Run
```
python3 scripts/lift_functions.py bin_input/ROTTR.exe --count 10
```
IR files land in `generated/lift/` on the owner's machine (not committed).
Smallest functions (2 to 4 bytes) are padding or stubs, so the script samples 32 to 256 bytes.
