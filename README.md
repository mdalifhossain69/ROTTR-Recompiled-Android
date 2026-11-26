# ROTTR-Recomp

Goal: run a legally owned Steam copy of Rise of the Tomb Raider on an ARM64 Android device.

**Status: playable through translation layers on one tested device (see `docs/test-results.md`). The static recompilation is a scaffold only and does not run the game.**

Working setup: [Winlator Ludashi 5.0](docs/winlator-ludashi-5.0.md)

## Static recompilation
See [the roadmap](docs/static-recomp.md).

```
python3 scripts/doctor.py
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

## Rules
- No game files, assets, executables or Steam files in this repository.
- No DRM or ownership-check bypassing. Use your own Steam account.

## Plan
1. Run `scripts/check-device.sh` in Termux and record the result.
2. Reproduce the translation-layer result and record the full settings.
3. Record device, driver versions, settings and frame rate for every test.
4. Treat static recompilation as separate long-term research.

## Continuing the work (static recompilation)

The static-recompilation scaffold (Remill lift-and-compare harness) was merged
in at commit `d18772b` ("where it was left"). Both approaches now live in this
repo:

- **Approach A** — translation layers: `scripts/setup-termux.sh`,
  `scripts/run-rottr.sh`, `scripts/setup-prefix.sh`, `config/*`,
  `docs/SETUP.md`, `docs/PERFORMANCE.md`, `docs/COMPATIBILITY.md`,
  `docs/TROUBLESHOOTING.md` (tested first; see `docs/approaches.md`).
- **Approach C** — static recompilation: `scripts/recon.py`,
  `scripts/functions.py`, `scripts/lift_functions.py`,
  `scripts/build_harness.py`, `scripts/compare_lifted.py`, `harness/`,
  `runtime/`, CMake (`linux-debug`, `android-arm64`), `tests/`,
  `docs/static-recomp.md`, `docs/milestone1-lift.md` (long-term research).

See [`docs/NEXT_STEPS.md`](docs/NEXT_STEPS.md) for the exact commands to resume
Milestone 1 (lift and compare a few functions), the prerequisites (Remill,
unicorn, NDK, your own `ROTTR.exe`), and the known blockers.

## Project structure (static-recompilation files)

```
scripts/recon.py        # read-only PE32+ report
scripts/functions.py    # .pdata exception table -> function ranges
scripts/lift_functions.py   # lift functions to LLVM IR via Remill
scripts/build_harness.py    # compile lifted IR + shim -> libharness.so
scripts/compare_lifted.py   # Unicorn reference vs lifted native
scripts/doctor.py     # build-tool readiness check
scripts/setup-remill.sh     # build Remill (Ubuntu 22.04 / proot-distro)
scripts/check-device.sh     # print device facts
harness/shim.cpp          # native shim running lifted functions
runtime/src/guest.c       # rottr_cpu guest state
runtime/include/rottr/guest.h
tests/test_*.py / tests/test_guest.c   # synthetic, no game needed
```

