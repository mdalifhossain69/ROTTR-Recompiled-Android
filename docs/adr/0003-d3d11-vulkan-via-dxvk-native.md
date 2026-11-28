# ADR 0003: Use DXVK Native for D3D11-to-Vulkan

- Status: **SELECTED — ANDROID INTEGRATION NOT PROVEN**
- Date: 2026-11-28
- Scope: D3D11/DXGI rendering for the Windows x64 game on Android ARM64.

## Decision

Use DXVK Native as the preferred D3D11-to-Vulkan implementation instead of
continuing to grow the project's partial D3D11/Vulkan backend. Keep the existing
backend and tests until the replacement passes the Android integration gates.

Do not add a separate vkd3d-shader build as the D3D11 renderer. DXVK Native
builds its D3D11 library with its `dxbc-spirv` shader subproject.

## Evidence and limits

The upstream [DXVK project](https://github.com/doitsujin/dxvk) supports D3D11
translation to Vulkan and documents a native mode for applications that do not
use Wine. Its native build exposes SDL2, SDL3, and GLFW WSI backends; Android is
not listed as a built-in WSI backend. The upstream D3D11 target includes the
DXVK device, D3D11, DXGI, and DXBC-to-SPIR-V implementation.

DXVK Native is not a drop-in replacement for this project's guest runtime.
The game is x86-64 Windows code running on an Android ARM64 host. A host DXVK
object or COM vtable cannot be exposed directly to translated guest code:
guest calls use the Windows x64 ABI while native functions use the Android
AArch64 ABI. The integration needs explicit guest-to-host entry points and
ABI-safe COM method thunks. Android surface and lifecycle ownership also need
to be connected to an available WSI backend or a project Android backend.

This decision does not change CPU translation, PE/DLL loading, Win32/EOS/Steam
compatibility, or ownership and DRM requirements.

## Implementation gates

1. Pin a reviewed DXVK Native revision and record all source/license
   dependencies; do not rely on a moving upstream branch.
2. Cross-build the D3D11/DXGI libraries for Android ARM64 with the selected
   Android-capable WSI path.
3. From an Android-native smoke test, create a D3D11 device, clear a target,
   present through the app's Android surface, and validate lifecycle/surface
   recreation.
4. Prove a guest Windows x64 call can create and use D3D11/DXGI objects through
   explicit ABI-safe thunks; never forward native vtable pointers directly.
5. Only then route ROTTR calls into DXVK Native and validate actual game
   shaders and draws.

The first successful native clear/present is not a ROTTR frame or a playable
game. The game still has substantial unresolved imports and requires legitimate
Steam/EOS components where applicable.
