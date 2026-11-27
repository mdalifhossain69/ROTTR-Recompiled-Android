#pragma once

#include "rottr/guest.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace rottr {

enum class X64StopReason : uint8_t {
    Halted, Returned, InstructionBudgetExceeded, InvalidMemory,
    ReadOnlyMemory, UnsupportedInstruction, InvalidCpuState
};

struct X64GuestMemory {
    uint64_t base = 0;
    uint8_t* data = nullptr;
    size_t size = 0;
    bool writable = false;
    bool executable = false;
};

struct X64RunResult {
    X64StopReason reason = X64StopReason::InvalidCpuState;
    uint64_t instructionsExecuted = 0;
    uint64_t faultAddress = 0;
    std::string detail;
};

// Limited, bounds-checked x86-64 interpreter for controlled guest-code tests.
// Unsupported encodings stop execution; they are never silently treated as NOPs.
X64RunResult runX64Interpreter(rottr_cpu& cpu, const X64GuestMemory& memory,
                               uint64_t instructionBudget);
bool hasX64InstructionInterpreter();

// A CPU interpreter alone is not a complete Windows x86-64 execution runtime.
bool hasWindowsX64ExecutionBackend();
std::string describeWindowsX64ExecutionBackend();

}  // namespace rottr
