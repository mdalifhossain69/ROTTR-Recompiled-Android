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

bool conditionTrue(const rottr_cpu& cpu, unsigned condition) {
    const bool cf = (cpu.rflags & kFlagCarry) != 0;
    const bool pf = (cpu.rflags & kFlagParity) != 0;
    const bool zf = (cpu.rflags & kFlagZero) != 0;
    const bool sf = (cpu.rflags & kFlagSign) != 0;
    const bool of = (cpu.rflags & kFlagOverflow) != 0;
    switch (condition & 0x0fU) {
        case 0x0: return of;
        case 0x1: return !of;
        case 0x2: return cf;
        case 0x3: return !cf;
        case 0x4: return zf;
        case 0x5: return !zf;
        case 0x6: return cf || zf;
        case 0x7: return !cf && !zf;
        case 0x8: return sf;
        case 0x9: return !sf;
        case 0xa: return pf;
        case 0xb: return !pf;
        case 0xc: return sf != of;
        case 0xd: return sf == of;
        case 0xe: return zf || (sf != of);
        case 0xf: return !zf && (sf == of);
        default: return false;
    }
}
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

struct RmOperand {
    bool isRegister = false;
    unsigned registerIndex = 0;
    uint64_t address = 0;
};

bool decodeRmOperand(Decoder& decoder, const rottr_cpu& cpu, uint8_t modrm,
                     unsigned rexB, unsigned rexX, RmOperand& operand) {
    const unsigned mod = modrm >> 6U;
    const unsigned rm = modrm & 7U;
    if (mod == 3) {
        operand.isRegister = true;
        operand.registerIndex = rm | (rexB << 3U);
        return true;
    }

    uint64_t base = 0;
    uint64_t index = 0;
    unsigned scale = 0;
    bool hasBase = true;
    bool ripRelative = false;

    if (rm == 4) {
        uint8_t sib = 0;
        if (!decoder.byte(sib)) return false;
        scale = sib >> 6U;
        const unsigned indexField = (sib >> 3U) & 7U;
        const unsigned baseField = sib & 7U;
        if (indexField != 4 || rexX != 0)
            index = cpu.gpr[indexField | (rexX << 3U)];
        if (mod == 0 && baseField == 5) hasBase = false;
        else base = cpu.gpr[baseField | (rexB << 3U)];
    } else if (mod == 0 && rm == 5) {
        hasBase = false;
        ripRelative = true;
    } else {
        base = cpu.gpr[rm | (rexB << 3U)];
    }

    int64_t displacement = 0;
    if (mod == 1) {
        int8_t value = 0;
        if (!decoder.immediate(value)) return false;
        displacement = value;
    } else if (mod == 2 || (mod == 0 && !hasBase)) {
        int32_t value = 0;
        if (!decoder.immediate(value)) return false;
        displacement = value;
    }

    if (ripRelative) base = decoder.next();
    operand.address = (hasBase || ripRelative ? base : 0) +
                      (index << scale) + static_cast<uint64_t>(displacement);
    return true;
}

bool readOperand(const X64GuestMemory& memory, const rottr_cpu& cpu,
                 const RmOperand& operand, uint64_t& value) {
    if (operand.isRegister) {
        value = cpu.gpr[operand.registerIndex];
        return true;
    }
    return readGuest(memory, operand.address, &value, sizeof(value));
}


bool readOperandSized(const X64GuestMemory& memory, const rottr_cpu& cpu,
                      const RmOperand& operand, size_t size, uint64_t& value) {
    value = 0;
    if (operand.isRegister) {
        value = cpu.gpr[operand.registerIndex];
        if (size == 1) value &= 0xffU;
        else if (size == 2) value &= 0xffffU;
        else if (size == 4) value &= 0xffffffffU;
        return true;
    }
    return readGuest(memory, operand.address, &value, size);
}

bool writeOperand(const X64GuestMemory& memory, rottr_cpu& cpu,
                  const RmOperand& operand, uint64_t value) {
    if (operand.isRegister) {
        cpu.gpr[operand.registerIndex] = value;
        return true;
    }
    return writeGuest(memory, operand.address, &value, sizeof(value));
}

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


uint64_t shift64(rottr_cpu& cpu, uint64_t value, unsigned count, unsigned operation) {
    count &= 0x3fU;
    if (count == 0) return value;

    uint64_t result = value;
    bool carry = false;
    if (operation == 4 || operation == 6) { // SHL/SAL
        carry = ((value >> (64U - count)) & 1U) != 0;
        result = value << count;
    } else if (operation == 5) { // SHR
        carry = ((value >> (count - 1U)) & 1U) != 0;
        result = value >> count;
    } else if (operation == 7) { // SAR
        carry = ((value >> (count - 1U)) & 1U) != 0;
        result = value >> count;
        if ((value >> 63U) != 0)
            result |= (~uint64_t{0}) << (64U - count);
    }

    cpu.rflags &= ~(kFlagCarry | kFlagOverflow | kFlagZero |
                    kFlagSign | kFlagParity);
    if (carry) cpu.rflags |= kFlagCarry;
    if (result == 0) cpu.rflags |= kFlagZero;
    if ((result >> 63U) != 0) cpu.rflags |= kFlagSign;
    if (evenParity(static_cast<uint8_t>(result))) cpu.rflags |= kFlagParity;
    if (count == 1) {
        bool overflow = false;
        if (operation == 4 || operation == 6)
            overflow = ((result >> 63U) != 0) != carry;
        else if (operation == 5)
            overflow = (value >> 63U) != 0;
        if (overflow) cpu.rflags |= kFlagOverflow;
    }
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
    uint64_t callDepth = 0;
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
        const unsigned rexX = (rex >> 1U) & 1U;

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
            if (callDepth == 0)
                return stop(X64StopReason::Returned, executed, target, "top-level RET reached");
            --callDepth;
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
                if (index == 4) cpu.gpr[4] = value;
                else {
                    reg(cpu, index) = value;
                    cpu.gpr[4] += sizeof(uint64_t);
                }
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
                ++callDepth;
            }
            cpu.rip = target;
        } else if (opcode == 0x0f) {
            uint8_t secondOpcode = 0;
            if (!decoder.byte(secondOpcode))
                return stop(X64StopReason::InvalidMemory, executed, instructionAddress,
                            "truncated 0F opcode");
            if (secondOpcode >= 0x80 && secondOpcode <= 0x8f) {
                int32_t displacement = 0;
                if (!decoder.immediate(displacement))
                    return stop(X64StopReason::InvalidMemory, executed, instructionAddress,
                                "truncated near conditional branch");
                cpu.rip = conditionTrue(cpu, secondOpcode & 0x0fU) ?
                        decoder.next() + static_cast<int64_t>(displacement) : decoder.next();
            } else if (secondOpcode == 0xb6 || secondOpcode == 0xb7 ||
                       secondOpcode == 0xbe || secondOpcode == 0xbf) {
                uint8_t modrm = 0;
                if (!decoder.byte(modrm))
                    return stop(X64StopReason::InvalidMemory, executed, instructionAddress,
                                "truncated MOVZX/MOVSX ModRM");
                RmOperand source;
                if (!decodeRmOperand(decoder, cpu, modrm, rexB, rexX, source))
                    return stop(X64StopReason::InvalidMemory, executed, instructionAddress,
                                "truncated MOVZX/MOVSX address");
                const unsigned destination = ((modrm >> 3U) & 7U) | (rexR << 3U);
                const size_t sourceSize = (secondOpcode == 0xb7 || secondOpcode == 0xbf) ? 2 : 1;
                uint64_t value = 0;
                if (!readOperandSized(memory, cpu, source, sourceSize, value))
                    return stop(X64StopReason::InvalidMemory, executed, source.address,
                                "MOVZX/MOVSX source read failed");
                const bool signExtend = secondOpcode == 0xbe || secondOpcode == 0xbf;
                if (signExtend) {
                    if (sourceSize == 1)
                        value = static_cast<uint64_t>(static_cast<int64_t>(
                                static_cast<int8_t>(value)));
                    else
                        value = static_cast<uint64_t>(static_cast<int64_t>(
                                static_cast<int16_t>(value)));
                }
                reg(cpu, destination) = rexW ? value : static_cast<uint32_t>(value);
                cpu.rip = decoder.next();
            } else {
                return stop(X64StopReason::UnsupportedInstruction, executed, instructionAddress,
                            "unsupported two-byte opcode");
            }
        } else if (opcode >= 0x70 && opcode <= 0x7f) {
            int8_t displacement = 0;
            if (!decoder.immediate(displacement))
                return stop(X64StopReason::InvalidMemory, executed, instructionAddress,
                            "truncated short conditional branch");
            cpu.rip = conditionTrue(cpu, opcode & 0x0fU) ?
                    decoder.next() + static_cast<int64_t>(displacement) : decoder.next();
        } else if (opcode == 0xeb) {
            int8_t displacement = 0;
            if (!decoder.immediate(displacement))
                return stop(X64StopReason::InvalidMemory, executed, instructionAddress,
                            "truncated short JMP");
            cpu.rip = decoder.next() + static_cast<int64_t>(displacement);
        } else if (opcode == 0x81 || opcode == 0x83) {
            if (!rexW)
                return stop(X64StopReason::UnsupportedInstruction, executed, instructionAddress,
                            "only 64-bit immediate group-1 encodings are supported");
            uint8_t modrm = 0;
            if (!decoder.byte(modrm))
                return stop(X64StopReason::InvalidMemory, executed, instructionAddress,
                            "truncated immediate arithmetic ModRM");
            RmOperand destination;
            if (!decodeRmOperand(decoder, cpu, modrm, rexB, rexX, destination))
                return stop(X64StopReason::InvalidMemory, executed, instructionAddress,
                            "truncated immediate arithmetic address");
            const unsigned operation = (modrm >> 3U) & 7U;
            int64_t signedImmediate = 0;
            if (opcode == 0x83) {
                int8_t value = 0;
                if (!decoder.immediate(value))
                    return stop(X64StopReason::InvalidMemory, executed, instructionAddress,
                                "truncated sign-extended imm8");
                signedImmediate = value;
            } else {
                int32_t value = 0;
                if (!decoder.immediate(value))
                    return stop(X64StopReason::InvalidMemory, executed, instructionAddress,
                                "truncated sign-extended imm32");
                signedImmediate = value;
            }
            uint64_t left = 0;
            if (!readOperand(memory, cpu, destination, left))
                return stop(X64StopReason::InvalidMemory, executed, destination.address,
                            "immediate arithmetic destination read failed");
            const uint64_t right = static_cast<uint64_t>(signedImmediate);
            uint64_t result = left;
            bool writeResult = true;
            switch (operation) {
                case 0: result = add64(cpu, left, right); break;
                case 1:
                    result = left | right;
                    setLogicFlags(cpu, result);
                    break;
                case 4:
                    result = left & right;
                    setLogicFlags(cpu, result);
                    break;
                case 5: result = sub64(cpu, left, right); break;
                case 6:
                    result = left ^ right;
                    setLogicFlags(cpu, result);
                    break;
                case 7:
                    (void) sub64(cpu, left, right);
                    writeResult = false;
                    break;
                default:
                    return stop(X64StopReason::UnsupportedInstruction, executed,
                                instructionAddress,
                                "immediate group-1 operation is not implemented");
            }
            if (writeResult && !writeOperand(memory, cpu, destination, result))
                return stop(destination.isRegister ? X64StopReason::InvalidMemory :
                            (memory.writable ? X64StopReason::InvalidMemory :
                                               X64StopReason::ReadOnlyMemory),
                            executed, destination.address,
                            "immediate arithmetic destination write failed");
            cpu.rip = decoder.next();
        } else if (opcode == 0x63 && rexW) {
            uint8_t modrm = 0;
            if (!decoder.byte(modrm))
                return stop(X64StopReason::InvalidMemory, executed, instructionAddress,
                            "truncated MOVSXD ModRM");
            RmOperand source;
            if (!decodeRmOperand(decoder, cpu, modrm, rexB, rexX, source))
                return stop(X64StopReason::InvalidMemory, executed, instructionAddress,
                            "truncated MOVSXD address");
            const unsigned destination = ((modrm >> 3U) & 7U) | (rexR << 3U);
            uint64_t value = 0;
            if (!readOperandSized(memory, cpu, source, 4, value))
                return stop(X64StopReason::InvalidMemory, executed, source.address,
                            "MOVSXD source read failed");
            reg(cpu, destination) = static_cast<uint64_t>(
                    static_cast<int64_t>(static_cast<int32_t>(value)));
            cpu.rip = decoder.next();
        } else if (opcode == 0xc1 || opcode == 0xd1 || opcode == 0xd3) {
            if (!rexW)
                return stop(X64StopReason::UnsupportedInstruction, executed, instructionAddress,
                            "only 64-bit shift encodings are supported");
            uint8_t modrm = 0;
            if (!decoder.byte(modrm))
                return stop(X64StopReason::InvalidMemory, executed, instructionAddress,
                            "truncated shift ModRM");
            RmOperand destination;
            if (!decodeRmOperand(decoder, cpu, modrm, rexB, rexX, destination))
                return stop(X64StopReason::InvalidMemory, executed, instructionAddress,
                            "truncated shift address");
            const unsigned operation = (modrm >> 3U) & 7U;
            if (operation != 4 && operation != 5 && operation != 6 && operation != 7)
                return stop(X64StopReason::UnsupportedInstruction, executed,
                            instructionAddress, "unsupported shift group operation");
            unsigned count = 1;
            if (opcode == 0xc1) {
                uint8_t immediate = 0;
                if (!decoder.immediate(immediate))
                    return stop(X64StopReason::InvalidMemory, executed, instructionAddress,
                                "truncated shift immediate");
                count = immediate & 0x3fU;
            } else if (opcode == 0xd3) {
                count = static_cast<unsigned>(cpu.gpr[1] & 0x3fU);
            }
            uint64_t value = 0;
            if (!readOperand(memory, cpu, destination, value))
                return stop(X64StopReason::InvalidMemory, executed, destination.address,
                            "shift destination read failed");
            const uint64_t shifted = shift64(cpu, value, count, operation);
            if (count != 0 && !writeOperand(memory, cpu, destination, shifted))
                return stop(destination.isRegister ? X64StopReason::InvalidMemory :
                            (memory.writable ? X64StopReason::InvalidMemory :
                                               X64StopReason::ReadOnlyMemory),
                            executed, destination.address, "shift destination write failed");
            cpu.rip = decoder.next();
        } else if (opcode == 0x89 || opcode == 0x8b ||
                   opcode == 0x01 || opcode == 0x03 || opcode == 0x29 ||
                   opcode == 0x2b || opcode == 0x09 || opcode == 0x0b ||
                   opcode == 0x21 || opcode == 0x23 || opcode == 0x31 ||
                   opcode == 0x33 || opcode == 0x39 || opcode == 0x3b ||
                   opcode == 0x85) {
            if (!rexW)
                return stop(X64StopReason::UnsupportedInstruction, executed, instructionAddress,
                            "only 64-bit MOV and arithmetic encodings are supported");
            uint8_t modrm = 0;
            if (!decoder.byte(modrm))
                return stop(X64StopReason::InvalidMemory, executed, instructionAddress,
                            "truncated ModRM byte");
            RmOperand rmOperand;
            if (!decodeRmOperand(decoder, cpu, modrm, rexB, rexX, rmOperand))
                return stop(X64StopReason::InvalidMemory, executed, instructionAddress,
                            "truncated ModRM/SIB displacement");
            const unsigned regField = ((modrm >> 3U) & 7U) | (rexR << 3U);
            if (opcode == 0x89 || opcode == 0x8b) {
                if (opcode == 0x89) {
                    if (!writeOperand(memory, cpu, rmOperand, reg(cpu, regField)))
                        return stop(rmOperand.isRegister ? X64StopReason::InvalidMemory :
                                    (memory.writable ? X64StopReason::InvalidMemory :
                                                       X64StopReason::ReadOnlyMemory),
                                    executed, rmOperand.address, "MOV destination write failed");
                } else {
                    uint64_t value = 0;
                    if (!readOperand(memory, cpu, rmOperand, value))
                        return stop(X64StopReason::InvalidMemory, executed, rmOperand.address,
                                    "MOV source read failed");
                    reg(cpu, regField) = value;
                }
            } else {
                const bool destinationIsReg = opcode == 0x03 || opcode == 0x2b ||
                                              opcode == 0x0b || opcode == 0x23 ||
                                              opcode == 0x33 || opcode == 0x3b;
                const uint64_t sourceValue = destinationIsReg ? 0 : reg(cpu, regField);
                uint64_t rmValue = 0;
                if (!readOperand(memory, cpu, rmOperand, rmValue))
                    return stop(X64StopReason::InvalidMemory, executed, rmOperand.address,
                                "arithmetic source read failed");
                const uint64_t left = destinationIsReg ? reg(cpu, regField) : rmValue;
                const uint64_t right = destinationIsReg ? rmValue : sourceValue;
                uint64_t result = left;
                bool writeResult = true;
                switch (opcode) {
                    case 0x01: case 0x03: result = add64(cpu, left, right); break;
                    case 0x29: case 0x2b: result = sub64(cpu, left, right); break;
                    case 0x09: case 0x0b:
                        result = left | right;
                        setLogicFlags(cpu, result);
                        break;
                    case 0x21: case 0x23:
                        result = left & right;
                        setLogicFlags(cpu, result);
                        break;
                    case 0x31: case 0x33:
                        result = left ^ right;
                        setLogicFlags(cpu, result);
                        break;
                    case 0x39: case 0x3b: case 0x85:
                        (void) sub64(cpu, opcode == 0x85 ? (left & right) : left,
                                     opcode == 0x85 ? 0 : right);
                        if (opcode == 0x85) setLogicFlags(cpu, left & right);
                        writeResult = false;
                        break;
                    default: break;
                }
                if (writeResult) {
                    const bool stored = destinationIsReg ?
                            (reg(cpu, regField) = result, true) :
                            writeOperand(memory, cpu, rmOperand, result);
                    if (!stored)
                        return stop(memory.writable ? X64StopReason::InvalidMemory :
                                                      X64StopReason::ReadOnlyMemory,
                                    executed, rmOperand.address, "arithmetic destination write failed");
                }
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
