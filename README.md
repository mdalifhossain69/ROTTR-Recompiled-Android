# Rise of the Tomb Raider — Native Android Port

A work-in-progress native Android ARM64 project targeting Rise of the Tomb Raider (ROTTR), with Vulkan rendering and Android-native controls.

> **Current result: not playable.** The repository contains an Android app foundation, read-only PE inspection, a diagnostic Vulkan presentation path, partial D3D11/Vulkan graphics primitives, and a limited x86-64 interpreter. The game is not executing on Android. No AArch64 DBT/JIT exists yet, and no actual ROTTR frame has been rendered.

## Evidence-based status

| Area | Repository evidence | Status |
|---|---|---|
| Android app / ARM64 packaging | Android Gradle module, JNI and native ARM64 build configuration | Foundation present; current build not verified in this documentation update |
| Game folder selection | Android storage picker / launcher code | Implemented foundation; does not boot game |
| PE executable inspection | `runtime/src/pe_inspect.cpp` | Read-only inspection; not a loader |
| x86-64 interpreter | `runtime/src/x64_interpreter.cpp`, runtime API and test source | Limited instruction subset implemented; tests authored, not build/test verified |
| x86-64 → ARM64 DBT/JIT | No decoder-to-IR translator, AArch64 emitter or executable code cache | Not implemented; selected architecture only |
| PE32+ loading | No complete image mapping, relocations, TLS and import loader | Not implemented |
| Windows API / NT compatibility | A few stubs and abstractions only | Not sufficient for ROTTR |
| DLL/dependency loading | No complete guest module loader | Not implemented |
| Vulkan Android presentation | Native surface/swapchain diagnostic renderer | Foundation present; diagnostic output, not game rendering |
| D3D11-style device/context | `runtime/src/graphics/d3d11_device.*` | Partial abstraction; not full COM D3D11 |
| Vulkan resources and commands | `runtime/src/graphics/vulkan_backend.*` | Partial backend primitives; not connected to game calls |
| DXBC handling | Parser and optional vkd3d-shader adapter | Inspection/parser present; compiler not provisioned for Android ARM64 |
| Actual ROTTR draw stream/frame | No guest game runtime or connected game command stream | Not achieved |
| Audio, controller/touch overlay, HUD | Source foundations exist for some input/audio components | Game integration not verified; configurable in-game HUD not complete |
| Gameplay/performance/release | No gameplay baseline | Not started / not verified |

The detailed, authoritative status is [docs/ROADMAP.md](docs/ROADMAP.md). Graphics boundaries are in [docs/native-d3d11-vulkan.md](docs/native-d3d11-vulkan.md).

## Selected CPU direction

ROTTR is a Windows AMD64 executable; Android runs ARM64. This is **not same-ISA native code execution (NCE)**. The selected long-term CPU strategy is a project-owned x86-64-to-AArch64 dynamic binary translator (DBT/JIT) with a native code cache. The existing limited interpreter is retained as a correctness reference and temporary fallback.

**Important:** the DBT/JIT is a design decision only. No AArch64 translator, executable-memory manager or translated-code cache has been implemented. See [ADR 0002](docs/adr/0002-aarch64-dbt-primary.md).

## Intended product architecture

1. Android owns the application lifecycle, storage access, window, settings, input and Vulkan device.
2. A guest CPU runtime, PE loader, Windows API compatibility layer and dependency loader execute the user's legitimate Windows game installation.
3. D3D11 calls/resources/shaders/state are translated to Vulkan.
4. Actual game rendering is presented through Android's Vulkan swapchain.
5. Android-native audio, controls, HUD and performance work are integrated and measured.

CPU execution, PE loading, Windows compatibility and graphics translation are separate engineering workstreams. A diagnostic Vulkan frame, PE inspection, or successful APK build is not a game boot.

## Build and verification

- [android/README.md](android/README.md) — Android debug APK.
- [docs/BUILDING.md](docs/BUILDING.md) — native build and test commands.
- [docs/ROADMAP.md](docs/ROADMAP.md) — implementation and verification gates.

No build or test was run as part of the latest documentation-only update. Do not treat authored test files as passing tests.

## Repository rules

- Keep active development on `main`; do not create unnecessary branches.
- GitHub Actions must remain manual-only (`workflow_dispatch`); no push/PR automation.
- Do not commit game executables, proprietary assets, credentials or signing keys.
- Preserve Steam/EOS ownership and DRM checks.
- Mark work complete only when code is present, integrated as claimed, and relevant tests/device checks have actually passed.

Research project; not affiliated with Crystal Dynamics, Eidos-Montréal, Square Enix or Valve.
