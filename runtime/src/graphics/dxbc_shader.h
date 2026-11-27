#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rottr::gfx::shader {

enum class Stage : uint8_t { Vertex, Pixel, Geometry, Hull, Domain, Compute, Unknown };

struct DxbcInfo {
    Stage stage = Stage::Unknown;
    uint8_t major = 0;
    uint8_t minor = 0;
    uint32_t tokenCount = 0;
    size_t shaderChunkOffset = 0;
    size_t shaderChunkSize = 0;
};

enum class CompileStatus : uint8_t {
    Success,
    InvalidContainer,
    UnsupportedShaderStage,
    CompilerUnavailable,
    TranslationFailed,
    InvalidSpirv
};

struct CompileResult {
    CompileStatus status = CompileStatus::InvalidContainer;
    Stage stage = Stage::Unknown;
    std::vector<uint32_t> spirv;
    std::string diagnostic;
};

// Validates the DXBC container and extracts the shader model/stage from SHDR/SHEX.
// All offsets and sizes are checked before any bytecode is dereferenced.
bool inspectDxbc(const void* data, size_t size, DxbcInfo& out, std::string* error = nullptr);

// Converts a D3D11 DXBC shader to Vulkan SPIR-V using vkd3d-shader when that
// optional compiler library is linked into the Android runtime.
CompileResult compileDxbcToSpirv(const void* data, size_t size);
bool hasDxbcSpirvCompiler();

} // namespace rottr::gfx::shader
