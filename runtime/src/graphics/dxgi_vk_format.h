#pragma once

#include <cstdint>

namespace rottr::gfx {

// Numeric values intentionally match DXGI_FORMAT and VkFormat so the
// translation table can be tested on host builds without Vulkan headers.
enum class DxgiFormat : uint32_t {
    Unknown = 0,
    R32G32B32A32Float = 2,
    R32G32B32Float = 6,
    R16G16B16A16Float = 10,
    R32G32Float = 16,
    R16G16Float = 34,
    R32Float = 41,
    D32Float = 40,
    D24UnormS8Uint = 45,
    R8G8Unorm = 49,
    R16Float = 54,
    R8Unorm = 61,
    R8G8B8A8Unorm = 28,
    R8G8B8A8UnormSrgb = 29,
    B8G8R8A8Unorm = 87,
    B8G8R8A8UnormSrgb = 91,
    BC1Unorm = 71,
    BC1UnormSrgb = 72,
    BC2Unorm = 74,
    BC2UnormSrgb = 75,
    BC3Unorm = 77,
    BC3UnormSrgb = 78,
    BC5Unorm = 83,
    BC7Unorm = 98,
    BC7UnormSrgb = 99,
};

struct FormatInfo {
    DxgiFormat dxgi;
    uint32_t vkFormat;
    uint8_t blockWidth;
    uint8_t blockHeight;
    uint8_t bytesPerBlock;
    bool compressed;
    bool srgb;
    bool depthStencil;
};

const FormatInfo* findFormat(DxgiFormat format);
bool isFormatSupported(DxgiFormat format);
uint32_t bytesPerBlock(DxgiFormat format);
uint32_t rowPitch(DxgiFormat format, uint32_t width);
uint32_t imageByteSize(DxgiFormat format, uint32_t width, uint32_t height);

}  // namespace rottr::gfx
