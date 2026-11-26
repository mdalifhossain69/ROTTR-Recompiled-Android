# Next steps — resuming from where the work was left

Current commit: `d18772b` — merge of the mdalifhossain69 static-recompilation
scaffold ("Merge remote harness changes; keep local -lremill fix") with the
previous translation-layer main.

This project is a **scaffold only**; it does not run the game. The immediate
goal is **Milestone 1: lift and compare a few functions** (see
`docs/milestone1-lift.md`), proving the Remill lift → native-compare path works
on real game code before scaling up.

## Where things stand

| Phase | Work | Status |
|---|---|---|
| 0 | Layout, build, executable report, tests | Done (no game needed) |
| 1 | Analyse a user-supplied unprotected executable | Done — report in `docs/phase1-report.md` (owner's machine, 2026-11-26) |
| 2 | Recover functions and lift them (Remill); first step = Milestone 1 | Started, **not run** |
| 3 | Windows API runtime for the imports found in phase 1 | Not started |
| 4 | Graphics (Direct3D 11 → Vulkan), audio, input, video | Not started |
| 5 | Android ARM64 build, custom Vulkan driver loading | Not started |
| 6 | Touch controls and performance | Not started |

## Prerequisites for Milestone 1

These must be in place before the lift/compare pipeline can run:

1. **Remill built** — `scripts/setup-remill.sh` (tested on Ubuntu 22.04, run inside
   `proot-distro`, needs LLVM/Clang 15+). Set `LLVM_VERSION` / `USE_SYSTEM_LLVM` as needed.
   Produces `tools/remill-src/` and a `remill-lift-17` binary plus headers/lib.
2. **Python `unicorn` package** — `pip install --break-system-packages unicorn`
   (needed by `compare_lifted.py` and `test_harness.py`). In this Termux env it
   currently fails: `ImportError: Failed to load the Unicorn dynamic library`.
3. **Android NDK** — `ANDROID_NDK_HOME` set, for the `android-arm64` CMake preset
   (phase 5; not needed for Milestone 1 on-host compare).
4. **Your own `ROTTR.exe`** — place it at `bin_input/ROTTR.exe` (ignored by Git;
   never commit executables/assets). A DRM-free GOG edition is the preferred input.

## Verify the scaffold builds and self-tests

```bash
python3 scripts/doctor.py           # cmake, ninja, clang must be found
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug          # guest, recon, functions pass; harness skips
                                    # without Remill headers / working unicorn
```

## Run Milestone 1 (on the owner's machine with ROTTR.exe)

```bash
# 0. Build Remill first (scripts/setup-remill.sh) -- one time, not run yet

# 1. Recover function ranges from your executable
python3 scripts/functions.py bin_input/ROTTR.exe --out generated/functions.csv

# 2. Lift ~10 small primary functions to LLVM IR
python3 scripts/lift_functions.py bin_input/ROTTR.exe --count 10
# IR -> generated/lift/*.ll (not committed)

# 3. Build the harness (lifted IR + shim -> libharness.so)
python3 scripts/build_harness.py

# 4. Compare lifted vs original (Unicorn reference)
pip install --break-system-packages unicorn
python3 scripts/compare_lifted.py bin_input/ROTTR.exe --inputs 200
```

Exit criteria (from `docs/milestone1-lift.md`): all 10 functions produce
identical results on x86-64 (Unicorn) and ARM64 (native lifted), each step
recorded with commit hash, device, and NDK version.

## Known blockers in this environment

- `unicorn` Python package cannot load its dynamic library here →
  `test_harness.py` and `compare_lifted.py` cannot run.
- No Android NDK (`ANDROID_NDK_HOME` unset) → `android-arm64` preset untested.
- Remill not built → `setup-remill.sh` not executed (targets Ubuntu 22.04 in
  `proot-distro`).
- No `ROTTR.exe` present → `recon.py` / `functions.py` / `lift_functions.py` /
  `compare_lifted.py` have no real input to operate on.

## Rules (from README)

- Never commit game files, assets, executables, or Steam files (`bin_input/`,
  `generated/`, `build/`, `*.exe`, `*.dll` are gitignored).
- No DRM or ownership-check bypassing. Use your own Steam/GOG account.
- Static recompilation is separate long-term research; translation layers
  (Approach A, scripts/ + config/) remain the tested path.

## Related

- `docs/approaches.md` — Approach A (translation layers, tested first) vs C (static recomp)
- `docs/static-recomp.md` — full roadmap
- `docs/milestone1-lift.md` — Milestone 1 details and exit criteria
- `docs/phase1-report.md` — recorded executable report
- `scripts/doctor.py` — build-tool readiness check
