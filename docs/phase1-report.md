# Phase 1: executable report

Run by the owner with `scripts/recon.py` on 2026-11-26. No game file is stored here.

| Item | Value |
|---|---|
| SHA-256 | `4daad0e31a0b64bf5cc079881ba254c5f5392ce506444a7f8b38c28076437c7f` |
| File size | 27,582,000 bytes |
| Format | PE32+, x86-64 |
| Entry point RVA | `0xcc7170` (inside `.text`) |
| `.text` | 13,630,142 bytes, entropy 6.47 |
| `.pdata` | 864,600 bytes, room for 72,050 entries |
| `.shader` | 7,037,656 bytes |
| Protection indicators | None found (heuristic, not proof) |

## Imports that need handling
| Area | DLLs |
|---|---|
| Windows system and C/C++ runtime | KERNEL32, USER32, GDI32, ADVAPI32, SHELL32, SHLWAPI, ole32, COMDLG32, UxTheme, CRYPT32, dbghelp, faultrep, MSVCP140, VCRUNTIME140, api-ms-win-crt-* |
| Network | WS2_32, WININET, IPHLPAPI |
| Audio and video | WINMM, MSACM32, bink2w64 |
| Input and devices | SETUPAPI, HID, stream_engine, openvr_api |
| Store services | steam_api64, EOSSDK-Win64-Shipping |

No Direct3D DLL is in the import table. It is presumably loaded at run time; not yet checked.
Steam ownership checks stay in place; they are not to be stubbed out or bypassed.
