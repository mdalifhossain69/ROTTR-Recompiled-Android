# Executable analysis record

This document records read-only analysis performed on 2026-11-26 against an owner-supplied ROTTR executable. The executable itself is not stored in this repository.

| Item | Observed value |
|---|---|
| SHA-256 | 4daad0e31a0b64bf5cc079881ba254c5f5392ce506444a7f8b38c28076437c7f |
| File size | 27,582,000 bytes |
| Format | PE32+, AMD64 / x86-64 |
| Entry point RVA | 0xcc7170 |
| .text size | 13,630,142 bytes |
| .pdata size | 864,600 bytes |
| .shader size | 7,037,656 bytes |

The inspection found no protection indicators using the tool's heuristics. **This is not proof that the executable is DRM-free or that ownership checks are absent.** The report does not authorize patching or bypassing protection.

## Observed imports

| Area | DLLs |
|---|---|
| Windows/system/runtime | KERNEL32, USER32, GDI32, ADVAPI32, SHELL32, SHLWAPI, ole32, COMDLG32, UxTheme, CRYPT32, dbghelp, faultrep, MSVCP140, VCRUNTIME140, api-ms-win-crt-* |
| Network | WS2_32, WININET, IPHLPAPI |
| Audio/video | WINMM, MSACM32, bink2w64 |
| Input/devices | SETUPAPI, HID, stream_engine, openvr_api |
| Store services | steam_api64, EOSSDK-Win64-Shipping |

The absence of a statically listed Direct3D DLL does not prove that the game does not use Direct3D; it may resolve graphics APIs dynamically.

## What this report does not establish

- It does not load or execute the executable.
- It does not establish that the file is unprotected.
- It does not establish that the import list is complete for dynamically loaded modules.
- It does not prove that the game can run on Android.
- It does not replace runtime tracing of the actual game.

Preserve Steam/EOS ownership checks and use a legitimate user-owned game installation.
