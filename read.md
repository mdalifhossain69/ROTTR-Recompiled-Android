# ROTTR Android — Game Booting Plan and AI Handoff

> **Purpose:** This is the working handoff document for Claude, ChatGPT, or another coding agent continuing this repository. Read this file and the linked project documentation before changing code. It describes the route from the current foundations to the first real Rise of the Tomb Raider (ROTTR) frame on Android.

**Current truth:** This repository is not a playable port. The Android application and Vulkan presentation foundations exist, and several D3D11/Vulkan building blocks have been implemented. The original Windows x86-64 game is not yet executing on Android, and the graphics backend is not connected to the game's real rendering workload.

## 1. Product goal and non-negotiable direction

Build a **native Android ARM64 application** that runs the user's legitimate ROTTR installation and presents the actual game through Android's native Vulkan renderer.

- Android owns the app lifecycle, storage access, window, input, audio integration, settings and user interface.
- The long-term graphics path is Direct3D 11 game calls translated to Vulkan.
- The original game is x86-64 Windows software. Android ARM64 cannot execute its instructions directly; a real CPU execution/recompilation strategy and Windows API compatibility are required.
- Do not turn the product into a Winlator-style desktop environment or expose a Windows desktop.
- Do not reintroduce the rejected Proton/FEXCore packaging detour. Any proposed CPU execution technology must be evaluated against the native Android product goal and documented before adoption.
- Do not bypass DRM, Steam/EOS ownership checks, or other access controls. Do not commit the game EXE, game assets, or proprietary runtime files.

The target is a native Android experience, not a claim that Windows APIs or x86-64 execution cease to be necessary.

## 2. Verified repository baseline

The following is the known implementation baseline. Re-inspect the current `main` branch before relying on it; code may have changed since this document was written.

| Area | Baseline |
|---|---|
| Android ARM64 application | Project foundation exists |
| Game-folder selection / launcher | Basic implementation exists |
| Original EXE inspection | Read-only PE inspection exists; it does not execute the EXE |
| Android Vulkan surface and swapchain | Presentation foundation exists; current diagnostic output is not ROTTR |
| DXGI format mapping | Common formats implemented |
| D3D11 device/resource/immediate-context layer | Partial API/backend foundation with host tests |
| Vulkan buffers and images | Partial backend implementation |
| Texture initial-data upload | Implemented for supported formats/subresources |
| Texture layout tracking and image copies | Implemented as backend primitives |
| DXBC inspection | Parser/validation implemented |
| DXBC-to-SPIR-V | Optional vkd3d-shader adapter; dependency is not provisioned by the project |
| Vulkan shader modules | Creation/destruction primitives implemented |
| Vulkan graphics pipelines | Creation/destruction primitives implemented |
| Vulkan draw recording | Draw/indexed-draw command recording exists in the backend |
| Backend connected to Android renderer | Not yet integrated |
| Backend receiving actual ROTTR D3D11 calls | Not implemented |
| x86-64 execution on ARM64 | Not implemented |
| Windows API compatibility sufficient for ROTTR | Not implemented |
| Verified game boot / actual game frame | Not achieved |
| Playable gameplay | Not achieved |

Read these files as well:
- `README.md` — project overview and status.
- `docs/ROADMAP.md` — project-wide milestones and exit criteria.
- `docs/native-d3d11-vulkan.md` — graphics implementation and limitations.
- `docs/BUILDING.md` — build and host-test instructions.
- `android/README.md` — Android module instructions.

## 3. End-to-end boot path

The intended boot sequence is:

1. Android app starts and obtains access to the user's game directory.
2. Runtime validates the executable and required game files.
3. A PE32+ loader maps the executable and dependencies into a guest process address space.
4. The selected x86-64 execution strategy runs guest instructions on ARM64.
5. Windows API compatibility provides the functions required by the game and its dependencies.
6. Game initialization, configuration, asset access, threads and synchronization progress.
7. The game creates D3D11 devices/resources/shaders and submits rendering work.
8. The D3D11 compatibility layer translates those operations into Vulkan.
9. Vulkan renders into the Android-owned surface/swapchain.
10. Native Android input/audio and lifecycle handling support actual gameplay.

CPU execution, Windows API compatibility, and graphics translation are distinct workstreams. A Vulkan renderer cannot execute x86-64 instructions. A PE parser, a successful APK build, or a diagnostic Vulkan frame is not evidence of a game boot.

## 4. Implementation roadmap

### Milestone 0 — Establish a reproducible baseline

**Goal:** Know exactly what currently builds and passes before adding runtime complexity.

Tasks:
- Inspect the current `main` tree and recent commits.
- Read the project documentation and relevant source before editing.
- Run the documented host tests and Android build/CI checks where available.
- Record exact commands, environment, results and failures.
- Identify existing TODOs and avoid duplicating already implemented primitives.
- Keep the documentation synchronized with the actual code.

**Exit gate:** A baseline report identifies passing/failing tests and the exact current implementation boundaries.

### Milestone 1 — Choose and prove the x86-64 execution strategy

**Goal:** Prove that AMD64 guest instructions can execute on Android ARM64.

This is the largest architectural blocker. Do not start by pretending the PE loader alone solves it.

Tasks:
- Research feasible, maintained, ARM64-compatible x86-64 translation or recompilation approaches.
- Check licensing, Android/NDK support, required kernel features, ABI constraints, performance, exception handling, debugging support and distribution implications.
- Respect the product decision against a Proton/FEXCore packaging detour. Do not silently add either as a dependency.
- Write a short architecture decision record comparing viable options and their risks.
- Build a minimal isolated proof of concept that executes a small x86-64 test program on an Android ARM64 device or emulator.
- Verify register state, integer operations, calls/returns, memory access, faults/exceptions and multi-threading to the extent supported by the selected approach.
- Measure overhead and document unsupported instructions and system requirements.

**Exit gate:** A reproducible ARM64 Android proof of concept executes guest x86-64 code, with a documented technology decision. If no viable strategy is available, stop and report the blocker instead of building a fake loader-to-game boot path.

### Milestone 2 — PE32+ loader and guest process model

**Goal:** Load a test executable into the chosen guest execution environment.

Tasks:
- Parse PE32+ headers and validate architecture, image size, section table, alignments and bounds.
- Map sections with appropriate guest memory permissions.
- Apply base relocations.
- Resolve imports/exports against an explicit guest module registry.
- Handle required delay imports where needed.
- Implement TLS directory processing and callbacks when required.
- Set up the initial thread context and executable entry point according to the selected execution runtime.
- Implement guest memory allocation, protection, mapping and cleanup.
- Fail closed on unsupported PE features; emit actionable diagnostics.
- Test malformed inputs, overflow, invalid RVAs, missing imports and relocation edge cases.

**Exit gate:** A known-good test PE maps, resolves imports, runs its entry point and exits cleanly under the guest execution strategy. This is not yet a ROTTR boot.

### Milestone 3 — Windows API and runtime compatibility

**Goal:** Support the APIs ROTTR actually imports and calls, incrementally and with tests.

Priority areas:
- Kernel memory and virtual-memory APIs.
- Thread creation, thread-local storage, events, mutexes, semaphores and wait/wakeup behavior.
- Process/module information and loader APIs.
- File handles, directory enumeration, file mapping and async I/O as required.
- Synchronization primitives and correct timeout/error semantics.
- Timing, performance counters and multimedia timers.
- Required Winsock APIs if used by startup or services.
- Required user32/window/input abstractions adapted to the Android host.
- Required CRT and compiler runtime support.
- Exception/unwind behavior and thread exit/cleanup.

Method:
- Inspect the EXE and dependency imports, then trace actual calls at runtime.
- Implement only the needed API surface first, but make semantics correct.
- Maintain an API compatibility matrix: implemented, tested, stubbed, unsupported.
- Never return success from a stub when the operation has not actually been performed.

**Exit gate:** Small Windows test programs exercise the implemented APIs; the game progresses through each newly supported initialization stage with useful traces and no unresolved required imports.

### Milestone 4 — Dependency loading and game initialization

**Goal:** Reach the game's engine initialization using the user's legitimate installation.

Tasks:
- Inventory required DLLs and their transitive imports.
- Define the module search order and safe game-directory policy.
- Implement module loading and dependency initialization in the guest environment.
- Identify required Steam/EOS integration and support legitimate installed services where feasible.
- Preserve DRM, ownership checks and platform authentication; do not bypass them.
- Implement required filesystem behavior and asset path handling.
- Add startup stage tracing, crash reports and a reproducible diagnostic bundle.
- Verify configuration, localization and required data files are discovered.

**Exit gate:** The game reaches a clearly identified engine initialization milestone, with required dependencies loaded and no fabricated success statuses.

### Milestone 5 — Complete the D3D11-to-Vulkan runtime path

**Goal:** Convert real game D3D11 work into valid Vulkan work.

Build on the existing backend instead of replacing it without reason.

Tasks, in dependency order:
1. D3D11 shader objects and bytecode ownership.
2. Provision and integrate a supported ARM64 DXBC-to-SPIR-V compiler; build it reproducibly or document why it cannot be shipped.
3. Shader reflection and resource binding metadata.
4. Constant buffers, shader-resource views, unordered-access views and samplers.
5. Input layouts, vertex formats, index formats and topology conversion.
6. Render-target/depth-stencil views and render-target binding.
7. Blend, rasterizer, depth/stencil and multisample state.
8. Descriptor set layouts, descriptor pools and descriptor updates.
9. Pipeline layouts, compatible render passes/dynamic rendering and pipeline cache.
10. Swapchain image/framebuffer integration with the Android renderer.
11. D3D11 resource hazards, image layouts, barriers and synchronization.
12. Correct command submission, lifetime management and device-loss handling.
13. Capture actual game D3D11 calls and compare them with emitted Vulkan commands.

Important existing limitations to address:
- The optional vkd3d-shader adapter is not provisioned by the project.
- The backend is not instantiated/wired into the Android renderer.
- The game's D3D11 shader/state/resource bindings are incomplete.
- Existing Vulkan draw recording is a backend primitive, not proof that ROTTR is submitting draw calls.

**Exit gate:** A controlled D3D11 test workload renders correctly through the Android swapchain, then a captured game-generated workload produces Vulkan commands and visible output.

### Milestone 6 — First real ROTTR frame

**Goal:** Show a frame generated by the actual game on the Android display.

Tasks:
- Trace startup from process entry to the first scene.
- Fix missing APIs, asset access, shader translation, resource binding and synchronization based on real diagnostics.
- Confirm the draw stream originates from ROTTR, not a test scene or diagnostic renderer.
- Confirm the Vulkan backend targets the Android swapchain.
- Capture logs, frame diagnostics and device details.
- Test repeatability across cold starts.

**Exit gate:** A recognizable, actual ROTTR game scene is rendered on Android. Save evidence and record device, build, game version and exact test steps.

### Milestone 7 — Playable Android experience

**Goal:** Turn first-frame success into reliable gameplay.

Tasks:
- Touch controller overlay with configurable layout and positions.
- Controller and camera input mapping.
- Native audio and video playback paths required by the game.
- Save/load and configuration persistence.
- App pause/resume and surface recreation.
- GPU/CPU profiling, frame pacing and memory pressure handling.
- Thermal and battery-aware performance settings.
- Crash recovery and compatibility testing across target devices.

**Exit gate:** A user can control the game through a real gameplay segment, save and resume, and repeat the test without critical crashes.

## 5. Required engineering workflow for any AI agent

Any AI continuing this project must follow these rules:

1. **Start on `main`.** The repository is intended to have one active development branch. Do not create a feature branch or PR unless a platform constraint makes it unavoidable.
2. Fetch the latest `main` before editing. Do not assume this document's baseline is newer than the code.
3. Read the relevant source, tests, CMake files and docs before making changes.
4. Implement a coherent, testable vertical slice. Avoid large speculative scaffolding.
5. Add or update host tests for portable logic. Add Android/device tests for Android-specific behavior where feasible.
6. Build the affected targets. Run relevant tests. Report exact commands and actual results.
7. Do not claim a feature is complete merely because an interface, stub, parser or abstraction exists.
8. Update `read.md`, `README.md`, `docs/ROADMAP.md` and technical docs when implementation status changes.
9. Commit directly to `main` with a specific message when the repository connection permits it.
10. Do not claim an APK, CI run, device test, game boot, GPU frame or FPS result unless it was actually produced and verified.
11. Keep user-facing work native to Android. Do not add a Windows desktop shell or emulator-like launcher.
12. Do not commit proprietary game files, credentials, signing keys, generated game assets or large diagnostic dumps.
13. Preserve licensing and legitimate ownership requirements; never bypass DRM or platform authentication.
14. If blocked, explain the exact blocker, evidence, and smallest next experiment. Do not hide failure behind placeholder success.

## 6. Progress reporting format

At the end of each implementation task, report:

- **Implemented:** exact files and behavior added.
- **Integrated:** which runtime path now calls the code.
- **Tests:** exact commands and pass/fail results.
- **Device verification:** device/OS and observed result, or explicitly “not tested”.
- **Remaining blockers:** concrete missing pieces.
- **Next task:** one specific next implementation step.

Use these status labels consistently:
- `NOT STARTED`
- `IN PROGRESS`
- `IMPLEMENTED — NOT INTEGRATED`
- `INTEGRATED — NOT DEVICE VERIFIED`
- `DEVICE VERIFIED`
- `BLOCKED`

## 7. Immediate next task

**Do not start by adding more Vulkan draw primitives.** The immediate next task is:

> Produce an architecture decision record for x86-64 guest execution on Android ARM64, then build the smallest on-device proof of concept that executes a controlled x86-64 test program.

This resolves the fundamental CPU execution question before the team invests further in loader and Windows API work. In parallel, preserve the existing D3D11/Vulkan backend and its tests; do not claim it is connected to ROTTR until real game commands flow through it.

## 8. Definition of success

The project is considered booted only when the original, legitimately installed ROTTR game reaches a recognizable game scene and its own rendering workload is presented through the Android Vulkan swapchain.

These do **not** count as game boot:
- APK builds successfully.
- The EXE is parsed or inspected.
- A guest runtime starts without executing the game.
- A diagnostic Vulkan triangle, stage block or clear color appears.
- A test D3D11 workload renders.
- The game window or launcher opens without the actual game scene.

**North star:** one native Android app, real ROTTR execution, real game-generated Vulkan rendering, then stable playable controls and performance.
