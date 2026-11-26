// graphics_types.h
// Type definitions for the Graphics Subsystem (Direct3D 11 → Vulkan translation layer)

#pragma once

#include <cstdint>
#include "video/video_types.h"

// --- Windows basic types ---
typedef int32_t BOOL;
typedef uint32_t UINT;
typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef int32_t INT;
typedef int8_t INT8;
typedef int16_t INT16;
typedef int32_t INT32;
typedef int64_t INT64;
typedef void* HANDLE;
typedef HANDLE HWND;

// --- Video Types (must be before swap chain) ---
enum class DXGI_FORMAT {
    UNKNOWN = 0,
    R32G32B32A32_TYPELESS = 1,
    R32G32B32A32_FLOAT = 2,
    R24G8_TYPELESS = 26,
    D24_UNORM_S8_UINT = 27,
    B8G8R8A8_TYPELESS = 28,
    B8G8R8A8_UNORM = 29,
    // ... more formats
};

// --- Core Types ---
enum class D3D11_FILTER {
    MIN_MAG_MIP_POINT = 0x1,
    MIN_MAG_POINT_MAG_FILTERPOINT = 0x2,
    MIN_POINT_MAG_MAGFILTER = 0x3,
    ANISOTROPIC = 0x5,
    // ... more filters
};

enum class D3D11_COMPARISON_FUNC {
    NEVER = 1,
    LESS = 2,
    EQUAL = 3,
    LESS_EQUAL = 4,
    GREATER = 5,
    NOT_EQUAL = 6,
    GREATER_EQUAL = 7,
    ALWAYS = 8,
};

// --- Texture Types ---
enum class D3D11_TEXTURE_ADDRESS_MODE {
    WRAP = 1,
    MIRROR = 2,
    CLAMP = 3,
    BORDER = 4,
    MIRROR_ONCE = 5,
};

// --- Depth Stencil Description ---
enum class D3D11_DEPTH_WRITE_MASK {
    ZERO = 0,
    ALL = 1,
};

enum class D3D11_STENCIL_OP {
    KEEP = 1,
    ZERO = 2,
    REPLACE = 3,
    INCR_SAT = 4,
    DECR_SAT = 5,
    INVERT = 6,
    INCR = 7,
    DECR = 8,
};

struct D3D11_DEPTH_STENCIL_DESC {
    BOOL DepthEnable;
    D3D11_DEPTH_WRITE_MASK DepthWriteMask;
    D3D11_COMPARISON_FUNC DepthFunc;
    BOOL StencilEnable;
    UINT8 StencilReadMask;
    UINT8 StencilWriteMask;
    D3D11_STENCIL_OP StencilFailOp;
    D3D11_STENCIL_OP StencilDepthFailOp;
    D3D11_STENCIL_OP StencilPassOp;
};

// --- Swap Chain Description ---
enum class DXGI_USAGE {
    RENDER_TARGET_OUTPUT = 0x1,
    SHADER_INPUT = 0x10,
    BACK_BUFFER = 0x20,
    SHARED = 0x40,
    READ_ONLY = 0x80,
    DISCARD_ON_PRESENT = 0x100,
    UNORDERED_ACCESS = 0x200,
};

enum class DXGI_SCALING {
    STRETCH = 0,
    NONE = 1,
    ASPECT_RATIO_STRETCH = 2,
};

enum class DXGI_SWAP_EFFECT {
    DISCARD = 0,
    SEQUENTIAL = 1,
    FLIP_SEQUENTIAL = 2,
    FLIP_DISCARD = 3,
};

struct DXGI_SWAP_CHAIN_DESC1 {
    UINT Width;
    UINT Height;
    DXGI_FORMAT Format;
    UINT SampleDescCount;
    UINT SampleQuality;
    DXGI_USAGE BufferUsage;
    UINT BufferCount;
    DXGI_SCALING Scaling;
    DXGI_SWAP_EFFECT SwapEffect;
    UINT AlphaFilter;
    UINT AlphaMode;
};

// --- Buffer Description ---
enum class D3D11_USAGE {
    DEFAULT = 0,
    IMMUTABLE = 1,
    DYNAMIC = 2,
    STAGING = 3,
};

struct D3D11_BUFFER_DESC {
    UINT ByteWidth;
    D3D11_USAGE Usage;
    UINT CPUAccessFlags;
    UINT MiscFlags;
    UINT StructureByteStride;
};

// --- Resource Description ---
enum class D3D11_RESOURCE_DIMENSION_TYPE {
    UNKNOWN = 0,
    BUFFER = 1,
    TEXTURE1D = 2,
    TEXTURE2D = 3,
    TEXTURE3D = 4,
};

struct D3D11_RESOURCE_DIMENSION {
    D3D11_RESOURCE_DIMENSION_TYPE Dimension;
    UINT MiscFlags;
    // Union removed for simplicity - would need forward declarations
};

// --- Function Declarations ---
// Initialize graphics subsystem (called once at startup)
extern "C" bool graphics_init(int width, int height, HWND hwnd);

// Shutdown graphics subsystem (called once at exit)
extern "C" void graphics_shutdown(void);

// Present the rendered frame to screen
extern "C" bool graphics_present(void);

// Render a video frame from YUV data
extern "C" bool graphics_render_frame(const VideoSample& sample);

// Get the current back buffer texture handle
extern "C" void* graphics_get_back_buffer(void);

// Get graphics adapter info
extern "C" void graphics_get_adapter_info(int& adapter_id, char* name, int max_name_len);