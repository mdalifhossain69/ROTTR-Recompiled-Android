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

## Not yet implemented

- D3D11 COM object/device/context ABI and lifetime model.
- Buffer and texture allocation/binding on Vulkan.
- DXBC shader decoding and translation to SPIR-V.
- D3D11 input assembler, rasterizer, blend, depth/stencil, render-target and sampler state translation.
- Command-list submission, synchronization, resource barriers/layout tracking, MSAA resolve.
- Actual game-code ARM64 recompilation and Windows API coverage.
- On-device validation against ROTTR rendering captures.

Do not describe the current milestone as a working game renderer or playable port. The existing Android Vulkan surface renderer is a clear-only diagnostic renderer; it does not yet draw ROTTR assets.
