# Rise of the Tomb Raider — Native Android Port

A work-in-progress project to bring Rise of the Tomb Raider (ROTTR) to Android as a native ARM64 application with Vulkan rendering and Android-native controls.

**This is not currently a playable port.** The Android app, executable inspection, Vulkan presentation foundation, and several graphics-translation building blocks exist. The Windows x86-64 game is not yet executing on Android, and the graphics backend is not yet connected to ROTTR's real rendering workload.

## Project status

| Area | Current status |
|---|---|
| Android launcher and game-folder selection | Implemented |
| PE executable inspection | Implemented; inspection only |
| Android Vulkan surface/swapchain presentation | Foundation implemented; diagnostic rendering only |
| x86-64 game-code execution on ARM64 | Not implemented / backend not linked |
| Windows API compatibility and game process loading | Incomplete |
| D3D11 resource/device/context foundations | Partial, host-testable API |
| DXGI format mapping | Implemented for a documented subset |
| DXBC inspection | Implemented |
| DXBC to SPIR-V compilation | Optional adapter only; compiler dependency not provisioned |
| Vulkan buffers, images and copies | Partial backend implementation |
| Vulkan pipeline creation and draw-command recording | Implemented as backend primitives; not connected to game |
| ROTTR shader/state/resource translation | Incomplete |
| Game audio and input integration | Incomplete |
| In-game touch overlay and performance HUD | Not implemented |
| Verified ROTTR gameplay on Android | Not achieved |

The authoritative milestone checklist is in [docs/ROADMAP.md](docs/ROADMAP.md). Graphics implementation boundaries are in [docs/native-d3d11-vulkan.md](docs/native-d3d11-vulkan.md).

## Intended architecture

1. A native Android ARM64 application owns the activity, storage access, window, lifecycle, input and Vulkan device.
2. A supported x86-64 execution/recompilation solution and Windows API compatibility layer load and execute the user's legally obtained game installation.
3. The game's Direct3D 11 calls are translated into Vulkan resources, shaders, state and commands.
4. The game renders into Android's Vulkan swapchain.
5. Native Android audio, touch/controller input, settings, diagnostics and performance UI are connected to the running game.

CPU execution and graphics translation are separate workstreams. Vulkan does not execute Windows x86-64 instructions. A PE inspection result or successful APK build does not mean the game has booted.

## Build and verification

- Android module: [android/README.md](android/README.md).
- Native CMake and host tests: [docs/BUILDING.md](docs/BUILDING.md).
- Graphics implementation and limitations: [docs/native-d3d11-vulkan.md](docs/native-d3d11-vulkan.md).

Do not infer gameplay support from a successful build or diagnostic Vulkan frame. Gameplay is only considered working after the actual game process reaches a known scene and real game draw calls are observed on-device.

## Repository rules

- Do not commit the game executable, game assets, Steam/Epic files, or generated proprietary game content.
- Use the user's own legitimate game installation.
- Do not bypass DRM, Steam/EOS ownership checks, or other access controls.
- Keep implementation status evidence-based. Mark work complete only when code is integrated and relevant tests/device checks pass.

## License and affiliation

Research project; not affiliated with Crystal Dynamics, Eidos-Montréal, Square Enix, or Valve.
