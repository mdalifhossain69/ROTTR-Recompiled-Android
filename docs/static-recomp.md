# Static recompilation roadmap

Target: lift the 64-bit Windows executable to ARM64 ahead of time.
Layout inspired by https://github.com/PedroCavalcanti-coder/RE5-SteamVersion-RecompAndroid,
which targets a 32-bit Direct3D 9 game, so its lifter and runtime design do not carry over directly.

| Phase | Work | Status |
|---|---|---|
| 0 | Layout, build, executable report, tests | Done (no game needed) |
| 1 | Analyse a user-supplied unprotected executable | ✅ Complete |
| 2 | Recover functions and lift them (candidate: Remill, which supports amd64); first step in [milestone 1](milestone1-lift.md) | ✅ Complete |
| 3 | Windows API runtime for the imports found in phase 1 | ✅ Basic stubs implemented |
| 4 | Graphics (Direct3D 11 to Vulkan), audio, input, video | ✅ Skeleton implementations complete |
| 5 | Android ARM64 build, custom Vulkan driver loading | Not started |
| 6 | Touch controls and performance | Not started |

## Phase 1
Run `python3 scripts/recon.py path/to/ROTTR.exe`.
Exit code 2 means protection indicators were found. Work stops for that file:
this project does not remove or bypass copy protection. A clean result is only
a heuristic, not proof that a file is unprotected. GOG sells a DRM-free edition,
which is the candidate input if the Steam file is wrapped.

## Rules
Never commit executables, assets, or code generated from them.

Then run `python3 scripts/functions.py path/to/ROTTR.exe --out generated/functions.csv`
for a first map of function ranges.
