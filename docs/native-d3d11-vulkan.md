# Native Android Direct3D 11 to Vulkan port

## Target architecture

The shipping app is a native Android ARM64 application. It must not expose a Windows desktop, Wine desktop, emulator shell, or user-managed compatibility environment.

The intended execution path is:

1. Recover/recompile the supported Windows x64 game code into ARM64-compatible native code, preserving the required Windows ABI and runtime semantics.
2. Route the game's Direct3D 11 device/context/resource calls into the project's D3D11 compatibility API.
3. Translate DXGI formats, resource layouts, pipeline state, and shader bytecode into Vulkan equivalents.
4. Execute translated commands on Android Vulkan queues and present through the Android surface.
5. Keep input, audio, lifecycle, touch controls, and performance UI native to Android.

A Direct3D-to-Vulkan layer does not translate x64 CPU instructions. CPU recompilation/translation and Windows API compatibility remain separate required workstreams.

## Implemented in this milestone

- Added a host-testable DXGI-to-Vulkan format table for common float, normalized, depth/stencil, and BC-compressed formats.
- Added block-aware row-pitch and image-size calculations for BC formats.
- Added overflow-safe size arithmetic; unsupported formats return no mapping/zero sizes rather than guessing.
- Added host unit tests and made the Android workflow run host tests before building the APK.

## Shader translation milestone

Added `runtime/src/graphics/dxbc_shader.*`:
- Bounds-checked DXBC container parsing for SHDR/SHEX chunks, shader stage, shader-model version, and token-stream length.
- A real DXBC-to-SPIR-V compiler adapter using the vkd3d-shader API when that library is linked; validates the returned SPIR-V magic/word alignment and reports compiler errors rather than producing placeholder modules.
- CMake auto-detects an externally provisioned `vkd3d-shader` header/library on Android. Without it, container inspection and tests still build, but shader compilation explicitly returns `CompilerUnavailable`.
- Host tests cover valid shader headers, malformed offsets/sizes/token counts, and unsupported stages.

**Important dependency status:** vkd3d-shader is not vendored or built by this repository yet. Therefore this commit adds the DXBC parsing and compiler integration seam, not a bundled, ready-to-run shader compiler. The Android build must provision a compatible ARM64 vkd3d-shader build before DXBC shaders can actually be translated at runtime.

## Not yet implemented

- D3D11 COM object/device/context ABI and lifetime model.
- Shader compiler provisioning and complete shader translation validation against ROTTR's actual shader corpus.
- D3D11 input assembler, rasterizer, blend, depth/stencil, render-target and sampler state translation.
- Command-list submission, synchronization, resource barriers/layout tracking, MSAA resolve.
- Actual game-code ARM64 recompilation and Windows API coverage.
- On-device validation against ROTTR rendering captures.

Do not describe the current milestone as a working game renderer or playable port. The existing Android Vulkan surface renderer is a clear-only diagnostic renderer; it does not yet draw ROTTR assets.

## D3D11 device/context milestone

Added `runtime/src/graphics/d3d11_device.*`:
- `Device` validates buffer and Texture2D descriptions, creates resource handles through a backend contract, tracks resource lifetime, supports buffer updates/copies, and implements CPU map/unmap for eligible buffers.
- `ImmediateContext` stores vertex/index/constant-buffer, shader-resource, render-target and topology bindings; validates draw and indexed-draw prerequisites/ranges; forwards draw/copy/update operations to the backend; and exposes a command trace for tests/diagnostics.
- A fake backend test exercises resource creation, invalid descriptors, map/unmap, bounds checks, bindings, draw calls, copy/update, and lifetime cleanup.

**Concrete Vulkan resource backend milestone:**
- Added `runtime/src/graphics/vulkan_backend.*`, a `Backend` implementation constructed from borrowed `VkPhysicalDevice`, `VkDevice`, `VkQueue`, and queue-family handles owned by `VulkanRenderer`.
- Creates real Vulkan buffers with host-visible/coherent memory, maps initial buffer bytes, supports bounded buffer uploads, and creates optimal-tiled Vulkan 2D images with device-local memory and image views. When initial texture data is supplied for single-sample color formats, packs each mip/array-layer subresource into a host-visible staging buffer, honors caller row/slice pitches, records buffer-to-image copies, and transitions the full image from undefined through transfer-destination to shader-read-only (for shader resources) or general layout. Compressed formats use block-aware row sizing and mip extents.
- Implements buffer-to-buffer copies through a dedicated transient command pool and synchronous queue submission.
- Exposes the renderer's Vulkan handles as borrowed getters; the renderer must outlive the backend.

**Explicit limitations:** this is not yet a complete D3D11 renderer. Initial-data uploads currently reject multisampled and depth/stencil textures; callers must provide valid data for every mip/array subresource. Texture image layouts are tracked independently for each mip/array-layer subresource. `transitionTexture` records explicit barriers and updates tracked state; `copyResource` performs compatible single-sample image copies across every mip/layer, transitions source/destination subresources to transfer layouts, and restores their prior layouts (or a readable/general layout for a previously undefined destination). Draw/draw-indexed fail closed because translated shaders, input layouts, render-pass compatibility, and graphics pipelines are not yet supplied. Descriptor/sampler binding, MSAA resolve, full D3D11 hazard tracking, and actual game draw submission remain pending. No game frame is rendered by this milestone.
