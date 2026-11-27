# Project roadmap and implementation status

**Reconciled:** 2026-11-27  
**Overall status:** foundational native Android work; no verified ROTTR gameplay.

This file is the project status source of truth. Status describes code present in the repository, not plans, authored-but-unrun tests, or assumptions.

## Status labels

- **IMPLEMENTED** — source code exists for the stated limited scope.
- **PARTIAL** — some primitives exist; required coverage or integration is missing.
- **IMPLEMENTED — NOT TEST VERIFIED** — code/test source exists, but no successful test evidence is recorded.
- **SELECTED — NOT IMPLEMENTED** — architecture chosen; no implementation yet.
- **NOT STARTED** — required code is absent.
- **NOT VERIFIED** — no current reproducible build/device evidence.

## Current milestone summary

| Workstream | Evidence in repository | Honest status |
|---|---|---|
| Android app / ARM64 project | Gradle app, JNI, native renderer sources | Foundation implemented; latest build not verified here |
| Game directory selection | Android launcher/storage selection code | Basic UI path exists; no game boot |
| PE inspection | `runtime/src/pe_inspect.cpp` | Read-only inspection implemented; no PE loader |
| x86-64 interpreter | `runtime/src/x64_interpreter.cpp`, runtime header and CMake test target | Limited subset implemented; test source not verified |
| x86-64 → AArch64 DBT/JIT | ADR 0002 only | Selected, not implemented |
| Windows PE loader | No complete image mapper/relocator/TLS/import pipeline | Not started |
| Windows API / NT layer | Some stubs and basic structures | Insufficient; not game compatible |
| Guest DLL loading | No complete guest module/dependency loader | Not started |
| DXGI/Vulkan formats | Mapping and size helpers | Partial subset implemented |
| D3D11 device/context | Resource/context abstractions and tests | Partial; not full COM ABI |
| Vulkan resource backend | Buffers, images, uploads, copies, layout tracking | Partial primitives |
| DXBC | Container/token parser and optional compiler adapter | Parser present; ARM64 compiler not provisioned |
| Vulkan shader/pipeline/draw primitives | Backend functions exist | Implemented as primitives; not game integrated |
| Actual game draw stream | No guest runtime integration | Not started |
| First actual ROTTR frame | No evidence | Not achieved |
| Audio/input/touch/HUD integration | Supporting source modules exist | Partial/unverified; not connected to running game |
| Performance/release validation | No gameplay baseline | Not started |

## M0 — Android application foundation

**Status: PARTIAL**

Present:
- Android Gradle application and ARM64 native build configuration.
- Launcher and storage-folder selection foundations.
- JNI/native window and Vulkan renderer sources.
- Diagnostic Vulkan presentation path.

Not established:
- A current successful build from this exact revision.
- Full lifecycle/surface-recreation stability across target devices.
- Game startup.

## M1 — Game installation inspection

**Status: PARTIAL**

Present:
- Read-only PE32+ executable inspection and metadata reporting.
- Local game input guidance; proprietary game files are not committed.

Missing:
- Complete selected-folder validation and game-data inventory.
- Safe runtime game-data mount.
- PE image mapping, relocations, imports, TLS and guest process initialization.

PE inspection is not PE loading or execution. DRM and store ownership checks must remain intact.

## M2 — CPU execution and Windows runtime

**Status: PARTIAL interpreter; DBT SELECTED, NOT IMPLEMENTED**

Present:
- Guest CPU state includes 16 GPRs, RIP, RFLAGS and XMM storage.
- Bounds-checked x86-64 interpreter with a limited instruction subset.
- CMake target and authored test source for the interpreter.
- ADR 0002 selects a project-owned x86-64-to-AArch64 DBT/JIT as the intended primary performance path.

Interpreter coverage in source includes a subset of:
- NOP/HLT/RET and stack PUSH/POP.
- Relative CALL/JMP and short/near conditional branches.
- MOV immediate and supported register/memory MOV forms.
- Integer ADD/SUB/OR/AND/XOR/CMP/TEST forms and group-1 immediates.
- SHL/SAL/SHR/SAR forms.
- MOVZX/MOVSX/MOVSXD forms.
- ModRM/SIB, displacement and RIP-relative addressing for covered operands.
- Bounds checks and explicit stop reasons for unsupported operations.

Limitations:
- This is not a complete x86-64 ISA; SIMD/FPU, broad system instructions, atomics, exceptions and many encodings are not implemented.
- Legacy high-byte register encodings and other edge semantics may remain unsupported; inspect source/tests before extending coverage.
- The test file is authored, but no successful compile/CTest result is recorded for this snapshot.
- There is no reusable production decoder/IR pipeline, AArch64 emitter, executable-memory manager, W^X code allocator, translated block cache or DBT execution entry point.
- No x86-64 guest code has been demonstrated executing through an ARM64 translated backend on device.

Next:
1. Extract a length-safe decoder/IR boundary shared by the reference interpreter and translator.
2. Implement a minimal AArch64 emitter and correct guest-state/host-ABI handling.
3. Add safe executable-memory lifecycle, instruction-cache maintenance and W^X policy.
4. Add block cache, code invalidation and safe interpreter fallback.
5. Differential-test translated blocks against the interpreter, then run the same corpus on Android ARM64.

**Do not call this NCE:** the guest ISA differs from the host ISA. The intended mechanism is DBT/JIT translation to native ARM64 instructions.

## M3 — PE loader and Windows compatibility

**Status: NOT STARTED as a complete game runtime**

Required:
- PE32+ section mapping, relocations, TLS and entry-point setup.
- Guest virtual memory and process/thread lifecycle.
- Import/export resolution and guest DLL loading.
- Windows API/NT compatibility for the actual imported and dynamically resolved calls.
- Correct synchronization, exceptions/unwinding, file mapping and I/O behavior.
- Real dependency inventory and initialization traces.

Existing PE inspection and Win32 stub files do not satisfy these requirements. No game DLL loading or Windows runtime compatibility is claimed.

## M4 — D3D11-to-Vulkan graphics

**Status: PARTIAL**

Present:
- DXGI format mapping and BC size/pitch helpers for a subset of formats.
- D3D11-style device/resource/immediate-context abstractions.
- Buffer/texture operations, selected context state and draw validation/tracing.
- Vulkan buffer/image/view/staging upload/copy/layout tracking primitives.
- Vulkan shader-module, graphics-pipeline, draw-command recording and submission primitives.

Missing:
- Full COM-compatible D3D11 interfaces and lifetime semantics.
- Complete views, descriptors, samplers, constant-buffer/resource binding and pipeline state.
- Full synchronization, hazards, barriers, command lists and MSAA behavior.
- One coherent device/queue/resource ownership integration with Android renderer.
- Game D3D11 call stream feeding these components.

The code is a partial compatibility foundation, not a complete D3D11 implementation.

## M5 — Shader compilation and real game rendering

**Status: PARTIAL primitives; actual game path NOT STARTED**

Present:
- DXBC container/token inspection.
- Optional vkd3d-shader adapter.
- Vulkan shader module and graphics pipeline creation primitives.

Missing:
- Reproducible vkd3d-shader provisioning/build for Android ARM64.
- Translation and validation of the actual ROTTR shader corpus.
- D3D11 shader reflection/resource mapping and descriptor layout.
- Connection from game D3D11 calls through Vulkan backend to Android swapchain.
- Actual ROTTR draw packets, geometry, textures, shaders and visible game frame.

A diagnostic clear/stage renderer or synthetic Vulkan draw is not a ROTTR frame.

## M6 — Audio, input, touch UI and HUD

**Status: PARTIAL source foundations; game integration NOT VERIFIED**

Input, touch-control, audio and performance source modules exist. Their presence does not prove connection to a running guest game.

Still required:
- Connect game input APIs to Android controller/touch input.
- In-game configurable touch overlay (position, size, opacity and visibility).
- Performance HUD with measured FPS/frame time and CPU/GPU/memory data.
- Required game audio API compatibility and Android output.
- Persist settings/layout and validate multitouch, orientation and lifecycle.

## M7 — Performance, compatibility and release

**Status: NOT STARTED for gameplay**

No real game performance baseline or playable scene is available. Do not promise FPS.

Required after first real frame/gameplay:
- Profile translation, CPU, GPU, shader compile, memory and thermal behavior.
- Optimize using measured traces.
- Test saves, loading, cutscenes, audio, controls, suspend/resume and long sessions.
- Establish device compatibility and reproducible signed/update-compatible release builds.

## Verification record for this documentation update

- Repository source and Markdown files were inspected on `main`.
- No source-code implementation was changed in this documentation update.
- No CMake build, CTest run, Gradle/APK build, GitHub Actions run or device test was performed.
- Therefore, test files are described as authored, not passing; runtime features are not described as verified.

## Rules for future status changes

- Distinguish source present, integrated, host-tested, Android-built and device-verified.
- Link the exact implementation/test evidence where possible.
- Do not mark a milestone complete because a stub, interface, test source or optional adapter exists.
- Update this file when code or actual test/device evidence changes.
- Keep active development on `main`; GitHub Actions remain manual-only.
