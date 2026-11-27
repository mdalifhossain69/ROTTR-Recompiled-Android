#include "graphics/dxgi_vk_format.h"

#include <cassert>
#include <cstdint>
#include <limits>

using rottr::gfx::DxgiFormat;

int main() {
    using namespace rottr::gfx;
    assert(findFormat(DxgiFormat::R8G8B8A8Unorm)->vkFormat == 37U);
    assert(findFormat(DxgiFormat::BC1Unorm)->vkFormat == 133U);
    assert(findFormat(DxgiFormat::BC7UnormSrgb)->vkFormat == 146U);
    assert(findFormat(DxgiFormat::D24UnormS8Uint)->depthStencil);
    assert(rowPitch(DxgiFormat::R8G8B8A8Unorm, 1920) == 7680U);
    assert(rowPitch(DxgiFormat::BC1Unorm, 5) == 16U);
    assert(imageByteSize(DxgiFormat::BC1Unorm, 5, 5) == 32U);
    assert(imageByteSize(DxgiFormat::R16G16B16A16Float, 4, 3) == 96U);
    assert(rowPitch(DxgiFormat::Unknown, 10) == 0U);
    assert(imageByteSize(DxgiFormat::BC3Unorm, std::numeric_limits<uint32_t>::max(), 4) == 0U);
    return 0;
}
