# Approaches

## A. Windows build through translation layers (preferred integration target)

The complete chain for the Windows ROTTR executable is:

1. x86-64 Windows instruction execution on ARM64.
2. Windows API and loader compatibility (Wine/Proton or a compatible ARM64EC/WOW64 setup).
3. Direct3D 11 translation to Vulkan (DXVK); VKD3D-Proton is needed only for a Direct3D 12 path.
4. Vulkan presentation on Android, using the device's Vulkan driver (Turnip on supported Adreno devices).

**Important platform distinction:** FEX's full Linux user-mode emulator is not an Android runtime. FEXCore is a reusable CPU translation library and may be built for Android, but it is not by itself a complete Windows process environment. The FEX project explicitly says the Android OS environment is incompatible with the Linux environment FEX expects. Box64 also expects a Linux userspace; Android integration normally supplies a glibc environment/container such as Winlator.

The existing device configuration notes in `winlator-ludashi-5.0.md` record Proton 10 arm64ec, FEXCore 2611 (listed as the 32-bit emulator), and DXVK+VKD3D. That is a useful known-device reference, not proof that this app embeds those components. The current APK does **not** link FEXCore, Box64, Wine, Proton, DXVK or VKD3D.

## B. Linux build through translation layers

Feral Interactive released an x86-64 Linux version using Vulkan. Running it with
Box64 or FEX would avoid Wine and Direct3D translation. Unverified; Android
compatibility of its runtime libraries is an open question.

## C. Static recompilation

Translate the x86-64 executable to ARM64 ahead of time, then replace the OS,
graphics, audio, input and Steam interfaces it depends on. We have found no
ready-made tool or working port for this game. Long-term research only.

## Current implementation status

The Android build exposes an explicit x64-backend capability report. It currently reports **NOT LINKED** rather than treating a successful PE header check or Vulkan surface as proof that game code can execute. The next implementation step is a supported Android userspace/runtime integration, not adding FEXCore as an isolated library.

## Related work

https://github.com/Rinto02/ROTTR-Recomp - translation-layer configs and scripts
(one commit and no releases when checked on 2026-11-26).
