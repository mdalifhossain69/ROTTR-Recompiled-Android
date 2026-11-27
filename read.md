# ROTTR Android — project handoff and implementation status

**Repository:** `mdalifhossain69/ROTTR-Recompiled-Android`  
**Active branch:** `main`  
**Last reconciled:** 2026-11-27

> **Truth first:** This is not a playable port. Android and diagnostic Vulkan foundations exist. A limited x86-64 interpreter exists. The selected AArch64 DBT/JIT does not exist yet. There is no complete PE loader, Windows API runtime or guest DLL loader. The graphics backend is not connected to actual ROTTR D3D11 calls. No first game frame has been achieved.

This file is a handoff summary. [README.md](README.md) is the short overview; [docs/ROADMAP.md](docs/ROADMAP.md) is the authoritative milestone tracker.

## 1. Product constraints

- Native Android ARM64 app and native Vulkan presentation.
- Target actual user-owned ROTTR installation; preserve Steam/EOS ownership and DRM checks.
- No Winlator-style desktop UI.
- Do not reintroduce Proton/FEXCore packaging.
- Keep implementation on `main`; no unnecessary branches.
- GitHub Actions remain manual-only; do not add push/PR triggers.
- Keep proprietary game files, assets, credentials and signing keys out of Git.
- Do not claim a feature works without code integration and actual verification.

## 2. Current code inventory

| Component | What is present | What remains absent |
|---|---|---|
| Android | Gradle module, JNI/native window and Vulkan renderer sources | Current build/device validation not established by this doc update |
| Launcher | Basic UI and storage-folder selection | Complete game validation and boot |
| PE | Read-only PE inspection | PE image mapper, relocations, imports, TLS, guest entry |
| CPU | Bounded x86-64 interpreter, guest state and authored test target | Production decoder/IR, AArch64 emitter, executable code cache, DBT dispatcher |
| Windows runtime | Some stubs and low-level foundations | Required Win32/NT semantics, process/thread/synchronization, dependency loading |
| D3D11 | Device/resource/immediate-context abstractions | Full COM ABI, object model, complete views/state and game integration |
| Vulkan | Resource/upload/copy/layout/pipeline/draw primitives | Full D3D11 mapping and live game command stream |
| DXBC | Container/token inspection and optional compiler adapter | Provisioned ARM64 shader compiler and actual game shader validation |
| Audio/input | Supporting modules and touch-control source | Verified connection to running game |
| UI/performance | Android app foundation | In-game configurable overlay/HUD and measured performance |

### CPU instruction interpreter

The source in `runtime/src/x64_interpreter.cpp` implements a bounded subset, including covered forms of:
- NOP, HLT, RET, PUSH/POP, relative CALL/JMP, short and near conditional branches.
- MOV immediate and selected register/memory MOV operations.
- ADD, SUB, OR, AND, XOR, CMP, TEST and group-1 immediate arithmetic.
- SHL/SAL, SHR, SAR.
- MOVZX, MOVSX and MOVSXD.
- ModRM/SIB and displacement/RIP-relative addressing for implemented forms.
- Bounds checks and explicit unsupported-instruction stop behavior.

This is not complete x86-64 coverage. SIMD/FPU, broad system instructions, atomics, exceptions and many encodings are missing. Some legacy byte-register encodings and architectural corner cases remain unsupported. The interpreter test source exists, but this handoff does not claim it has compiled or passed.

## 3. Selected CPU architecture — ADR 0002

The intended primary execution path is project-owned x86-64-to-AArch64 DBT/JIT with:
- Reusable length-safe x86-64 decoder and typed IR.
- AArch64 code emitter.
- Correct guest state, flags and Windows x64 calling convention handling.
- Safe executable memory (W^X), instruction-cache maintenance.
- Translated basic-block cache and invalidation.
- Interpreter fallback and differential tests.

**Current state: decision only.** No translator, emitter, executable-memory allocator, code cache or DBT dispatch exists. This is not same-ISA NCE: x86-64 guest code must be translated to ARM64 host instructions.

See [docs/adr/0002-aarch64-dbt-primary.md](docs/adr/0002-aarch64-dbt-primary.md). ADR 0001 is the earlier option evaluation and is superseded for the primary strategy.

## 4. Boot path still required

1. Validate the selected legitimate game installation.
2. Implement PE32+ image mapping, relocations, TLS and guest entry setup.
3. Implement x86-64-to-AArch64 DBT/JIT and prove controlled translated code on Android ARM64.
4. Implement Windows API/NT compatibility and required guest DLL/dependency loading.
5. Trace actual game startup and implement required APIs without fabricated stub success.
6. Complete D3D11 object/state/shader/resource semantics.
7. Provision ARM64 DXBC compilation and validate the actual shader corpus.
8. Connect guest D3D11 calls to Vulkan and Android swapchain.
9. Verify a real game-generated scene/frame on device.
10. Integrate audio, controller/touch UI, HUD, lifecycle and performance work.

CPU execution, PE loading, Windows compatibility and graphics are distinct workstreams. A successful PE inspection, test program, diagnostic renderer or APK build is not game boot.

## 5. Build and verification discipline

Use [docs/BUILDING.md](docs/BUILDING.md) for commands. At each task:
- Record exact commands and actual exit results.
- Distinguish authored tests from executed/passing tests.
- Distinguish compiled code from integrated runtime behavior.
- Record device model, Android version, GPU/driver and logs for device tests.
- Do not claim FPS before measuring real gameplay.

**This documentation reconciliation did not run CMake, CTest, Gradle, GitHub Actions or device tests.** No test pass or build success is claimed.

## 6. Next engineering task

Start the DBT work with a reusable, length-safe decoder/IR boundary shared with the existing interpreter. Then implement a deliberately small AArch64 emitter and differential tests. Do not expand interpreter coverage indefinitely as the performance strategy. Keep the interpreter as a reference oracle and fallback.

The DBT must not be described as implemented until translated ARM64 code is generated, safely executed, and shown to match interpreter state/memory on host and Android ARM64.

## 7. Definition of game boot

Only count success when the original, legitimately installed ROTTR process reaches a recognizable game scene and its own rendering workload is submitted through the Android Vulkan swapchain.

These do not count:
- APK builds successfully.
- EXE is inspected.
- The interpreter runs a small test program.
- Diagnostic Vulkan clear/marker/stage output appears.
- Synthetic D3D11/Vulkan test draw succeeds.
- Launcher opens without a game-generated frame.

**North star:** one native Android app, real ROTTR execution, real game-generated Vulkan rendering, then stable playable controls and performance.
