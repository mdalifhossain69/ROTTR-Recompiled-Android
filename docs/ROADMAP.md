# Project roadmap and milestone status

**Last reconciled:** 2026-11-27  
**Overall status:** foundational development; no verified ROTTR gameplay on Android.

This is the single source of truth for project milestones. A component being present in the repository does not mean the full game uses it.

## Target

Deliver a native Android ARM64 application for Rise of the Tomb Raider with:
- Native Android launcher, storage access, lifecycle and settings.
- A real, supported way to execute or recompile the game's x86-64 code for ARM64.
- Windows API/runtime compatibility required by the game and middleware.
- Direct3D 11-to-Vulkan translation, including resources, shaders, state and draw submission.
- Android Vulkan swapchain presentation of actual game frames.
- Working audio, controller/touch input, configurable touch overlay and performance HUD.
- Repeatable builds, tests and on-device validation.

The user-facing app must remain a native Android experience. A Windows desktop/container/emulator interface is not the product target.

## Status legend

- **Done** — implemented and verified for the stated scope.
- **Partial** — useful code exists, but integration, coverage or runtime validation remains.
- **Blocked / not started** — required implementation is absent or a dependency has not been provisioned.
- **Not verified** — cannot claim success without a test or device evidence.

## Milestones

### M0 — Repository and Android application foundation
**Status: Partial / foundation present**
- [x] Android Gradle application and native ARM64 build configuration exist.
- [x] Launcher screen and Android Storage Access Framework folder selection exist.
- [x] Diagnostic logging and native bridge foundations exist.
- [ ] Verify lifecycle, surface recreation and stability on target Android devices.
- [ ] Verify installation/update signing behavior for the intended distribution workflow.

### M1 — Game installation identification and validation
**Status: Partial**
- [x] Read-only PE32+ executable inspection exists.
- [x] Executable architecture and metadata can be inspected.
- [ ] Validate the selected game directory and required data files, not just one EXE.
- [ ] Implement a safe game-data mount interface for the runtime.
- [ ] Preserve Steam/EOS ownership and DRM checks.

PE parsing/inspection is not loading or executing the game.

### M2 — CPU execution/recompilation and Windows runtime
**Status: Blocked / not implemented**
- [x] An x86-64 backend capability/status interface exists.
- [ ] Select and provision a viable Android ARM64 execution strategy or genuine static-recompilation pipeline for the actual game.
- [ ] Execute a controlled x86-64 test program on-device through the selected runtime.
- [ ] Implement PE loading, relocations, imports, TLS, exceptions, memory model and required process/thread behavior.
- [ ] Implement the Windows API and middleware surface actually used by ROTTR.
- [ ] Load the game and reach a deterministic boot milestone without bypassing ownership checks.
- [ ] Add crash reporting, process lifecycle and diagnostics.

**Exit criteria:** reproducible on-device evidence proves real game code is executing and reaches a documented initialization milestone. A “backend not linked” status or PE identification does not pass.

### M3 — D3D11 compatibility layer and Vulkan resources
**Status: Partial**
- [x] DXGI-to-Vulkan mapping for a subset of common formats, including selected compressed and depth/stencil formats.
- [x] Block-aware BC row-pitch/image-size calculations and overflow checks.
- [x] D3D11-like device/resource/immediate-context abstractions with validation and host fake-backend tests.
- [x] Vulkan primitives for buffers, images, staging uploads, resource copies and per-subresource layout tracking.
- [ ] Complete D3D11 object model, COM-compatible lifetime/refcount behavior and required interfaces.
- [ ] Complete texture formats, views, mapping rules and synchronization semantics required by the game.
- [ ] Implement descriptor/resource/sampler binding and constant-buffer updates.
- [ ] Implement input assembler, rasterizer, blend, depth/stencil and render-target state.
- [ ] Implement command lists, hazards, barriers and robust queue synchronization.
- [ ] Implement multisample resource handling and resolve.
- [ ] Validate against captures/traces from the actual game.

### M4 — Shader translation and Vulkan pipelines
**Status: Partial; compiler dependency is a blocker**
- [x] Bounds-checked DXBC container and shader-token inspection.
- [x] Optional vkd3d-shader compiler adapter.
- [x] SPIR-V output validation and Vulkan shader-module creation.
- [x] Vulkan graphics-pipeline creation primitive.
- [ ] Provision a compatible ARM64 vkd3d-shader compiler in the reproducible Android build.
- [ ] Translate and validate the actual ROTTR shader corpus and required features.
- [ ] Map D3D11 shader resources, constant buffers, samplers and stage interfaces to Vulkan descriptors.
- [ ] Create game-compatible pipeline layouts and state.
- [ ] Add shader/pipeline cache and failure diagnostics.
- [ ] Validate visual correctness in representative scenes.

The repository does not currently vendor or build vkd3d-shader for Android ARM64. Without it, DXBC compilation reports CompilerUnavailable.

### M5 — Connect real game draws to Android presentation
**Status: Partial backend primitives; runtime integration not done**
- [x] Vulkan backend can begin/end a caller-provided render pass.
- [x] Backend can bind owned graphics pipelines and vertex/index buffers.
- [x] Backend records vkCmdDraw and vkCmdDrawIndexed and submits its command buffer.
- [ ] Connect the Android renderer's swapchain render pass/framebuffers or compatible rendering interface.
- [ ] Establish one coherent Vulkan device/queue/resource ownership model.
- [ ] Connect the game's D3D11 command stream to the immediate context and Vulkan backend.
- [ ] Produce actual ROTTR draw calls and present them to the swapchain.
- [ ] Verify frames with captures, counters and on-device visual checks.
- [ ] Keep diagnostic placeholder rendering out of the gameplay path.

**Exit criteria:** a real ROTTR scene is visible on Android and captures prove game geometry, textures and shaders were submitted by the game path. A synthetic vkCmdDraw test is not sufficient.

### M6 — Audio, input, touch controls and in-game UI
**Status: Partial / integration not verified**
- [ ] Complete required game audio API compatibility and connect game audio to Android output.
- [ ] Connect physical controllers and Android input to the game's input model.
- [ ] Render an in-game touch overlay with configurable button/stick positions, sizes, opacity and visibility.
- [ ] Add an in-game performance HUD with FPS/frame time and useful CPU/GPU/memory diagnostics.
- [ ] Persist layouts/settings and support reset/recovery.
- [ ] Validate multi-touch, orientation, focus changes and input latency on-device.

Source files for input, touch and audio exist, but that does not prove they are connected to a running ROTTR process or drawn in-game.

### M7 — Performance, compatibility and release
**Status: Not started for real gameplay**
- [ ] Establish a stable baseline on target ARM64 devices.
- [ ] Profile CPU translation, Vulkan submissions, shader compilation, memory and thermal behavior.
- [ ] Optimize after correctness and repeatable measurements.
- [ ] Test saves, loading, cutscenes, audio, controls, suspend/resume and long sessions.
- [ ] Define supported device/driver requirements and known limitations.
- [ ] Produce reproducible signed builds and update-compatible release artifacts.
- [ ] Publish a compatibility matrix backed by device test logs.

Do not promise a specific FPS before real gameplay measurements exist.

## Current implementation inventory

| Component | What exists | What it does not prove |
|---|---|---|
| Launcher | Android UI and folder picker | Game boot |
| PE inspector | Reads executable metadata | CPU execution or DRM-free status |
| Vulkan renderer | Android presentation foundation | ROTTR rendering |
| x64 runtime interface | Reports backend capability | An x64 execution engine is linked |
| D3D11 device/context | Resource/context abstractions and tests | Full D3D11 ABI or game API |
| Vulkan backend | Resource, pipeline and command primitives | Game runtime is wired to it |
| DXBC | Parser and optional compiler adapter | ARM64 shader compiler is packaged |
| Draw calls | Real Vulkan command recording primitives | Game draw stream is generated |

## Immediate implementation order

1. **CPU/runtime proof:** architecture review is recorded in [`docs/adr/0001-x64-execution-android-arm64.md`](adr/0001-x64-execution-android-arm64.md). A bounded interpreter slice and host test target now exist; coverage includes register and memory MOV/arithmetic, immediate group-1 ADD/OR/AND/SUB/XOR/CMP, stack/CALL/RET, and short/near conditional branches. Tests are authored but this slice has not been build- or host-test-verified, and it is not production ISA coverage. Continue with register-width semantics, shifts, sign/zero extension, and broader architectural tests before AArch64 code generation and controlled on-device execution.
2. **Game loader and imports:** implement the loader and Windows APIs/middleware ROTTR requires, preserving ownership checks.
3. **Graphics compiler and state:** provision ARM64 DXBC compilation; complete shader/resource/state translation.
4. **Renderer integration:** connect the D3D11 context and Vulkan backend to Android's swapchain and the actual game command stream.
5. **First real frame:** boot to a known scene and verify actual game draw submissions.
6. **Input/audio/UI:** connect controls, audio, touch overlay and performance HUD to the live game.
7. **Optimization/release:** measure, fix correctness and stability, then optimize.

## Rules for updating this roadmap

- Update this file whenever a milestone materially changes.
- Link completed items to code/tests or a verified CI/device run where possible.
- Keep “implemented”, “integrated”, “host-tested” and “device-verified” distinct.
- Never mark a milestone complete because only a skeleton, interface, mock backend or build target exists.
- Never describe a diagnostic renderer, synthetic draw test or APK installation as playable gameplay.
