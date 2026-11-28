// Roadmap step 48: basic block translator with interpreter fallback.
// Pass: translated block state matches the interpreter after every block.
#pragma once

#include "rottr/x64_runtime.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rottr {
namespace arm64 {

struct TranslationResult {
    enum class Status { Translated, Unsupported, InvalidMemory };
    Status status = Status::Unsupported;
    std::vector<uint32_t> code;
    uint64_t instructionCount = 0;
    uint64_t startRip = 0;
    uint64_t endRip = 0;
    std::string detail;
};

TranslationResult translateBasicBlock(const X64GuestMemory& memory,
                                      uint64_t rip,
                                      size_t maxInstructions = 32);

}  // namespace arm64
}  // namespace rottr