#include "rottr/arm64_translator_runtime.h"
#include "rottr/arm64_code_buffer.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace rottr {
namespace arm64 {

bool hasArm64TranslationBackend() {
#if defined(__aarch64__)
    return true;
#else
    return false;
#endif
}

X64RunResult runX64Translated(rottr_cpu& cpu, const X64GuestMemory& memory,
                              uint64_t instructionBudget) {
    X64RunResult result;
    result.reason = X64StopReason::InstructionBudgetExceeded;
    uint64_t executed = 0;
#if defined(__aarch64__)
    while (executed < instructionBudget) {
        const size_t maxInstructions = static_cast<size_t>(
            std::min<uint64_t>(32, instructionBudget - executed));
        TranslationResult block = translateBasicBlock(memory, cpu.rip, maxInstructions);
        if (block.status == TranslationResult::Status::Translated && !block.code.empty()) {
            ExecutableMemory native(block.code.size() * sizeof(uint32_t));
            if (!native.valid() ||
                !native.writeWords(0, block.code.data(), block.code.size()) ||
                !native.seal()) {
                result.reason = X64StopReason::InvalidCpuState;
                result.detail = "cannot allocate translated code buffer";
                result.instructionsExecuted = executed;
                return result;
            }
            using BlockFn = void (*)(rottr_cpu*);
            BlockFn fn = reinterpret_cast<BlockFn>(native.entry());
            fn(&cpu);
            executed += block.instructionCount;
            continue;
        }

        X64RunResult step = runX64Interpreter(cpu, memory, 1);
        executed += step.instructionsExecuted;
        const bool interpreterStopped =
            step.instructionsExecuted == 0 ||
            step.reason == X64StopReason::Halted ||
            step.reason == X64StopReason::Returned ||
            step.reason == X64StopReason::InvalidMemory ||
            step.reason == X64StopReason::ReadOnlyMemory ||
            step.reason == X64StopReason::UnsupportedInstruction ||
            (step.reason == X64StopReason::InvalidCpuState && step.instructionsExecuted == 0);
        if (interpreterStopped) {
            result.reason = step.reason;
            result.detail = std::move(step.detail);
            result.faultAddress = step.faultAddress;
            result.instructionsExecuted = executed;
            return result;
        }
    }
#else
    result = runX64Interpreter(cpu, memory, instructionBudget);
    result.detail = "host is not AArch64; translated blocks were not executed";
#endif
    result.instructionsExecuted = executed;
    return result;
}

}  // namespace arm64
}  // namespace rottr