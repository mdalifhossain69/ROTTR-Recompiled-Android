#include "graphics/dxbc_shader.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace rottr::gfx::shader;

static void put32(std::vector<uint8_t>& b, size_t p, uint32_t v) {
    b[p] = static_cast<uint8_t>(v);
    b[p + 1] = static_cast<uint8_t>(v >> 8U);
    b[p + 2] = static_cast<uint8_t>(v >> 16U);
    b[p + 3] = static_cast<uint8_t>(v >> 24U);
}

static std::vector<uint8_t> makeDxbc(uint32_t programType = 1U) {
    // One SHDR chunk containing a minimal two-token shader program header.
    std::vector<uint8_t> b(52U, 0);
    std::memcpy(b.data(), "DXBC", 4);
    put32(b, 24, static_cast<uint32_t>(b.size()));
    put32(b, 28, 1);
    put32(b, 32, 36);
    std::memcpy(b.data() + 36, "SHDR", 4);
    put32(b, 40, 8);
    put32(b, 44, (programType << 16U) | (5U << 4U));
    // The minimal shader token stream consists of version and length tokens.
    put32(b, 48, 2);
    return b;
}

int main() {
    DxbcInfo info{};
    std::string error;
    auto valid = makeDxbc();
    assert(inspectDxbc(valid.data(), valid.size(), info, &error));
    assert(info.stage == Stage::Vertex);
    assert(info.major == 5 && info.minor == 0);
    assert(info.tokenCount == 2);
    assert(info.shaderChunkOffset == 44);
    assert(compileDxbcToSpirv(valid.data(), valid.size()).status ==
           (hasDxbcSpirvCompiler() ? CompileStatus::Success : CompileStatus::CompilerUnavailable));

    auto badMagic = valid;
    badMagic[0] = 'X';
    assert(!inspectDxbc(badMagic.data(), badMagic.size(), info));

    auto badSize = valid;
    put32(badSize, 24, 0xffffffffU);
    assert(!inspectDxbc(badSize.data(), badSize.size(), info));

    auto badChunk = valid;
    put32(badChunk, 40, 0xfffffff0U);
    assert(!inspectDxbc(badChunk.data(), badChunk.size(), info));

    auto badTokens = valid;
    put32(badTokens, 48, 99U);
    assert(!inspectDxbc(badTokens.data(), badTokens.size(), info));

    auto unknownStage = makeDxbc(15U);
    assert(!inspectDxbc(unknownStage.data(), unknownStage.size(), info));
    return 0;
}
