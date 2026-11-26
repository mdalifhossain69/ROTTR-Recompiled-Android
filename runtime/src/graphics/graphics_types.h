// graphics_types.h
// Type definitions for the Graphics Subsystem (Direct3D 11 → Vulkan translation layer)

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
    UINT8 StencilReadMask;  // (duplicate for clarity)
};

// --- Swap Chain Description ---
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
struct D3D11_BUFFER_DESC {
    UINT ByteWidth;
    D3D11_USAGE Usage;
    UINT CPUAccessFlags;
    UINT MiscFlags;
    UINT StructureByteStride;
};

// --- Resource Description ---
struct D3D11_RESOURCE_DIMENSION {
    D3D11_RESOURCE_DIMENSION Dimension;
    D3D11_RESOURCE_DIMENSION_FLAG MiscFlags;
    union {
        D3D11_TEX1D_DESC Tex1D;
        D3D11_TEX2D_DESC Tex2D;
        D3D11_TEX3D_DESC Tex3D;
    };
};

// --- Video Types ---
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

// --- Video Sample Structure ---
struct VideoSample {
    uint64_t timestamp;
    uint8_t* data;
    uint32_t width;
    uint32_t height;
    DXGI_FORMAT format;
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
