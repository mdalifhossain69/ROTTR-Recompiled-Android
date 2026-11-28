#include "rottr/x64_runtime.h"
#include "rottr/x64_decoder.h"
#include "x64_test_runner.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}
}

int main() {
    std::array<uint8_t, 32> decoderBytes{};
    rottr::X64GuestMemory decoderMemory{
            0x2000, decoderBytes.data(), decoderBytes.size(), true, true};
    rottr::Decoder decoder(decoderMemory, UINT64_MAX);
    uint8_t decodedByte = 0;
    if (!expect(!decoder.byte(decodedByte),
                "decoder must reject an address whose offset overflows guest bounds"))
        return 1;

    rottr::X64GuestMemory nonExecutableMemory{
            0x2000, decoderBytes.data(), decoderBytes.size(), true, false};
    rottr::Decoder nonExecutableDecoder(nonExecutableMemory, 0x2000);
    if (!expect(!nonExecutableDecoder.byte(decodedByte),
                "decoder must reject a non-executable guest region"))
        return 1;

    std::fill(decoderBytes.begin(), decoderBytes.end(), 0x65);
    rottr::Decoder maximumLengthDecoder(decoderMemory, 0x2000);
    for (size_t i = 0; i < rottr::kMaximumInstructionLength - 1U; ++i) {
        if (!expect(maximumLengthDecoder.byte(decodedByte),
                    "decoder must consume bytes up to the maximum instruction length"))
            return 1;
    }
    uint16_t oversizedImmediate = 0;
    if (!expect(!maximumLengthDecoder.immediate(oversizedImmediate),
                "decoder must reject an immediate that exceeds the 15-byte instruction limit"))
        return 1;

    std::array<uint8_t, 1> danglingPrefixBytes{0x66};
    rottr::X64GuestMemory danglingPrefixMemory{
            0x2800, danglingPrefixBytes.data(), danglingPrefixBytes.size(),
            true, true};
    rottr_cpu danglingPrefixCpu{};
    rottr_cpu_reset(&danglingPrefixCpu, danglingPrefixMemory.base,
                    danglingPrefixMemory.base);
    auto danglingPrefixResult = rottr::runX64Interpreter(
            danglingPrefixCpu, danglingPrefixMemory, 1);
    if (!expect(danglingPrefixResult.reason ==
                        rottr::X64StopReason::InvalidMemory &&
                    danglingPrefixResult.faultAddress ==
                            danglingPrefixMemory.base + 1,
                "truncated legacy prefix must report an instruction-fetch fault"))
        return 1;
    danglingPrefixBytes[0] = 0x48;
    rottr_cpu_reset(&danglingPrefixCpu, danglingPrefixMemory.base,
                    danglingPrefixMemory.base);
    danglingPrefixResult = rottr::runX64Interpreter(
            danglingPrefixCpu, danglingPrefixMemory, 1);
    if (!expect(danglingPrefixResult.reason ==
                        rottr::X64StopReason::InvalidMemory &&
                    danglingPrefixResult.faultAddress ==
                            danglingPrefixMemory.base + 1,
                "truncated REX prefix must report an instruction-fetch fault"))
        return 1;

    std::array<uint8_t, 258> guardedMemory{};
    std::array<uint8_t, 256> randomProgram{};
    constexpr uint8_t kGuard = 0xa5;
    guardedMemory.front() = kGuard;
    guardedMemory.back() = kGuard;
    rottr::X64GuestMemory randomGuest{
            0x3000, guardedMemory.data() + 1U, randomProgram.size(), true, true};
    uint32_t randomState = 0x7f4a7c15U;
    rottr_cpu randomCpu{};
    for (size_t trial = 0; trial < 256; ++trial) {
        for (uint8_t& byte : randomProgram) {
            randomState ^= randomState << 13U;
            randomState ^= randomState >> 17U;
            randomState ^= randomState << 5U;
            byte = static_cast<uint8_t>(randomState);
        }
        std::memcpy(guardedMemory.data() + 1U, randomProgram.data(),
                    randomProgram.size());
        rottr_cpu_reset(&randomCpu, randomGuest.base, randomGuest.base + 0xf0U);
        (void)rottr::runX64Interpreter(randomCpu, randomGuest, 16);
        if (!expect(guardedMemory.front() == kGuard &&
                    guardedMemory.back() == kGuard,
                    "random instruction streams must stay inside guest memory"))
            return 1;
    }

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
    auto result = runX64ForTest(cpu, guest, 64);
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
    result = runX64ForTest(cpu, guest, 8);
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
    result = runX64ForTest(cpu, guest, 8);
    if (!expect(result.reason == rottr::X64StopReason::Halted,
                "nested RET must return to caller") ||
        !expect(cpu.gpr[0] == 42, "called guest function must execute") ||
        !expect(cpu.gpr[4] == 0x1080, "CALL/RET must balance guest stack")) {
        return 1;
    }

    // Memory MOV plus ModRM/SIB addressing: [RBX], [RBX + RSI*4 + 8].
    constexpr uint8_t memoryProgram[] = {
        0x48,0xbb,0xd0,0x10,0,0,0,0,0,0, // mov rbx, 0x10d0
        0x48,0xb9,5,0,0,0,0,0,0,0,       // mov rcx, 5
        0x48,0x89,0x0b,                   // mov [rbx], rcx
        0x48,0x01,0x0b,                   // add [rbx], rcx
        0x48,0xbe,2,0,0,0,0,0,0,0,       // mov rsi, 2
        0x48,0x01,0x4c,0xb3,0x08,         // add [rbx+rsi*4+8], rcx
        0x48,0x8b,0x13,                   // mov rdx, [rbx]
        0x48,0x8b,0x54,0xb3,0x08,         // mov rdx, [rbx+rsi*4+8]
        0xf4
    };
    memory.fill(0);
    for (size_t i = 0; i < sizeof(memoryProgram); ++i) memory[i] = memoryProgram[i];
    rottr_cpu_reset(&cpu, 0x1000, 0x1080);
    result = runX64ForTest(cpu, guest, 32);
    uint64_t plainMemory = 0;
    uint64_t indexedMemory = 0;
    std::memcpy(&plainMemory, memory.data() + 0xd0, sizeof(plainMemory));
    std::memcpy(&indexedMemory, memory.data() + 0xe0, sizeof(indexedMemory));
    if (!expect(result.reason == rottr::X64StopReason::Halted,
                "memory MOV and arithmetic must reach HLT") ||
        !expect(plainMemory == 10, "base-addressed memory arithmetic must store 10") ||
        !expect(indexedMemory == 5, "SIB scale/index/displacement must address correct memory") ||
        !expect(cpu.gpr[2] == 5, "SIB memory load must populate RDX")) {
        return 1;
    }

    // Near JZ (0F 84 rel32) must take the branch after equal CMP.
    constexpr uint8_t nearBranchProgram[] = {
        0x48,0xb8,5,0,0,0,0,0,0,0,
        0x48,0xb9,5,0,0,0,0,0,0,0,
        0x48,0x39,0xc8,
        0x0f,0x84,0x01,0,0,0,
        0xf4,0xf4
    };
    memory.fill(0);
    for (size_t i = 0; i < sizeof(nearBranchProgram); ++i) memory[i] = nearBranchProgram[i];
    rottr_cpu_reset(&cpu, 0x1000, 0x1080);
    result = runX64ForTest(cpu, guest, 32);
    if (!expect(result.reason == rottr::X64StopReason::Halted,
                "near conditional branch must reach HLT") ||
        !expect(cpu.rip == 0x101f, "near JZ must branch to the second HLT")) {
        return 1;
    }

    // Register OR, AND, and TEST exercise logical result and flag behavior.
    constexpr uint8_t logicalProgram[] = {
        0x48,0xb8,0xf0,0,0,0,0,0,0,0,
        0x48,0xb9,0x0f,0,0,0,0,0,0,0,
        0x48,0x09,0xc8, // or rax, rcx => ff
        0x48,0x21,0xc8, // and rax, rcx => 0f
        0x48,0x85,0xc8, // test rax, rcx => nonzero
        0x48,0x31,0xc9, // xor rcx, rcx
        0x48,0x85,0xc8, // test rax, rcx => zero
        0xf4
    };
    memory.fill(0);
    for (size_t i = 0; i < sizeof(logicalProgram); ++i) memory[i] = logicalProgram[i];
    rottr_cpu_reset(&cpu, 0x1000, 0x1080);
    result = runX64ForTest(cpu, guest, 32);
    if (!expect(result.reason == rottr::X64StopReason::Halted,
                "logical instructions must reach HLT") ||
        !expect(cpu.gpr[0] == 0x0f, "OR and AND must produce the expected RAX") ||
        !expect((cpu.rflags & (1ULL << 6)) != 0,
                "TEST with zero intersection must set ZF")) {
        return 1;
    }


    // Group-1 immediate arithmetic supports memory destinations and CMP no-writeback.
    constexpr uint8_t immediateMemoryProgram[] = {
        0x48,0xbb,0xd0,0x10,0,0,0,0,0,0,
        0x48,0xb9,5,0,0,0,0,0,0,0,
        0x48,0x89,0x0b,
        0x48,0x83,0x03,0x03,
        0x48,0x83,0x3b,0x08,
        0x74,0x01,0xf4,0xf4
    };
    memory.fill(0);
    for (size_t i = 0; i < sizeof(immediateMemoryProgram); ++i)
        memory[i] = immediateMemoryProgram[i];
    rottr_cpu_reset(&cpu, 0x1000, 0x1080);
    result = runX64ForTest(cpu, guest, 32);
    uint64_t immediateMemoryValue = 0;
    std::memcpy(&immediateMemoryValue, memory.data() + 0xd0,
                sizeof(immediateMemoryValue));
    if (!expect(result.reason == rottr::X64StopReason::Halted,
                "memory immediate ADD/CMP must reach HLT") ||
        !expect(immediateMemoryValue == 8,
                "memory immediate ADD must write the updated value") ||
        !expect((cpu.rflags & (1ULL << 6)) != 0,
                "memory immediate CMP must set ZF without changing memory") ||
        !expect(cpu.rip == 0x1023,
                "memory immediate CMP must drive the following conditional branch")) {
        return 1;
    }

    // An immediate operation must reject an out-of-range memory destination.
    constexpr uint8_t outOfBoundsImmediateProgram[] = {
        0x48,0xbb,0xfc,0x10,0,0,0,0,0,0,
        0x48,0x83,0x03,0x01,
        0xf4
    };
    memory.fill(0);
    for (size_t i = 0; i < sizeof(outOfBoundsImmediateProgram); ++i)
        memory[i] = outOfBoundsImmediateProgram[i];
    rottr_cpu_reset(&cpu, 0x1000, 0x1080);
    result = runX64ForTest(cpu, guest, 16);
    if (!expect(result.reason == rottr::X64StopReason::InvalidMemory,
                "out-of-bounds immediate memory operand must fault")) {
        return 1;
    }


    // 64-bit immediate shifts: SHL, SHR and arithmetic SAR.
    constexpr uint8_t shiftProgram[] = {
        0x48,0xb8,3,0,0,0,0,0,0,0,
        0x48,0xc1,0xe0,2,
        0x48,0xb9,0x10,0,0,0,0,0,0,0,
        0x48,0xc1,0xe9,2,
        0x48,0xba,0xf8,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
        0x48,0xc1,0xfa,1,
        0xf4
    };
    memory.fill(0);
    for (size_t i = 0; i < sizeof(shiftProgram); ++i) memory[i] = shiftProgram[i];
    rottr_cpu_reset(&cpu, 0x1000, 0x1080);
    result = runX64ForTest(cpu, guest, 32);
    if (!expect(result.reason == rottr::X64StopReason::Halted,
                "64-bit shift instructions must reach HLT") ||
        !expect(cpu.gpr[0] == 12, "SHL must shift the destination left") ||
        !expect(cpu.gpr[1] == 4, "SHR must shift in zero bits") ||
        !expect(cpu.gpr[2] == ~uint64_t{3},
                "SAR must preserve the negative sign bit")) {
        return 1;
    }

    // A zero shift count leaves both the operand and arithmetic flags unchanged.
    constexpr uint8_t zeroShiftProgram[] = {
        0x48,0xb8,0x34,0x12,0,0,0,0,0,0,
        0x48,0xc1,0xe0,0,
        0xf4
    };
    memory.fill(0);
    for (size_t i = 0; i < sizeof(zeroShiftProgram); ++i)
        memory[i] = zeroShiftProgram[i];
    rottr_cpu_reset(&cpu, 0x1000, 0x1080);
    cpu.rflags |= (1ULL << 0);
    result = runX64ForTest(cpu, guest, 8);
    if (!expect(result.reason == rottr::X64StopReason::Halted,
                "zero-count shift must reach HLT") ||
        !expect(cpu.gpr[0] == 0x1234, "zero-count shift must preserve its operand") ||
        !expect((cpu.rflags & (1ULL << 0)) != 0,
                "zero-count shift must preserve carry flag")) {
        return 1;
    }


    // MOVZX/MOVSX and MOVSXD cover byte, word and dword source widths.
    constexpr uint8_t extensionProgram[] = {
        0x48,0xbb,0xd0,0x10,0,0,0,0,0,0,
        0x48,0x0f,0xb6,0x03,             // movzx rax, byte [rbx]
        0x48,0x0f,0xbe,0x0b,             // movsx rcx, byte [rbx]
        0x48,0x0f,0xb7,0x53,0x02,         // movzx rdx, word [rbx+2]
        0x48,0x63,0x73,0x04,              // movsxd rsi, dword [rbx+4]
        0xf4
    };
    memory.fill(0);
    memory[0xd0] = 0x80;
    memory[0xd2] = 0xff;
    memory[0xd3] = 0x80;
    memory[0xd4] = 0x01;
    memory[0xd5] = 0x00;
    memory[0xd6] = 0x00;
    memory[0xd7] = 0x80;
    for (size_t i = 0; i < sizeof(extensionProgram); ++i)
        memory[i] = extensionProgram[i];
    rottr_cpu_reset(&cpu, 0x1000, 0x1080);
    result = runX64ForTest(cpu, guest, 32);
    if (!expect(result.reason == rottr::X64StopReason::Halted,
                "MOVZX/MOVSX/MOVSXD must reach HLT") ||
        !expect(cpu.gpr[0] == 0x80, "MOVZX byte must zero extend") ||
        !expect(cpu.gpr[1] == ~uint64_t{0} - 127,
                "MOVSX byte must sign extend -128") ||
        !expect(cpu.gpr[2] == 0x80ff, "MOVZX word must zero extend") ||
        !expect(cpu.gpr[6] == 0xffffffff80000001ULL,
                "MOVSXD must sign extend a dword")) {
        return 1;
    }

    // A 32-bit MOV destination zero-extends into its full 64-bit register.
    constexpr uint8_t zeroExtend32Program[] = {
        0xb8,0x78,0x56,0x34,0x12,
        0xf4
    };
    memory.fill(0);
    for (size_t i = 0; i < sizeof(zeroExtend32Program); ++i)
        memory[i] = zeroExtend32Program[i];
    rottr_cpu_reset(&cpu, 0x1000, 0x1080);
    cpu.gpr[0] = 0xffffffffffffffffULL;
    result = runX64ForTest(cpu, guest, 8);
    if (!expect(result.reason == rottr::X64StopReason::Halted,
                "32-bit MOV must reach HLT") ||
        !expect(cpu.gpr[0] == 0x12345678,
                "writing a 32-bit register must zero the upper 32 bits")) {
        return 1;
    }


    // 8/16-bit arithmetic preserves unaffected register bits; 32-bit writes clear upper bits.
    constexpr uint8_t partialWidthProgram[] = {
        0x66,0x01,0xc8, // add ax, cx
        0x40,0x00,0xc8, // add al, cl (REX makes SPL/BPL/... byte names available)
        0xf4
    };
    memory.fill(0);
    for (size_t i = 0; i < sizeof(partialWidthProgram); ++i)
        memory[i] = partialWidthProgram[i];
    rottr_cpu_reset(&cpu, 0x1000, 0x1080);
    cpu.gpr[0] = 0x1122334455660001ULL;
    cpu.gpr[1] = 2;
    result = runX64ForTest(cpu, guest, 8);
    if (!expect(result.reason == rottr::X64StopReason::Halted,
                "16-bit and 8-bit arithmetic must reach HLT") ||
        !expect(cpu.gpr[0] == 0x1122334455660005ULL,
                "16-bit and 8-bit writes must preserve upper register bits")) {
        return 1;
    }

    constexpr uint8_t dwordArithmeticProgram[] = {
        0x01,0xd8, // add eax, ebx
        0xf4
    };
    memory.fill(0);
    for (size_t i = 0; i < sizeof(dwordArithmeticProgram); ++i)
        memory[i] = dwordArithmeticProgram[i];
    rottr_cpu_reset(&cpu, 0x1000, 0x1080);
    cpu.gpr[0] = 0xffffffffffffffffULL;
    cpu.gpr[3] = 2;
    result = runX64ForTest(cpu, guest, 8);
    if (!expect(result.reason == rottr::X64StopReason::Halted,
                "32-bit arithmetic must reach HLT") ||
        !expect(cpu.gpr[0] == 1,
                "32-bit arithmetic result must zero the upper register half")) {
        return 1;
    }

    // Byte arithmetic also operates on memory without touching adjacent bytes.
    constexpr uint8_t byteMemoryProgram[] = {
        0x40,0x00,0x0b, // add byte [rbx], cl
        0xf4
    };
    memory.fill(0);
    for (size_t i = 0; i < sizeof(byteMemoryProgram); ++i)
        memory[i] = byteMemoryProgram[i];
    memory[0xd0] = 0xfe;
    memory[0xd1] = 0x7a;
    rottr_cpu_reset(&cpu, 0x1000, 0x1080);
    cpu.gpr[3] = 0x10d0;
    cpu.gpr[1] = 1;
    result = runX64ForTest(cpu, guest, 8);
    if (!expect(result.reason == rottr::X64StopReason::Halted,
                "byte memory arithmetic must reach HLT") ||
        !expect(memory[0xd0] == 0xff && memory[0xd1] == 0x7a,
                "byte memory arithmetic must preserve adjacent bytes")) {
        return 1;
    }

    // String instructions: rep movsb copies bytes from RSI to RDI.
    constexpr uint8_t movsbProgram[] = {
        0x48,0xbe,0x40,0x10,0,0,0,0,0,0, // mov rsi, 0x1040
        0x48,0xbf,0x80,0x10,0,0,0,0,0,0, // mov rdi, 0x1080
        0x48,0xb9,8,0,0,0,0,0,0,0,      // mov rcx, 8
        0xf3,0xa4,                    // rep movsb
        0xf4                          // hlt
    };
    memory.fill(0);
    for (size_t i = 0; i < sizeof(movsbProgram); ++i) memory[i] = movsbProgram[i];
    for (size_t i = 0; i < 8; ++i) memory[0x40 + i] = static_cast<uint8_t>(i + 1);
    rottr_cpu_reset(&cpu, 0x1000, 0x1080);
    result = runX64ForTest(cpu, guest, 64);
    if (!expect(result.reason == rottr::X64StopReason::Halted,
                "rep movsb must reach HLT") ||
        !expect(cpu.gpr[6] == 0x1048, "rep movsb must advance RSI") ||
        !expect(cpu.gpr[7] == 0x1088, "rep movsb must advance RDI") ||
        !expect(cpu.gpr[1] == 0, "rep movsb must zero RCX")) {
        return 1;
    }
    for (size_t i = 0; i < 8; ++i) {
        if (!expect(memory[0x80 + i] == static_cast<uint8_t>(i + 1),
                    "rep movsb must copy source data to destination")) {
            return 1;
        }
    }

    // stosb with REP fills memory with AL.
    constexpr uint8_t stosbProgram[] = {
        0x48,0xbf,0x80,0x10,0,0,0,0,0,0, // mov rdi, 0x1080
        0x48,0xb9,8,0,0,0,0,0,0,0,      // mov rcx, 8
        0xb0,0x5a,                      // mov al, 0x5a
        0xf3,0xaa,                      // rep stosb
        0xf4                          // hlt
    };
    memory.fill(0);
    for (size_t i = 0; i < sizeof(stosbProgram); ++i) memory[i] = stosbProgram[i];
    rottr_cpu_reset(&cpu, 0x1000, 0x1080);
    result = runX64ForTest(cpu, guest, 64);
    if (!expect(result.reason == rottr::X64StopReason::Halted,
                "rep stosb must reach HLT") ||
        !expect(cpu.gpr[7] == 0x1088, "rep stosb must advance RDI") ||
        !expect(cpu.gpr[1] == 0, "rep stosb must zero RCX")) {
        return 1;
    }
    for (size_t i = 0; i < 8; ++i) {
        if (!expect(memory[0x80 + i] == 0x5a,
                    "rep stosb must fill destination with AL")) {
            return 1;
        }
    }

    // lodsb loads a byte from RSI into AL and advances RSI.
    constexpr uint8_t lodsbProgram[] = {
        0x48,0xbe,0x40,0x10,0,0,0,0,0,0, // mov rsi, 0x1040
        0xac,                          // lodsb
        0xf4                          // hlt
    };
    memory.fill(0);
    for (size_t i = 0; i < sizeof(lodsbProgram); ++i) memory[i] = lodsbProgram[i];
    memory[0x40] = 0xab;
    rottr_cpu_reset(&cpu, 0x1000, 0x1080);
    result = runX64ForTest(cpu, guest, 8);
    if (!expect(result.reason == rottr::X64StopReason::Halted,
                "lodsb must reach HLT") ||
        !expect(cpu.gpr[0] == 0xab, "lodsb must load byte into AL") ||
        !expect(cpu.gpr[6] == 0x1041, "lodsb must advance RSI")) {
        return 1;
    }

    // scasb with REPNE scans for a byte equal to AL; stops when found.
    constexpr uint8_t scasbProgram[] = {
        0x48,0xbf,0x40,0x10,0,0,0,0,0,0, // mov rdi, 0x1040
        0x48,0xb9,4,0,0,0,0,0,0,0,      // mov rcx, 4
        0xb0,0x03,                      // mov al, 3
        0xf2,0xae,                      // repne scasb
        0xf4                          // hlt
    };
    memory.fill(0);
    for (size_t i = 0; i < sizeof(scasbProgram); ++i) memory[i] = scasbProgram[i];
    memory[0x40] = 1;  // [0x1040] = 1
    memory[0x41] = 2;  // [0x1041] = 2
    memory[0x42] = 3;  // [0x1042] = 3 (target)
    memory[0x43] = 4;  // [0x1043] = 4
    rottr_cpu_reset(&cpu, 0x1000, 0x1080);
    result = runX64ForTest(cpu, guest, 16);
    if (!expect(result.reason == rottr::X64StopReason::Halted,
                "repne scasb must reach HLT") ||
        !expect(cpu.gpr[7] == 0x1043, "repne scasb must point RDI past found byte") ||
        !expect(cpu.gpr[1] == 1, "repne scasb must decrement RCX for each comparison")) {
        return 1;
    }

    // A string instruction without REP executes once and preserves RCX.
    constexpr uint8_t movsbNoRepProgram[] = {
        0x48,0xbe,0x40,0x10,0,0,0,0,0,0,
        0x48,0xbf,0x80,0x10,0,0,0,0,0,0,
        0x48,0xb9,2,0,0,0,0,0,0,0,
        0xa4,0xf4
    };
    memory.fill(0);
    std::memcpy(memory.data(), movsbNoRepProgram, sizeof(movsbNoRepProgram));
    memory[0x40] = 0xaa;
    memory[0x80] = 0xcc;
    memory[0x81] = 0xdd;
    rottr_cpu_reset(&cpu, 0x1000, 0x1080);
    result = runX64ForTest(cpu, guest, 16);
    if (!expect(result.reason == rottr::X64StopReason::Halted,
                "MOVSB without REP must reach HLT") ||
        !expect(cpu.gpr[1] == 2, "MOVSB without REP must preserve RCX") ||
        !expect(cpu.gpr[6] == 0x1041 && cpu.gpr[7] == 0x1081,
                "MOVSB without REP must advance RSI and RDI once") ||
        !expect(memory[0x80] == 0xaa && memory[0x81] == 0xdd,
                "MOVSB without REP must copy exactly one byte")) {
        return 1;
    }

    // REP with RCX=0 performs no memory access and leaves the indices unchanged.
    constexpr uint8_t movsbZeroCountProgram[] = {
        0x48,0xbe,0x40,0x10,0,0,0,0,0,0,
        0x48,0xbf,0x80,0x10,0,0,0,0,0,0,
        0xf3,0xa4,0xf4
    };
    memory.fill(0);
    std::memcpy(memory.data(), movsbZeroCountProgram, sizeof(movsbZeroCountProgram));
    memory[0x40] = 0xaa;
    memory[0x80] = 0xcc;
    rottr_cpu_reset(&cpu, 0x1000, 0x1080);
    result = runX64ForTest(cpu, guest, 16);
    if (!expect(result.reason == rottr::X64StopReason::Halted,
                "zero-count REP MOVSB must reach HLT") ||
        !expect(cpu.gpr[1] == 0 && cpu.gpr[6] == 0x1040 && cpu.gpr[7] == 0x1080,
                "zero-count REP MOVSB must preserve RCX, RSI and RDI") ||
        !expect(memory[0x80] == 0xcc,
                "zero-count REP MOVSB must not access the destination")) {
        return 1;
    }

    // REP MOVSW/MOVSD/MOVSQ use their architectural element sizes.
    constexpr uint8_t movsWidthProgram[] = {
        0x48,0xbe,0xc0,0x10,0,0,0,0,0,0,
        0x48,0xbf,0xd0,0x10,0,0,0,0,0,0,
        0x48,0xb9,1,0,0,0,0,0,0,0,
        0x66,0xf3,0xa5,
        0x48,0xbe,0xc4,0x10,0,0,0,0,0,0,
        0x48,0xbf,0xd4,0x10,0,0,0,0,0,0,
        0x48,0xb9,1,0,0,0,0,0,0,0,
        0xf3,0xa5,
        0x48,0xbe,0xc8,0x10,0,0,0,0,0,0,
        0x48,0xbf,0xd8,0x10,0,0,0,0,0,0,
        0x48,0xb9,1,0,0,0,0,0,0,0,
        0xf3,0x48,0xa5,
        0xf4
    };
    memory.fill(0);
    std::memcpy(memory.data(), movsWidthProgram, sizeof(movsWidthProgram));
    for (size_t i = 0; i < 8; ++i)
        memory[0xc0 + i] = static_cast<uint8_t>(0x10 + i);
    rottr_cpu_reset(&cpu, 0x1000, 0x1080);
    result = runX64ForTest(cpu, guest, 64);
    if (!expect(result.reason == rottr::X64StopReason::Halted,
                "REP MOVSW/MOVSD/MOVSQ must reach HLT") ||
    !expect(std::memcmp(memory.data() + 0xd0, memory.data() + 0xc0, 2) == 0 &&
            std::memcmp(memory.data() + 0xd4, memory.data() + 0xc4, 4) == 0 &&
            std::memcmp(memory.data() + 0xd8, memory.data() + 0xc8, 8) == 0,
                "REP MOVSW/MOVSD/MOVSQ must copy 2/4/8-byte elements")) {
        return 1;
    }

    auto executeString = [&](uint8_t opcode, size_t width, uint8_t repeat,
                         uint64_t count, uint64_t accumulator) {
    std::vector<uint8_t> code;
    auto emit = [&code](uint8_t byte) { code.push_back(byte); };
    auto emitImmediate = [&code](uint8_t opcodeByte, uint64_t value) {
        code.push_back(0x48);
        code.push_back(opcodeByte);
        for (unsigned byte = 0; byte < 8; ++byte)
            code.push_back(static_cast<uint8_t>(value >> (byte * 8U)));
    };
    emitImmediate(0xbe, 0x10c0);  // RSI
    emitImmediate(0xbf, 0x10d0);  // RDI
    emitImmediate(0xb9, count);   // RCX
    emitImmediate(0xb8, accumulator);  // RAX
    if (width == 2) emit(0x66);
    if (repeat != 0) emit(repeat);
    if (width == 8) emit(0x48);
    emit(opcode);
    emit(0xf4);
    std::memcpy(memory.data(), code.data(), code.size());
    rottr_cpu_reset(&cpu, 0x1000, 0x1080);
    return runX64ForTest(cpu, guest, 64);
    };
    auto writePattern = [&memory](size_t offset, size_t width, uint64_t value) {
    for (size_t byte = 0; byte < width; ++byte)
        memory[offset + byte] = static_cast<uint8_t>(value >> (byte * 8U));
    };
    for (size_t width : {1U, 2U, 4U, 8U}) {
    const uint8_t movsOpcode = width == 1 ? 0xa4 : 0xa5;
    const uint8_t cmpsOpcode = width == 1 ? 0xa6 : 0xa7;
    const uint8_t scasOpcode = width == 1 ? 0xae : 0xaf;
    const uint8_t lodsOpcode = width == 1 ? 0xac : 0xad;
    const uint8_t stosOpcode = width == 1 ? 0xaa : 0xab;

    memory.fill(0);
    writePattern(0xc0, width, 0x11);
    writePattern(0xc0 + width, width, 0x22);
    result = executeString(movsOpcode, width, 0xf3, 2, 0);
    if (!expect(result.reason == rottr::X64StopReason::Halted &&
                cpu.gpr[1] == 0 && cpu.gpr[6] == 0x10c0 + 2 * width &&
                cpu.gpr[7] == 0x10d0 + 2 * width,
                "REP MOVS must consume RCX and advance indices by element width") ||
        !expect(std::memcmp(memory.data() + 0xd0, memory.data() + 0xc0,
                            2 * width) == 0,
                "REP MOVS must copy each supported element width"))
        return 1;

    memory.fill(0);
    writePattern(0xc0, width, 0x11);
    writePattern(0xc0 + width, width, 0x22);
    writePattern(0xd0, width, 0x11);
    writePattern(0xd0 + width, width, 0x23);
    result = executeString(cmpsOpcode, width, 0xf3, 3, 0);
    if (!expect(result.reason == rottr::X64StopReason::Halted &&
                cpu.gpr[1] == 1 && cpu.gpr[6] == 0x10c0 + 2 * width &&
                cpu.gpr[7] == 0x10d0 + 2 * width &&
                (cpu.rflags & (1ULL << 6)) == 0,
                "REPE CMPS must stop at the first mismatch"))
        return 1;

    memory.fill(0);
    writePattern(0xc0, width, 0x11);
    writePattern(0xc0 + width, width, 0x22);
    writePattern(0xd0, width, 0x23);
    writePattern(0xd0 + width, width, 0x22);
    result = executeString(cmpsOpcode, width, 0xf2, 3, 0);
    if (!expect(result.reason == rottr::X64StopReason::Halted &&
                cpu.gpr[1] == 1 && cpu.gpr[6] == 0x10c0 + 2 * width &&
                cpu.gpr[7] == 0x10d0 + 2 * width &&
                (cpu.rflags & (1ULL << 6)) != 0,
                "REPNE CMPS must stop at the first match"))
        return 1;

    memory.fill(0);
    writePattern(0xd0, width, 0x11);
    writePattern(0xd0 + width, width, 0x42);
    result = executeString(scasOpcode, width, 0xf2, 3, 0x42);
    if (!expect(result.reason == rottr::X64StopReason::Halted &&
                cpu.gpr[1] == 1 && cpu.gpr[7] == 0x10d0 + 2 * width &&
                (cpu.rflags & (1ULL << 6)) != 0,
                "REPNE SCAS must stop when the accumulator matches"))
        return 1;

    memory.fill(0);
    writePattern(0xd0, width, 0x11);
    writePattern(0xd0 + width, width, 0x42);
    result = executeString(scasOpcode, width, 0xf3, 3, 0x42);
    if (!expect(result.reason == rottr::X64StopReason::Halted &&
                cpu.gpr[1] == 2 && cpu.gpr[7] == 0x10d0 + width &&
                (cpu.rflags & (1ULL << 6)) == 0,
                "REPE SCAS must stop at the first mismatch"))
        return 1;

    memory.fill(0);
    writePattern(0xc0, width, 0x11);
    writePattern(0xc0 + width, width, 0x22);
    result = executeString(lodsOpcode, width, 0xf3, 2, 0);
    if (!expect(result.reason == rottr::X64StopReason::Halted &&
                cpu.gpr[0] == 0x22 && cpu.gpr[1] == 0 &&
                cpu.gpr[6] == 0x10c0 + 2 * width,
                "REP LODS must load the final element at each supported width"))
        return 1;

    memory.fill(0);
    result = executeString(stosOpcode, width, 0xf3, 2, 0x42);
    if (!expect(result.reason == rottr::X64StopReason::Halted &&
                cpu.gpr[1] == 0 && cpu.gpr[7] == 0x10d0 + 2 * width,
                "REP STOS must advance by each supported element width"))
        return 1;
    uint8_t expected[16] = {};
    for (size_t i = 0; i < 2 * width; ++i)
        expected[i] = static_cast<uint8_t>(
                uint64_t{0x42} >> ((i % width) * 8U));
    const bool storesCorrectBytes =
            std::memcmp(memory.data() + 0xd0, expected, 2 * width) == 0;
    if (!expect(storesCorrectBytes,
                "REP STOS must write the accumulator at each supported width")) {
        std::fprintf(stderr, "  width=%zu actual=", width);
        for (size_t i = 0; i < 2 * width; ++i)
            std::fprintf(stderr, "%02x", memory[0xd0 + i]);
        std::fprintf(stderr, " expected=");
        for (size_t i = 0; i < 2 * width; ++i)
            std::fprintf(stderr, "%02x", expected[i]);
        std::fprintf(stderr, "\n");
        return 1;
    }
    }

    std::puts("OK");
    return 0;
}
