# D3D11-to-Vulkan implementation status

**Status: PARTIAL BACKEND PRIMITIVES — NOT CONNECTED TO ROTTR**

## Goal and boundary

The target is a native Android ARM64 application. Android owns the lifecycle, surface, Vulkan device and UI. The game's Direct3D 11 resources, shaders, state and commands must eventually be translated into Vulkan.

This workstream does not execute Windows x86-64 instructions, load PE files, provide Windows APIs or load game DLLs. CPU execution and Windows runtime compatibility are separate prerequisites.

## Backend direction

DXVK Native is the selected D3D11-to-Vulkan implementation direction; Android
ARM64 build, WSI, and guest COM ABI integration are not yet proven. See
[ADR 0003](adr/0003-d3d11-vulkan-via-dxvk-native.md). Do not treat the current
project-owned device/backend primitives as a complete replacement for DXVK.

## Code currently present

### DXGI formats

The source contains a host-testable mapping table for a subset of common float, normalized, depth/stencil and BC-compressed formats. BC row-pitch and image-size calculations account for compression blocks and use overflow checks. Unsupported formats are intended to fail explicitly.

### D3D11-style device and immediate context

`runtime/src/graphics/d3d11_device.*` contains:
- Buffer and Texture2D descriptors and validation.
- Resource handles/lifetime and backend create/destroy operations.
- Buffer update/copy and selected CPU map/unmap behavior.
- Vertex/index/constant-buffer, shader-resource, render-target and topology state.
- Draw prerequisite/range validation and command tracing.
- Host fake-backend test source.

This is a project abstraction, not a complete COM-compatible D3D11 implementation. Full interface ABI, views, descriptors, samplers, resource binding and complete state semantics remain incomplete.

### Vulkan resource backend

`runtime/src/graphics/vulkan_backend.*` contains primitives for:
- Vulkan buffers and host-visible/coherent memory.
- Optimal-tiled 2D images, device-local allocations and image views.
- Staging uploads for supported single-sample color textures, including mip/array subresources and BC block-aware row sizing.
- Buffer copies, texture copies and per-mip/per-layer layout tracking.
- Explicit texture layout transitions.
- Vulkan shader-module creation from validated SPIR-V.
- Vulkan graphics-pipeline creation and owned-pipeline destruction.
- Caller-provided render-pass recording, pipeline/vertex/index binding, draw/indexed-draw command recording and queue submission.

The backend borrows Vulkan handles from a renderer; the renderer must outlive it. These operations are primitives and do not establish a complete D3D11 hazard, synchronization or queue model.

### DXBC shader handling

`runtime/src/graphics/dxbc_shader.*` validates DXBC container/chunk boundaries and shader token metadata. An optional vkd3d-shader adapter can produce validated SPIR-V when a compatible library is linked.

**Dependency limitation:** this repository does not currently vendor or build vkd3d-shader for Android ARM64. Without a compatible library, compilation reports `CompilerUnavailable`. Parsing DXBC is not compiling it.

## Not implemented / not integrated

1. Reproducible ARM64 vkd3d-shader provisioning and build.
2. Actual ROTTR shader corpus compilation and validation.
3. Complete D3D11 views, descriptor sets, samplers, constant buffers and resource binding semantics.
4. Complete input assembler, rasterizer, blend, depth/stencil, render-target and MSAA state.
5. Complete synchronization, hazards, barriers, command lists and device/queue ownership.
6. Integration with Android renderer's swapchain render pass/framebuffers.
7. Guest game D3D11 calls/command stream connected to the device/context and Vulkan backend.
8. On-device capture proving game-generated draws and a real ROTTR scene.

## Honest milestone boundary

The repository contains Vulkan resource, pipeline and draw-command primitives. They are not currently wired into a live ROTTR process or its rendering workload. The Android Vulkan presentation path is diagnostic; it is not evidence of ROTTR rendering. No actual game draw stream or first ROTTR frame has been achieved.

See [ROADMAP.md](ROADMAP.md) for project-wide status and verification gates.
