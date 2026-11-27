# ADR 0002: AArch64 dynamic binary translation is the primary CPU backend

- Status: Accepted direction; implementation not yet present
- Date: 2026-11-27
- Scope: x86-64 guest instruction execution on Android ARM64

## Decision

The primary CPU execution path will be a project-owned x86-64-to-AArch64 dynamic binary translator (DBT/JIT), with a native ARM64 code cache. The existing x86-64 interpreter is retained as a correctness oracle, diagnostic fallback and temporary execution path for instructions not yet translated.

This is not same-ISA NCE. ROTTR is an AMD64 Windows PE32+ executable and the Android host is ARM64; guest instructions must be translated before the host CPU can execute them.

## Why this direction

- An interpreter performs decode/dispatch for every guest instruction and is unlikely to deliver suitable performance for a large game.
- A DBT can translate basic blocks once, cache generated AArch64 code and execute those blocks directly on the host CPU.
- The existing bounded interpreter provides a useful reference for differential tests while the translator grows.
- A project-owned DBT avoids silently adding the previously rejected FEXCore/Proton stack or changing project licensing through an unreviewed dependency.

## Required DBT architecture

1. Reuse or extract a length-safe x86-64 decoder. Unsupported instructions must fail explicitly.
2. Lower decoded instructions into a small typed, architecture-neutral IR.
3. Keep guest architectural state explicit: GPRs, RIP, RFLAGS, XMM state and guest memory access.
4. Implement an AArch64 emitter with correct host ABI, register allocation/spills, flag materialization and branch fixups.
5. Allocate writable code memory, emit code, transition it to executable memory, flush the instruction cache, and enforce W^X where supported.
6. Cache translated basic blocks by guest address plus code-generation/version identity; invalidate blocks when guest code changes.
7. Route unsupported instructions to the interpreter only at safe block boundaries, preserving exact guest state.
8. Add differential tests comparing interpreter and translated state/memory, then run the same corpus on Android ARM64.
9. Add profiling counters for translated blocks, cache hits/misses, fallback instructions and translation time.

## Safety and correctness requirements

- Never execute writable code pages; use a write-then-execute policy and platform-supported cache maintenance.
- Validate all guest memory accesses and branch targets.
- Preserve Windows x64 guest calling convention independently of the AArch64 host ABI.
- Do not assume guest flags map directly to host NZCV; materialize guest RFLAGS correctly.
- Handle self-modifying code and code-cache invalidation before claiming compatibility.
- Do not claim NCE, complete x86-64 support, game compatibility or performance until demonstrated on-device.
- Keep PE loading, Windows API/NT compatibility, DLL loading and D3D11-to-Vulkan as separate workstreams.

## Implementation order

1. Extract a reusable decoder/IR boundary from the current interpreter.
2. Implement a minimal AArch64 emitter for a small integer/control-flow subset.
3. Add code-cache and executable-memory management.
4. Differential-test generated blocks against the interpreter on host and Android ARM64.
5. Expand integer ISA, then SIMD/FPU, atomics, exceptions and unwind behavior based on real game traces.

## Current status

The repository currently has a limited interpreter and authored tests. It does not yet contain an AArch64 translator, executable code cache, or Android DBT execution path. This ADR records the chosen direction, not a completed backend.
