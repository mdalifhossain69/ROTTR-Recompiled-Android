# ADR 0002: AArch64 dynamic binary translation as the primary CPU backend

- Status: **SELECTED — NOT IMPLEMENTED**
- Date: 2026-11-27
- Scope: x86-64 guest instruction execution on Android ARM64.

## Decision

The intended primary CPU path is a project-owned x86-64-to-AArch64 dynamic binary translator (DBT/JIT), with a native ARM64 code cache. The existing limited interpreter is retained as a correctness reference, diagnostic fallback and temporary execution path for instructions not yet translated.

This is not same-ISA NCE. ROTTR's guest instructions are AMD64/x86-64 while the Android host CPU is ARM64. Guest instructions must be translated before the ARM64 CPU can execute them.

## Why

An interpreter decodes and dispatches each guest instruction repeatedly. A DBT can translate basic blocks into ARM64 machine code, cache those blocks and execute the generated code directly on the host CPU. This is a performance-oriented design direction, not a measured performance result.

## Required architecture

1. Reusable, length-safe x86-64 decoder. Unsupported encodings must fail explicitly.
2. Typed intermediate representation (IR) for decoded instructions.
3. Explicit guest architectural state: GPRs, RIP, RFLAGS, XMM state and guest memory.
4. AArch64 emitter with host ABI handling, register allocation/spills, guest flag materialization and branch fixups.
5. Safe executable-memory lifecycle with write-then-execute/W^X policy and instruction-cache maintenance.
6. Translated basic-block cache with guest-code identity and invalidation for modified code.
7. Safe fallback to the reference interpreter at well-defined block boundaries.
8. Differential tests comparing translated execution with interpreter state and memory.
9. Android ARM64 tests and profiling counters for translation time, cache hits/misses and fallback.

## Correctness and safety requirements

- Do not execute writable code pages; enforce platform-supported W^X behavior.
- Validate guest memory accesses and branch targets.
- Preserve Windows x64 guest calling convention independently of the AArch64 host ABI.
- Do not assume x86 RFLAGS are equivalent to ARM64 NZCV; materialize guest flags correctly.
- Handle self-modifying code and invalidate translated blocks.
- Do not claim complete x86-64 coverage, NCE, game compatibility or performance without evidence.
- Keep PE loading, Windows APIs/NT compatibility, DLL loading and D3D11-to-Vulkan separate.

## Current implementation status

**No DBT/JIT implementation is present.** The repository currently has a limited x86-64 interpreter and its authored test source. It has no reusable translator IR, AArch64 emitter, executable code allocator, translated code cache or DBT dispatch path. No translated guest code has been verified on Android ARM64.

## Implementation sequence

1. Extract a reusable length-safe decoder and IR boundary from the current interpreter.
2. Implement a minimal AArch64 emitter for a small integer/control-flow subset.
3. Add safe executable-memory management and instruction-cache maintenance.
4. Add basic-block caching and code invalidation.
5. Differential-test generated blocks against the interpreter on host and Android ARM64.
6. Expand integer coverage, then SIMD/FPU, atomics, exceptions and unwind behavior based on actual game traces.

## Exit criteria

The first DBT milestone is complete only when the same controlled x86-64 byte sequences produce identical architectural state and memory through both interpreter and translated paths, with reproducible host and Android ARM64 test evidence.

This record documents a selected architecture, not an implemented execution backend.
