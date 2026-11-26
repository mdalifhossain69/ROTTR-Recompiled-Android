# Winlator Ludashi 5.0 setup

Settings from the owner's screenshots (2026-11-26), reported to give a very good
experience on a Redmi Turbo 4 Pro (Snapdragon 8s Gen 4, 12 GB, HyperOS 4).
Not tested on other devices. Settings not listed here were not captured.

## General
| Setting | Value |
|---|---|
| Environment | Container-2, Proton 10.0-6 arm64ec |
| Audio Driver | Alsa-Oboe |
| Fullscreen Stretched | On |
| Winlator HUD | Modern |
| DXVK HUD | Off |
| Locale (LC_ALL) | en_US |
| MIDI SoundFont | Disabled |
| Game Saves | On |

A second container (Proton 11.0-7 arm64ec) exists but was not the one selected.

## Video
| Setting | Value |
|---|---|
| Screen Size | 1280x720 (16:9) |
| Display Server | X++ |
| Renderer | DisplayX |
| Surface format | RGBA |
| Backend DisplayX | X11 |
| Boost CPU when presenting | On |
| Submit requests at refresh rate | Off |
| Present Mode | fifo |
| OpenGL Driver | freedreno |
| Freedreno OpenGL | 26.3.0 |
| Vulkan Driver | turnip27.0.0 |
| Vulkan Extensions | 184 of 184 enabled |
| Rendering Mode | Gmem |
| Vulkan Version | 1.3 |
| GPU Name | Device |
| Max Device Memory | 8192 |
| Driver Present Mode | mailbox |
| Sync Frame | Off |
| Disable Present Wait | Off |
| Resource Type | auto |
| BCN Emulation | auto |
| BCN Emulation Type | compute |

## Compatibility
| Setting | Value |
|---|---|
| DX Wrapper | DXVK+VKD3D |
| DXVK Version | 3.1.1-1-gplasync-arm64ec |
| VKD3D Version | 3.0.1-gamesir |
| VKD3D Feature Level | 12_1 |
| Frame Rate | 60 |
| Max Frame Latency | Off |
| Async | On |
| Async Cache | On |
| DDraw Wrapper | none |
| 32-bit Emulator | FEXCore |
| FEXCore Version | 2611 |

## In-game
| Setting | Value |
|---|---|
| Graphics | Medium |
| DirectX 12 | Off |
| Frame generation | Off |

With DirectX 12 off, the game renders through DXVK; the VKD3D settings apply only
to DirectX 12.

## Environment variables
```
WRAPPER_MAX_IMAGE_COUNT=0
ZINK_DESCRIPTORS=lazy
ZINK_DEBUG=compact
MESA_SHADER_CACHE_DISABLE=false
MESA_SHADER_CACHE_MAX_SIZE=512MB
mesa_glthread=true
WINEESYNC=1
FD_MESA_DEBUG=hiprio,sysmem
VKD3D_SHADER_MODEL=6_6
MESA_GL_VERSION_OVERRIDE=3.3
TU_DEBUG=gmem
```

## Advanced
| Setting | Value |
|---|---|
| Startup Selection | Essential (load only essential services) |
| Sync CPU Topology | On |
| Processor Affinity | All visible buttons selected (CPU0-CPU3 and part of the next); rest cut off |
| Builtin Decoder | GStreamer |
| Software decoding | Off |
| Sharpness Effect | None |

## Still to record
- In-game resolution and upscaling
- FPS in this app alone (the 60 frame rate setting needs checking against
  the 70-100 FPS reported across three apps)
