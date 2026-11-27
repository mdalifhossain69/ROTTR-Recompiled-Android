#include "graphics/dxbc_shader.h"

#include <cstring>
#include <limits>

#if defined(ROTTR_HAS_VKD3D_SHADER)
#include <vkd3d_shader.h>
#endif

namespace rottr::gfx::shader {
namespace {
uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8U) |
           (static_cast<uint32_t>(p[2]) << 16U) |
           (static_cast<uint32_t>(p[3]) << 24U);
}

bool fail(std::string* error, const char* message) {
    if (error) *error = message;
    return false;
}

Stage decodeStage(uint32_t token) {
    switch ((token >> 16U) & 0xffffU) {
    case 0: return Stage::Pixel;
    case 1: return Stage::Vertex;
    case 2: return Stage::Geometry;
    case 3: return Stage::Hull;
    case 4: return Stage::Domain;
    case 5: return Stage::Compute;
    default: return Stage::Unknown;
    }
}
} // namespace

bool inspectDxbc(const void* data, size_t size, DxbcInfo& out, std::string* error) {
    out = {};
    if (!data || size < 32U) return fail(error, "DXBC header is truncated");
    const auto* bytes = static_cast<const uint8_t*>(data);
    if (std::memcmp(bytes, "DXBC", 4) != 0) return fail(error, "DXBC magic is missing");

    const uint32_t declaredSize = readU32(bytes + 24U);
    const uint32_t chunkCount = readU32(bytes + 28U);
    if (declaredSize < 32U || declaredSize > size)
        return fail(error, "DXBC declared size is outside the supplied buffer");
    if (chunkCount == 0U || chunkCount > (declaredSize - 32U) / 4U)
        return fail(error, "DXBC chunk count is invalid");
    const size_t offsetsEnd = 32U + static_cast<size_t>(chunkCount) * 4U;
    if (offsetsEnd > declaredSize) return fail(error, "DXBC chunk-offset table is truncated");

    bool found = false;
    DxbcInfo parsed{};
    for (uint32_t i = 0; i < chunkCount; ++i) {
        const uint32_t offset = readU32(bytes + 32U + static_cast<size_t>(i) * 4U);
        if (offset < offsetsEnd || offset > declaredSize || declaredSize - offset < 8U)
            return fail(error, "DXBC chunk offset is invalid");
        const uint8_t* chunk = bytes + offset;
        const uint32_t chunkSize = readU32(chunk + 4U);
        if (chunkSize > declaredSize - offset - 8U)
            return fail(error, "DXBC chunk extends beyond the container");
        const bool shaderChunk = std::memcmp(chunk, "SHDR", 4) == 0 ||
                                 std::memcmp(chunk, "SHEX", 4) == 0;
        if (!shaderChunk) continue;
        if (found) return fail(error, "DXBC contains multiple shader program chunks");
        if (chunkSize < 8U || (chunkSize & 3U) != 0U)
            return fail(error, "DXBC shader token stream has an invalid size");
        const uint32_t version = readU32(chunk + 8U);
        const uint32_t tokenCount = readU32(chunk + 12U);
        if (tokenCount < 2U || static_cast<uint64_t>(tokenCount) * 4U != chunkSize)
            return fail(error, "DXBC shader token count does not match its chunk");
        parsed.stage = decodeStage(version);
        parsed.major = static_cast<uint8_t>((version >> 4U) & 0xfU);
        parsed.minor = static_cast<uint8_t>(version & 0xfU);
        parsed.tokenCount = tokenCount;
        parsed.shaderChunkOffset = static_cast<size_t>(offset) + 8U;
        parsed.shaderChunkSize = chunkSize;
        if (parsed.stage == Stage::Unknown) return fail(error, "DXBC shader stage is unsupported");
        if (parsed.major < 4U || parsed.major > 5U)
            return fail(error, "Only shader model 4.x and 5.x bytecode is supported");
        found = true;
    }
    if (!found) return fail(error, "DXBC container has no SHDR/SHEX shader program chunk");
    out = parsed;
    if (error) error->clear();
    return true;
}

bool hasDxbcSpirvCompiler() {
#if defined(ROTTR_HAS_VKD3D_SHADER)
    return true;
#else
    return false;
#endif
}

CompileResult compileDxbcToSpirv(const void* data, size_t size) {
    CompileResult result;
    DxbcInfo info{};
    if (!inspectDxbc(data, size, info, &result.diagnostic)) {
        result.status = CompileStatus::InvalidContainer;
        return result;
    }
    result.stage = info.stage;
#if defined(ROTTR_HAS_VKD3D_SHADER)
    struct vkd3d_shader_code input{};
    input.code = data;
    input.size = size;
    struct vkd3d_shader_code output{};
    struct vkd3d_shader_compile_arguments arguments{};
    arguments.target = VKD3D_SHADER_TARGET_SPIRV_VULKAN_1_0;
    const int rc = vkd3d_shader_compile_dxbc(&input, &output, nullptr, 0U, nullptr, &arguments);
    if (rc < 0 || !output.code || output.size == 0U || (output.size & 3U) != 0U) {
        if (output.code) vkd3d_shader_free_shader_code(&output);
        result.status = CompileStatus::TranslationFailed;
        result.diagnostic = "vkd3d-shader could not translate this DXBC program";
        return result;
    }
    const auto* words = static_cast<const uint32_t*>(output.code);
    const size_t wordCount = output.size / sizeof(uint32_t);
    if (wordCount < 5U || words[0] != 0x07230203U) {
        vkd3d_shader_free_shader_code(&output);
        result.status = CompileStatus::InvalidSpirv;
        result.diagnostic = "DXBC compiler returned an invalid SPIR-V module";
        return result;
    }
    result.spirv.assign(words, words + wordCount);
    vkd3d_shader_free_shader_code(&output);
    result.status = CompileStatus::Success;
    result.diagnostic.clear();
#else
    result.status = CompileStatus::CompilerUnavailable;
    result.diagnostic = "DXBC parsing is available, but vkd3d-shader is not linked into this build";
#endif
    return result;
}

} // namespace rottr::gfx::shader
