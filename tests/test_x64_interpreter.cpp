#include "rottr/x64_runtime.h"

#include <array>
#include <cstdint>
#include <cstdio>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}
}

int main() {
    // mov rax, 7; mov rcx, 5; add rax, rcx; push rax; xor rax, rax;
    // pop rdx; cmp rdx, 12; jne fail; hlt; fail: hlt
    constexpr std::array<uint8_t, 36> code = {
        0x48,0xb8,7,0,0,0,0,0,0,0,
        0x48,0xb9,5,0,0,0,0,0,0,0,
        0x48,0x01,0xc8,
        0x50,
        0x48,0x31,0xc0,
        0x5a,
        0x48,0x83,0xfa,0x0c,
        0x75,0x01,0xf4,0xf4
    };
    std::array<uint8_t, 256> memory{};
    for (size_t i = 0; i < code.size(); ++i) memory[i] = code[i];
    rottr_cpu cpu{};
    rottr_cpu_reset(&cpu, 0x1000, 0x1080);
    const rottr::X64GuestMemory guest{0x1000, memory.data(), memory.size(), true, true};
    auto result = rottr::runX64Interpreter(cpu, guest, 64);
    if (!expect(result.reason == rottr::X64StopReason::Halted,
                "sign-extended immediate CMP and conditional branch must execute") ||
        !expect(cpu.gpr[2] == 12, "PUSH/POP must preserve the computed value") ||
        !expect(cpu.gpr[0] == 0, "XOR must clear RAX") ||
        !expect((cpu.rflags & (1ULL << 6)) != 0, "CMP must set ZF for equal operands") ||
        !expect(cpu.rip == 0x1023, "JNE must not branch when CMP operands are equal") ||
        !expect(rottr::hasX64InstructionInterpreter(), "interpreter capability must be reported")) {
        return 1;
    }

    // A second tiny program verifies a successful execution/HLT path.
    constexpr uint8_t simple[] = {0x48,0xb8,0x2a,0,0,0,0,0,0,0,0xf4};
    memory.fill(0);
    for (size_t i = 0; i < sizeof(simple); ++i) memory[i] = simple[i];
    rottr_cpu_reset(&cpu, 0x1000, 0x1080);
    result = rottr::runX64Interpreter(cpu, guest, 8);
    if (!expect(result.reason == rottr::X64StopReason::Halted, "HLT must stop cleanly") ||
        !expect(cpu.gpr[0] == 42, "MOV r64, imm64 must set RAX") ||
        !expect(result.instructionsExecuted == 2, "instruction count must be exact")) {
        return 1;
    }

    // CALL rel32 into a local function; RET must return to the caller, not
    // terminate the interpreter; the caller then executes HLT.
    constexpr uint8_t callProgram[] = {
        0xe8,0x01,0x00,0x00,0x00,0xf4,
        0x48,0xb8,0x2a,0,0,0,0,0,0,0,0xc3
    };
    memory.fill(0);
    for (size_t i = 0; i < sizeof(callProgram); ++i) memory[i] = callProgram[i];
    rottr_cpu_reset(&cpu, 0x1000, 0x1080);
    result = rottr::runX64Interpreter(cpu, guest, 8);
    if (!expect(result.reason == rottr::X64StopReason::Halted,
                "nested RET must return to caller") ||
        !expect(cpu.gpr[0] == 42, "called guest function must execute") ||
        !expect(cpu.gpr[4] == 0x1080, "CALL/RET must balance guest stack")) {
        return 1;
    }

    std::puts("OK");
    return 0;
}
