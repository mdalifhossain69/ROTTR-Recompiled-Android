#include "graphics/dxgi_vk_format.h"

#include <array>
#include <limits>

namespace rottr::gfx {
namespace {

// VkFormat numeric values from the Vulkan core registry. Keep this table
// independent of vulkan.h so format conversion can be host-unit-tested.
constexpr std::array<FormatInfo, 23> kFormats{{
    {DxgiFormat::R32G32B32A32Float, 109, 1, 1, 16, false, false, false},
    {DxgiFormat::R32G32B32Float, 106, 1, 1, 12, false, false, false},
    {DxgiFormat::R16G16B16A16Float, 97, 1, 1, 8, false, false, false},
    {DxgiFormat::R32G32Float, 103, 1, 1, 8, false, false, false},
    {DxgiFormat::R16G16Float, 83, 1, 1, 4, false, false, false},
    {DxgiFormat::R32Float, 100, 1, 1, 4, false, false, false},
    {DxgiFormat::D32Float, 126, 1, 1, 4, false, false, true},
    {DxgiFormat::D24UnormS8Uint, 129, 1, 1, 4, false, false, true},
    {DxgiFormat::R8G8Unorm, 16, 1, 1, 2, false, false, false},
    {DxgiFormat::R16Float, 76, 1, 1, 2, false, false, false},
    {DxgiFormat::R8Unorm, 9, 1, 1, 1, false, false, false},
    {DxgiFormat::R8G8B8A8Unorm, 37, 1, 1, 4, false, false, false},
    {DxgiFormat::R8G8B8A8UnormSrgb, 43, 1, 1, 4, false, true, false},
    {DxgiFormat::B8G8R8A8Unorm, 44, 1, 1, 4, false, false, false},
    {DxgiFormat::B8G8R8A8UnormSrgb, 50, 1, 1, 4, false, true, false},
    {DxgiFormat::BC1Unorm, 133, 4, 4, 8, true, false, false},
    {DxgiFormat::BC1UnormSrgb, 134, 4, 4, 8, true, true, false},
    {DxgiFormat::BC2Unorm, 135, 4, 4, 16, true, false, false},
    {DxgiFormat::BC2UnormSrgb, 136, 4, 4, 16, true, true, false},
    {DxgiFormat::BC3Unorm, 137, 4, 4, 16, true, false, false},
    {DxgiFormat::BC3UnormSrgb, 138, 4, 4, 16, true, true, false},
    {DxgiFormat::BC5Unorm, 141, 4, 4, 16, true, false, false},
    {DxgiFormat::BC7Unorm, 145, 4, 4, 16, true, false, false},
}};

constexpr FormatInfo kBc7Srgb{DxgiFormat::BC7UnormSrgb, 146, 4, 4, 16, true, true, false};

uint32_t ceilDiv(uint32_t value, uint32_t divisor) {
    return value / divisor + (value % divisor != 0U ? 1U : 0U);
}

uint32_t checkedMul(uint32_t a, uint32_t b) {
    if (a != 0U && b > std::numeric_limits<uint32_t>::max() / a) return 0U;
    return a * b;
}

}  // namespace

const FormatInfo* findFormat(DxgiFormat format) {
    for (const auto& info : kFormats) {
        if (info.dxgi == format) return &info;
    }
    return format == DxgiFormat::BC7UnormSrgb ? &kBc7Srgb : nullptr;
}

bool isFormatSupported(DxgiFormat format) {
    return findFormat(format) != nullptr;
}

uint32_t bytesPerBlock(DxgiFormat format) {
    const auto* info = findFormat(format);
    return info == nullptr ? 0U : info->bytesPerBlock;
}

uint32_t rowPitch(DxgiFormat format, uint32_t width) {
    const auto* info = findFormat(format);
    if (info == nullptr || width == 0U) return 0U;
    const uint32_t blocks = ceilDiv(width, info->blockWidth);
    return checkedMul(blocks, info->bytesPerBlock);
}

uint32_t imageByteSize(DxgiFormat format, uint32_t width, uint32_t height) {
    const auto* info = findFormat(format);
    if (info == nullptr || width == 0U || height == 0U) return 0U;
    const uint32_t rows = ceilDiv(height, info->blockHeight);
    return checkedMul(rowPitch(format, width), rows);
}

}  // namespace rottr::gfx
