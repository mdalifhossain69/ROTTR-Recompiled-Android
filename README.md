# ROTTR-Recomp

Goal: run a legally owned Steam copy of Rise of the Tomb Raider on an ARM64 Android device.

**Status: playable through translation layers on one tested device (see `docs/test-results.md`). No static port exists here.**

Working setup: [Winlator Ludashi 5.0](docs/winlator-ludashi-5.0.md)

## Rules
- No game files, assets, executables or Steam files in this repository.
- No DRM or ownership-check bypassing. Use your own Steam account.

## Plan
1. Run `scripts/check-device.sh` in Termux and record the result.
2. Reproduce the translation-layer result and record the full settings.
3. Record device, driver versions, settings and frame rate for every test.
4. Treat static recompilation as separate long-term research.
