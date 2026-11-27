#include "rottr/x64_runtime.h"

#include <array>
#include <cstring>
#include <limits>

namespace rottr {
namespace {

constexpr uint64_t kFlagCarry = 1ULL << 0;
constexpr uint64_t kFlagParity = 1ULL << 2;
constexpr uint64_t kFlagAuxiliary = 1ULL << 4;
constexpr uint64_t kFlagZero = 1ULL << 6;
constexpr uint64_t kFlagSign = 1ULL << 7;
constexpr uint64_t kFlagOverflow = 1ULL << 11;
constexpr uint64_t kArithmeticFlags = kFlagCarry | kFlagParity | kFlagAuxiliary |
                                      kFlagZero | kFlagSign | kFlagOverflow;
constexpr uint64_t kReservedFlag = 1ULL << 1;
constexpr size_t kMaximumInstructionLength = 15;

struct Failure {
    X64StopReason reason;
    uint64_t address;
    const char* message;
};

bool contains(const X64GuestMemory& memory, uint64_t address, size_t length) {
    if (memory.data == nullptr || address < memory.base) return false;
    const uint64_t offset = address - memory.base;
    return offset <= memory.size && length <= memory.size - static_cast<size_t>(offset);
}

uint8_t read8(const X64GuestMemory& memory, uint64_t address) {
    return memory.data[static_cast<size_t>(address - memory.base)];
}

bool fetch(const X64GuestMemory& memory, uint64_t address, void* out, size_t size) {
    if (!memory.executable || !contains(memory, address, size)) return false;
    std::memcpy(out, memory.data + static_cast<size_t>(address - memory.base), size);
    return true;
}

bool readGuest(const X64GuestMemory& memory, uint64_t address, void* out, size_t size) {
    if (!contains(memory, address, size)) return false;
    std::memcpy(out, memory.data + static_cast<size_t>(address - memory.base), size);
    return true;
}

bool writeGuest(const X64GuestMemory& memory, uint64_t address,
                const void* value, size_t size) {
    if (!memory.writable || !contains(memory, address, size)) return false;
    std::memcpy(memory.data + static_cast<size_t>(address - memory.base), value, size);
    return true;
}

class Decoder {
public:
    Decoder(const X64GuestMemory& memory, uint64_t ip) : memory_(memory), start_(ip), cursor_(ip) {}

    bool byte(uint8_t& value) {
        if (cursor_ - start_ >= kMaximumInstructionLength ||
            !fetch(memory_, cursor_, &value, sizeof(value))) return false;
        ++cursor_;
        return true;
    }

    template <typename T>
    bool immediate(T& value) {
        if (cursor_ - start_ > kMaximumInstructionLength ||
            sizeof(T) > kMaximumInstructionLength - (cursor_ - start_) ||
            !fetch(memory_, cursor_, &value, sizeof(T))) return false;
        cursor_ += sizeof(T);
        return true;
    }

    uint64_t next() const { return cursor_; }
    uint64_t length() const { return cursor_ - start_; }

private:
    const X64GuestMemory& memory_;
    uint64_t start_;
    uint64_t cursor_;
};

uint64_t& reg(rottr_cpu& cpu, unsigned index) { return cpu.gpr[index & 15U]; }

bool evenParity(uint8_t value) {
    value ^= static_cast<uint8_t>(value >> 4);
    value &= 0x0f;
    return ((0x9669U >> value) & 1U) != 0;
}

void setLogicFlags(rottr_cpu& cpu, uint64_t result) {
    cpu.rflags &= ~kArithmeticFlags;
    if (result == 0) cpu.rflags |= kFlagZero;
    if ((result >> 63) != 0) cpu.rflags |= kFlagSign;
    if (evenParity(static_cast<uint8_t>(result))) cpu.rflags |= kFlagParity;
}

uint64_t add64(rottr_cpu& cpu, uint64_t left, uint64_t right) {
    const uint64_t result = left + right;
    cpu.rflags &= ~kArithmeticFlags;
    if (result < left) cpu.rflags |= kFlagCarry;
    if (((left ^ right ^ result) & 0x10U) != 0) cpu.rflags |= kFlagAuxiliary;
    if (((~(left ^ right) & (left ^ result)) >> 63) != 0) cpu.rflags |= kFlagOverflow;
    if (result == 0) cpu.rflags |= kFlagZero;
    if ((result >> 63) != 0) cpu.rflags |= kFlagSign;
    if (evenParity(static_cast<uint8_t>(result))) cpu.rflags |= kFlagParity;
    return result;
}

uint64_t sub64(rottr_cpu& cpu, uint64_t left, uint64_t right) {
    const uint64_t result = left - right;
    cpu.rflags &= ~kArithmeticFlags;
    if (left < right) cpu.rflags |= kFlagCarry;
    if (((left ^ right ^ result) & 0x10U) != 0) cpu.rflags |= kFlagAuxiliary;
    if ((((left ^ right) & (left ^ result)) >> 63) != 0) cpu.rflags |= kFlagOverflow;
    if (result == 0) cpu.rflags |= kFlagZero;
    if ((result >> 63) != 0) cpu.rflags |= kFlagSign;
    if (evenParity(static_cast<uint8_t>(result))) cpu.rflags |= kFlagParity;
    return result;
}

X64RunResult stop(X64StopReason reason, uint64_t count, uint64_t address,
                  const char* message) {
    return {reason, count, address, message};
}

}  // namespace

bool hasX64InstructionInterpreter() { return true; }

X64RunResult runX64Interpreter(rottr_cpu& cpu, const X64GuestMemory& memory,
                               uint64_t instructionBudget) {
    if (memory.data == nullptr || memory.size == 0 || !memory.executable ||
        !contains(memory, cpu.rip, 1) || instructionBudget == 0) {
        return stop(X64StopReason::InvalidCpuState, 0, cpu.rip,
                    "invalid guest memory, instruction pointer, or instruction budget");
    }

    uint64_t executed = 0;
    while (executed < instructionBudget) {
        const uint64_t instructionAddress = cpu.rip;
        Decoder decoder(memory, instructionAddress);
        uint8_t rex = 0;
        uint8_t opcode = 0;
        if (!decoder.byte(opcode))
            return stop(X64StopReason::InvalidMemory, executed, instructionAddress,
                        "instruction fetch is outside executable guest memory");

        if (opcode >= 0x40 && opcode <= 0x4f) {
            rex = opcode;
            if (!decoder.byte(opcode))
                return stop(X64StopReason::InvalidMemory, executed, instructionAddress,
                            "truncated instruction after REX prefix");
        }

        const bool rexW = (rex & 0x08U) != 0;
        const unsigned rexR = (rex >> 2U) & 1U;
        const unsigned rexB = rex & 1U;

        if (opcode == 0x90) {
            cpu.rip = decoder.next();
        } else if (opcode == 0xf4) {
            cpu.rip = decoder.next();
            ++executed;
            return stop(X64StopReason::Halted, executed, cpu.rip, "HLT reached");
        } else if (opcode == 0xc3) {
            uint64_t target = 0;
            if (!readGuest(memory, cpu.gpr[4], &target, sizeof(target)))
                return stop(X64StopReason::InvalidMemory, executed, cpu.gpr[4],
                            "RET stack read is outside guest memory");
            cpu.gpr[4] += sizeof(target);
            cpu.rip = target;
            ++executed;
            return stop(X64StopReason::Returned, executed, target, "RET reached");
        } else if (opcode >= 0xb8 && opcode <= 0xbf) {
            const unsigned destination = (opcode - 0xb8U) | (rexB << 3U);
            if (rexW) {
                uint64_t immediate = 0;
                if (!decoder.immediate(immediate))
                    return stop(X64StopReason::InvalidMemory, executed, instructionAddress,
                                "truncated MOV r64, imm64");
                reg(cpu, destination) = immediate;
            } else {
                uint32_t immediate = 0;
                if (!decoder.immediate(immediate))
                    return stop(X64StopReason::InvalidMemory, executed, instructionAddress,
                                "truncated MOV r32, imm32");
                reg(cpu, destination) = immediate;
            }
            cpu.rip = decoder.next();
        } else if (opcode >= 0x50 && opcode <= 0x5f) {
            const unsigned index = (opcode & 7U) | (rexB << 3U);
            if (opcode < 0x58) {
                const uint64_t newStack = cpu.gpr[4] - sizeof(uint64_t);
                const uint64_t value = reg(cpu, index);
                if (!writeGuest(memory, newStack, &value, sizeof(value)))
                    return stop(memory.writable ? X64StopReason::InvalidMemory :
                                                  X64StopReason::ReadOnlyMemory,
                                executed, newStack, "PUSH stack write failed");
                cpu.gpr[4] = newStack;
            } else {
                uint64_t value = 0;
                if (!readGuest(memory, cpu.gpr[4], &value, sizeof(value)))
                    return stop(X64StopReason::InvalidMemory, executed, cpu.gpr[4],
                                "POP stack read failed");
                reg(cpu, index) = value;
                cpu.gpr[4] += sizeof(uint64_t);
            }
            cpu.rip = decoder.next();
        } else if (opcode == 0xe8 || opcode == 0xe9) {
            int32_t displacement = 0;
            if (!decoder.immediate(displacement))
                return stop(X64StopReason::InvalidMemory, executed, instructionAddress,
                            "truncated relative CALL/JMP");
            const uint64_t target = decoder.next() + static_cast<int64_t>(displacement);
            if (opcode == 0xe8) {
                const uint64_t returnAddress = decoder.next();
                const uint64_t newStack = cpu.gpr[4] - sizeof(uint64_t);
                if (!writeGuest(memory, newStack, &returnAddress, sizeof(returnAddress)))
                    return stop(memory.writable ? X64StopReason::InvalidMemory :
                                                  X64StopReason::ReadOnlyMemory,
                                executed, newStack, "CALL stack write failed");
                cpu.gpr[4] = newStack;
            }
            cpu.rip = target;
        } else if (opcode == 0xeb || opcode == 0x74 || opcode == 0x75) {
            int8_t displacement = 0;
            if (!decoder.immediate(displacement))
                return stop(X64StopReason::InvalidMemory, executed, instructionAddress,
                            "truncated short branch");
            const bool take = opcode == 0xeb ||
                    (opcode == 0x74 && (cpu.rflags & kFlagZero) != 0) ||
                    (opcode == 0x75 && (cpu.rflags & kFlagZero) == 0);
            cpu.rip = take ? decoder.next() + static_cast<int64_t>(displacement) :
                             decoder.next();
        } else if (opcode == 0x01 || opcode == 0x03 || opcode == 0x29 ||
                   opcode == 0x2b || opcode == 0x31 || opcode == 0x33 ||
                   opcode == 0x39 || opcode == 0x3b) {
            if (!rexW)
                return stop(X64StopReason::UnsupportedInstruction, executed, instructionAddress,
                            "only 64-bit register arithmetic encodings are supported");
            uint8_t modrm = 0;
            if (!decoder.byte(modrm))
                return stop(X64StopReason::InvalidMemory, executed, instructionAddress,
                            "truncated arithmetic ModRM");
            const unsigned mod = modrm >> 6U;
            if (mod != 3)
                return stop(X64StopReason::UnsupportedInstruction, executed, instructionAddress,
                            "memory operands are not supported by this interpreter slice");
            const unsigned rm = (modrm & 7U) | (rexB << 3U);
            const unsigned source = ((modrm >> 3U) & 7U) | (rexR << 3U);
            const bool destinationIsReg = opcode == 0x03 || opcode == 0x2b ||
                                          opcode == 0x33 || opcode == 0x3b;
            const unsigned destination = destinationIsReg ? source : rm;
            const unsigned operand = destinationIsReg ? rm : source;
            const uint64_t left = reg(cpu, destination);
            const uint64_t right = reg(cpu, operand);
            switch (opcode) {
                case 0x01: case 0x03: reg(cpu, destination) = add64(cpu, left, right); break;
                case 0x29: case 0x2b: reg(cpu, destination) = sub64(cpu, left, right); break;
                case 0x31: case 0x33:
                    reg(cpu, destination) = left ^ right;
                    setLogicFlags(cpu, reg(cpu, destination));
                    break;
                case 0x39: case 0x3b: (void) sub64(cpu, left, right); break;
                default: break;
            }
            cpu.rip = decoder.next();
        } else {
            return stop(X64StopReason::UnsupportedInstruction, executed, instructionAddress,
                        "instruction encoding is not implemented");
        }

        cpu.rflags |= kReservedFlag;
        ++executed;
    }

    return stop(X64StopReason::InstructionBudgetExceeded, executed, cpu.rip,
                "instruction budget exhausted");
}

}  // namespace rottr
