#pragma once

#include "rottr/arm64_block_translator.h"
#include "rottr/x64_runtime.h"

namespace rottr {
namespace arm64 {

// Executes translated blocks when the host is AArch64 and falls back to the
// verified interpreter for instructions the block translator does not cover.
X64RunResult runX64Translated(rottr_cpu& cpu, const X64GuestMemory& memory,
                              uint64_t instructionBudget);

bool hasArm64TranslationBackend();

}  // namespace arm64
}  // namespace rottr