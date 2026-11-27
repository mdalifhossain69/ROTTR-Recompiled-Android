# ADR 0001: Windows x86-64 execution on Android ARM64 — initial evaluation

- Status: **Superseded for the primary CPU strategy by ADR 0002**
- Date: 2026-11-27
- Scope: Historical option evaluation for CPU execution only.

## Context at the time

ROTTR is a Windows AMD64 PE32+ application; the Android host is ARM64. This document was written before the repository gained its current limited x86-64 interpreter. The current repository contains a guest CPU state, a bounded interpreter and an interpreter test target. It does **not** contain a production x86-64 execution backend or AArch64 translator.

The product must remain a native Android application. Do not introduce a Windows desktop shell, Proton/FEXCore packaging detour, DRM bypass or proprietary game/runtime files.

## Options reviewed

| Option | Useful capability | Project concern |
|---|---|---|
| Unicorn Engine | x86-64 CPU emulation | GPLv2 licensing implications for this MIT project; CPU engine alone is not PE/Win32 compatibility |
| Box64 | x86-64 Linux user-mode dynamic recompilation | Primarily Linux ELF/Linux ABI; not a direct Windows PE + Win32 solution |
| FEXCore | ARM64 x86-64 translation | Rejected by the product direction; do not add |
| Proton/Wine | Windows API/game compatibility stack | Rejected as the product packaging/desktop direction |
| Project-owned DBT | Can be designed around Android ARM64 and the guest process model | Significant engineering and correctness/performance risk |

## Historical conclusion

The project should not silently add the above external runtimes. The subsequent decision is recorded in [ADR 0002](0002-aarch64-dbt-primary.md): a project-owned x86-64-to-AArch64 DBT/JIT is selected as the intended primary CPU backend. The existing interpreter is retained as a reference/fallback.

## Current implementation boundary

- A limited x86-64 interpreter exists in `runtime/src/x64_interpreter.cpp`.
- Its supported instruction subset is incomplete; see [ROADMAP.md](../ROADMAP.md).
- No AArch64 code emitter, executable-memory manager, translated code cache or on-device DBT path is present.
- PE loading, guest DLL loading, Windows APIs and D3D11/Vulkan integration remain separate unfinished workstreams.

This ADR records an architectural evaluation, not a completed runtime.
