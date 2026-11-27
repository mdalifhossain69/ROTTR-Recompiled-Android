#include "rottr/x64_runtime.h"
#include "rottr/x64_decoder.h"

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

// Extracted decoder (Step 19)
using Decoder = rottr::Decoder;

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
