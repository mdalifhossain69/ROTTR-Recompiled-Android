# ADR 0001: Windows x86-64 execution on Android ARM64

- Status: Decision pending implementation; no execution backend is linked
- Date: 2026-11-27
- Scope: CPU instruction execution only. This ADR does not select a Windows API layer or PE loader.

## Context

ROTTR is a Windows AMD64 PE32+ application. The Android target is ARM64. The current repository has a guest register-state structure, Remill-style memory intrinsics and a capability/status function, but it has no x86-64 decoder, translator, interpreter or execution backend. The status function correctly reports that no backend is linked.

The product must remain a native Android application. No Windows desktop shell, Proton/FEXCore packaging detour, DRM bypass or proprietary game/runtime files may be introduced.

## Options reviewed

| Option | Useful capability | Blocking concern for this project |
|---|---|---|
| Unicorn Engine 2.1.4 | x86-64 CPU emulation; Android NDK build is documented | GPLv2 conflicts with the repository's current MIT distribution choice unless the project deliberately changes licensing; CPU execution alone does not load PE files or implement Win32 |
| Box64 | MIT-licensed x86-64 Linux user-mode dynamic recompilation | Primarily Linux ELF/Linux ABI; not a direct Windows PE + Win32 solution and would require a separate Windows compatibility environment |
| FEXCore | Mature ARM64 x86-64 translation | Explicitly rejected by the product direction; do not add it |
| Proton/Wine stack | Windows API and game compatibility stack | Explicitly rejected as the product packaging/desktop direction; no silent reintroduction |
| Project-owned DBT | Can be designed around the Android host, PE guest process and existing runtime interfaces | Large engineering effort; correctness, ISA coverage, exceptions, SIMD, atomics, code cache and performance must be proven incrementally |

## Decision

Do not link Unicorn, Box64, FEXCore, Proton or Wine into the application as part of this change.

Proceed with a project-owned x86-64-to-AArch64 dynamic binary translation (DBT) workstream, subject to small, testable milestones. The first deliverable is not a ROTTR loader: it is an isolated decoder/IR/interpreter proof that runs a controlled x86-64 instruction sequence over bounded guest memory on both host tests and Android ARM64.

The implementation must keep the CPU backend separate from:
1. PE32+ image loading and relocations.
2. Windows DLL import/export resolution.
3. Win32 API and NT runtime compatibility.
4. D3D11-to-Vulkan graphics translation.

## Required first implementation slice

1. Define a documented guest CPU state, including GPRs, RIP, RFLAGS and XMM state.
2. Add a bounds-checked guest address-space abstraction with read/write/execute permissions.
3. Add a length-safe x86-64 instruction decoder. Reject unsupported encodings explicitly; never advance RIP using guessed instruction lengths.
4. Lower a deliberately small, documented integer/control-flow subset into a typed guest IR.
5. Add an interpreter for that IR as the correctness oracle.
6. Test arithmetic flags, partial-register writes, stack/call/return, branches, memory boundaries and invalid instructions against golden results.
7. Only after the interpreter is reliable, add AArch64 code generation and a code cache. Differentially compare translated execution with the interpreter.

No unsupported instruction may silently become a no-op or fabricated success.

## Non-goals for this slice

- Executing ROTTR.exe.
- Implementing Windows API calls.
- Claiming compatibility with arbitrary x86-64 binaries.
- Claiming game performance or a playable game.
- Adding a desktop/emulator UI.

## Exit criteria

The first CPU milestone is complete only when the same controlled x86-64 byte sequences produce identical architectural state and memory results in the reference interpreter and the ARM64 translated path, with reproducible host and on-device test evidence.

Until then, the runtime capability function must continue to report that a production Windows x86-64 execution backend is unavailable.
