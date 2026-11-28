#pragma once

#include "rottr/arm64_translator_runtime.h"
#include "rottr/x64_runtime.h"

#include <cstdlib>

// CPU tests use this indirection so the same programs can be run through the
// interpreter and through the AArch64 translation dispatcher in CI.
inline rottr::X64RunResult runX64ForTest(rottr_cpu& cpu,
                                         const rottr::X64GuestMemory& memory,
                                         uint64_t instructionBudget) {
    const char* translated = std::getenv("ROTTR_TRANSLATED_TESTS");
    if (translated != nullptr && translated[0] != '\0' &&
        rottr::arm64::hasArm64TranslationBackend()) {
        return rottr::arm64::runX64Translated(cpu, memory, instructionBudget);
    }
    return rottr::runX64Interpreter(cpu, memory, instructionBudget);
}
