# D3D11-to-Vulkan implementation notes

## Goal and architecture boundary

The target is a native Android ARM64 application. The app owns the Android lifecycle, surface, Vulkan device and UI. The game's Direct3D 11 workload must be translated into Vulkan resources, shaders, state and commands.

This graphics workstream is independent of CPU execution. A Vulkan backend cannot execute Windows x86-64 instructions, load a PE executable, or provide the Windows APIs required by the game.

## Implemented foundations

### DXGI formats

- A host-testable mapping table covers a documented subset of common float, normalized, depth/stencil and BC-compressed formats.
- BC row-pitch and image-size calculations account for compression blocks.
- Size arithmetic is overflow checked.
- Unsupported formats fail explicitly rather than silently choosing a substitute.

### D3D11-style device and immediate context

runtime/src/graphics/d3d11_device.* provides:
- Buffer and Texture2D descriptors and validation.
- Resource handles/lifetime and backend create/destroy operations.
- Buffer update/copy and eligible CPU map/unmap behavior.
- Vertex/index/constant-buffer, shader-resource, render-target and topology state.
- Draw prerequisite/range validation and command tracing.
- Host fake-backend tests.

This is a project abstraction, not a complete COM-compatible D3D11 implementation. Full interface ABI, views, descriptors, samplers and state translation remain incomplete.

### Vulkan resource backend

runtime/src/graphics/vulkan_backend.* provides:
- Vulkan buffers and host-visible/coherent memory.
- Optimal-tiled 2D images, device-local allocations and image views.
- Staging uploads for supported single-sample color textures, including mip/array subresources and BC block-aware row sizing.
- Buffer copies, texture copies and per-mip/per-layer layout tracking.
- Explicit texture layout transitions.
- Vulkan shader-module creation from validated SPIR-V.
- Vulkan graphics-pipeline creation and owned-pipeline destruction.
- Caller-provided render-pass recording, pipeline/vertex/index binding, vkCmdDraw and vkCmdDrawIndexed recording, and queue submission.

The backend borrows Vulkan handles from the renderer; the renderer must outlive it. Current resource/copy synchronization is not a complete D3D11 hazard or queue model.

### DXBC shader handling

runtime/src/graphics/dxbc_shader.* validates DXBC container/chunk boundaries and shader token metadata. It has an optional vkd3d-shader adapter which can produce validated SPIR-V when that library is linked.

**Dependency limitation:** vkd3d-shader is not currently vendored or built by this repository for Android ARM64. Without a compatible library, shader compilation returns CompilerUnavailable. Parsing DXBC is not compiling it.

## Not yet integrated / required next

1. Provision and test an ARM64 vkd3d-shader build in CI.
2. Compile actual ROTTR shaders and validate shader-model/features.
3. Implement the required D3D11 views, descriptor sets, samplers, constant buffers and resource binding semantics.
4. Complete input assembler, rasterizer, blend, depth/stencil and render-target state mapping.
5. Implement synchronization, resource hazards, command lists and MSAA resolve.
6. Share the Android renderer's swapchain render pass/framebuffers or a compatible rendering interface with this backend.
7. Connect the guest game's D3D11 calls/command stream to the device/context and backend.
8. Capture and verify real ROTTR draws and a rendered game scene on-device.

## Honest milestone boundary

The repository has real Vulkan resource, pipeline and draw-command primitives. These primitives are not yet wired into the Android app's gameplay runtime. No ROTTR draw stream is currently generated, and no ROTTR game frame is claimed. The Android Vulkan presentation path is a separate diagnostic renderer until the integration above is complete.

See ROADMAP.md for project-wide milestone order and exit criteria.
