# Approaches

## A. Windows build through translation layers (test first)
Box64 or FEX (x86-64 to ARM64), Wine/Proton (Windows APIs), DXVK or VKD3D-Proton
(Direct3D to Vulkan), and a Vulkan driver such as Mesa Turnip on Adreno GPUs.
Reported playable on one device; see `test-results.md`.

## B. Linux build through translation layers
Feral Interactive released an x86-64 Linux version using Vulkan. Running it with
Box64 or FEX would avoid Wine and Direct3D translation. Unverified; Android
compatibility of its runtime libraries is an open question.

## C. Static recompilation
Translate the x86-64 executable to ARM64 ahead of time, then replace the OS,
graphics, audio, input and Steam interfaces it depends on. We have found no
ready-made tool or working port for this game. Long-term research only.

## Related work
https://github.com/Rinto02/ROTTR-Recomp - translation-layer configs and scripts
(one commit and no releases when checked on 2026-11-26).
