#include "rottr/x64_runtime.h"
#include "rottr/x64_decoder.h"
#include "rottr/x64_bridge.h"

#include <array>
#include <cstdint>
#include <cstdio>
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

bool contains(const X64GuestMemory& memory, uint64_t address, size_t length) {
    if (memory.data == nullptr || address < memory.base) return false;
    const uint64_t offset = address - memory.base;
    return offset <= memory.size && length <= memory.size - static_cast<size_t>(offset);
}

[[maybe_unused]] bool fetch(const X64GuestMemory& memory, uint64_t address, void* out, size_t size) {
    if (!memory.executable || !contains(memory, address, size)) return false;
    std::memcpy(out, memory.data + static_cast<size_t>(address - memory.base), size);
    return true;
}

[[maybe_unused]] bool readGuest(const X64GuestMemory& memory, uint64_t address, void* out, size_t size) {
    if (!contains(memory, address, size)) return false;
    std::memcpy(out, memory.data + static_cast<size_t>(address - memory.base), size);
    return true;
}

[[maybe_unused]] bool writeGuest(const X64GuestMemory& memory, uint64_t address,
                const void* value, size_t size) {
    if (!memory.writable || !contains(memory, address, size)) return false;
    std::memcpy(memory.data + static_cast<size_t>(address - memory.base), value, size);
    return true;
}

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

[[maybe_unused]] bool readOperand(const X64GuestMemory& memory, const rottr_cpu& cpu,
                 const RmOperand& operand, uint64_t& value) {
    if (operand.isRegister) {
        value = cpu.gpr[operand.registerIndex];
        return true;
    }
    return readGuest(memory, operand.address, &value, sizeof(value));
}

[[maybe_unused]] bool readOperandSized(const X64GuestMemory& memory, const rottr_cpu& cpu,
                      const RmOperand& operand, size_t size, uint64_t& value) {
    value = 0;
    if (operand.isRegister) {
        if (size == 8) value = cpu.gpr[operand.registerIndex];
        else if (size == 4) value = static_cast<uint32_t>(cpu.gpr[operand.registerIndex]);
        else if (size == 2) value = static_cast<uint16_t>(cpu.gpr[operand.registerIndex]);
        else if (size == 1) value = static_cast<uint8_t>(cpu.gpr[operand.registerIndex]);
        return true;
    }
    return readGuest(memory, operand.address, &value, size);
}

[[maybe_unused]] bool writeOperandSized(const X64GuestMemory& memory, rottr_cpu& cpu,
                       const RmOperand& operand, size_t size, uint64_t value) {
    if (operand.isRegister) {
        uint64_t& dst = reg(cpu, operand.registerIndex);
        if (size == 8) dst = value;
        else if (size == 4) dst = static_cast<uint32_t>(value);
        else if (size == 2) dst = (dst & ~0xffffULL) | static_cast<uint16_t>(value);
        else if (size == 1) dst = (dst & ~0xffULL) | static_cast<uint8_t>(value);
        return true;
    }
    return writeGuest(memory, operand.address, &value, size);
}

uint8_t parityTable[256];
bool parityInitialized = false;

void initParity() {
    for (int i = 0; i < 256; ++i) {
        int bits = 0;
        for (int b = i; b; b &= b - 1) ++bits;
        parityTable[i] = (bits % 2 == 0) ? 1 : 0;
    }
    parityInitialized = true;
}

uint8_t computeParity(uint64_t value, size_t /*size*/) {
    if (!parityInitialized) initParity();
    uint8_t v = static_cast<uint8_t>(value & 0xffULL);
    return parityTable[v];
}

void updateArithmeticFlags(rottr_cpu& cpu, uint64_t result, uint64_t operand1,
                           uint64_t operand2, bool sub, size_t size) {
    const uint64_t mask = (size == 8) ? ~0ULL :
                          (size == 4) ? 0xffffffffULL :
                          (size == 2) ? 0xffffULL : 0xffULL;
    result &= mask;
    cpu.rflags &= ~kArithmeticFlags;

    if (result == 0) cpu.rflags |= kFlagZero;
    if (static_cast<int64_t>(static_cast<intptr_t>(result)) < 0) cpu.rflags |= kFlagSign;
    if (computeParity(result, size)) cpu.rflags |= kFlagParity;

    if (sub) {
        if (operand2 > operand1) cpu.rflags |= kFlagCarry;
        uint64_t aflags = operand1 ^ operand2 ^ result;
        if (aflags & (1ULL << 4)) cpu.rflags |= kFlagAuxiliary;
        if ((operand1 ^ operand2) & (1ULL << (size * 8 - 1)) &&
            (operand1 ^ result) & (1ULL << (size * 8 - 1)))
            cpu.rflags |= kFlagOverflow;
    } else {
        uint64_t sum = operand1 + operand2;
        if (sum < operand1) cpu.rflags |= kFlagCarry;
        uint64_t aflags = operand1 ^ operand2 ^ result;
        if (aflags & (1ULL << 4)) cpu.rflags |= kFlagAuxiliary;
        if (((!(operand1 ^ operand2)) & (1ULL << (size * 8 - 1))) &&
            ((operand1 ^ result) & (1ULL << (size * 8 - 1))))
            cpu.rflags |= kFlagOverflow;
    }
}

void updateLogicalFlags(rottr_cpu& cpu, uint64_t result, size_t size) {
    const uint64_t mask = (size == 8) ? ~0ULL :
                          (size == 4) ? 0xffffffffULL :
                          (size == 2) ? 0xffffULL : 0xffULL;
    result &= mask;
    cpu.rflags &= ~(kFlagCarry | kFlagOverflow | kFlagAuxiliary | kArithmeticFlags);
    if (result == 0) cpu.rflags |= kFlagZero;
    if (static_cast<int64_t>(static_cast<intptr_t>(result)) < 0) cpu.rflags |= kFlagSign;
    if (computeParity(result, size)) cpu.rflags |= kFlagParity;
}

struct RexPrefix {
    bool w = false;
    bool r = false;
    bool x = false;
    bool b = false;
};

bool executeInstruction(rottr_cpu& cpu, const X64GuestMemory& memory,
                        Decoder& decoder, const RexPrefix& rex,
                        uint8_t opcode, size_t opSizeOverride,
                        bool& ripUpdated, X64StopReason& stopReason) {
    ripUpdated = false;
    stopReason = X64StopReason::InvalidMemory;
    const bool rexW = rex.w;
    const size_t opSize = rexW ? 8 : (opSizeOverride == 2 ? 2 : 8);

    // ---- MOV: B8+r (mov r64/r32, imm) ----
    if (opcode >= 0xb8 && opcode <= 0xbf) {
        unsigned regIndex = (opcode & 7) | (rex.r << 3);
        if (rexW) {
            uint64_t v = 0;
            if (!decoder.immediate(v)) return false;
            reg(cpu, regIndex) = v;
        } else {
            uint32_t v = 0;
            if (!decoder.immediate(v)) return false;
            reg(cpu, regIndex) = static_cast<uint32_t>(v);
        }
        return true;
    }

    // ---- MOV: 89 /r (mov r/m64, r64) ----
    if (opcode == 0x89) {
        uint8_t modrm = 0;
        if (!decoder.byte(modrm)) return false;
        RmOperand rm;
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm)) return false;
        uint64_t src = reg(cpu, ((modrm >> 3) & 7) | (rex.r << 3));
        return writeOperandSized(memory, cpu, rm, opSize, src);
    }

    // ---- MOV: 8B /r (mov r64, r/m64) ----
    if (opcode == 0x8b) {
        uint8_t modrm = 0;
        if (!decoder.byte(modrm)) return false;
        RmOperand rm;
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm)) return false;
        uint64_t src = 0;
        if (!readOperandSized(memory, cpu, rm, opSize, src)) return false;
        unsigned dstReg = ((modrm >> 3) & 7) | (rex.r << 3);
        reg(cpu, dstReg) = src;
        return true;
    }

    // ---- MOV: C6 /0 (mov r/m8, imm8) ----
    if (opcode == 0xc6) {
        uint8_t modrm = 0;
        if (!decoder.byte(modrm)) return false;
        RmOperand rm;
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm)) return false;
        uint8_t imm = 0;
        if (!decoder.immediate(imm)) return false;
        return writeOperandSized(memory, cpu, rm, 1, static_cast<uint64_t>(imm));
    }

    // ---- PUSH: 50+r (push r64) ----
    if (opcode >= 0x50 && opcode <= 0x57) {
        unsigned regIndex = (opcode & 7) | (rex.b << 3);
        uint64_t value = reg(cpu, regIndex);
        cpu.gpr[4] -= 8;
        if (!writeGuest(memory, cpu.gpr[4], &value, 8)) return false;
        return true;
    }

    // ---- FF /2 (call r/m64) and FF /6 (push r/m64) ----
    if (opcode == 0xff) {
        std::fprintf(stderr, "DEBUG: ff handler entered at 0x%llx\n", (unsigned long long)cpu.rip);
        uint8_t modrm = 0;
        if (!decoder.byte(modrm)) { std::fprintf(stderr, "DEBUG: ff: modrm fetch failed\n"); return false; }
        std::fprintf(stderr, "DEBUG: ff: modrm=0x%02x regField=%u\n", modrm, ((modrm >> 3) & 7));
        const unsigned regField = (modrm >> 3) & 7;
        if (regField == 2) { // call r/m64
            RmOperand rm;
            if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm)) return false;
            uint64_t target = 0;
            if (!readOperandSized(memory, cpu, rm, opSize, target)) return false;
            uint64_t returnAddr = decoder.next();
            if (resolveHostCallProc(target) != nullptr) {
                cpu.gpr[4] -= 8;
                if (!writeGuest(memory, cpu.gpr[4], &returnAddr, 8)) return false;
                uint64_t ret = callHostProc(target, cpu, memory);
                reg(cpu, 0) = ret;
                cpu.rip = returnAddr;
                ripUpdated = true;
                return true;
            }
            cpu.gpr[4] -= 8;
            if (!writeGuest(memory, cpu.gpr[4], &returnAddr, 8)) return false;
            cpu.rip = target;
            ripUpdated = true;
            return true;
        }
        if (regField == 6) { // push r/m64
            RmOperand rm;
            if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm)) return false;
            uint64_t value = 0;
            if (!readOperandSized(memory, cpu, rm, opSize, value)) return false;
            cpu.gpr[4] -= 8;
            if (!writeGuest(memory, cpu.gpr[4], &value, 8)) return false;
            return true;
        }
        return false;
    }

    // ---- POP: 58+r (pop r64) ----
    if (opcode >= 0x58 && opcode <= 0x5f) {
        unsigned regIndex = (opcode & 7) | (rex.b << 3);
        uint64_t value = 0;
        if (!readGuest(memory, cpu.gpr[4], &value, 8)) return false;
        reg(cpu, regIndex) = value;
        cpu.gpr[4] += 8;
        return true;
    }

    // ---- XOR: 31 /r (xor r/m64, r64) ----
    if (opcode == 0x31) {
        uint8_t modrm = 0;
        if (!decoder.byte(modrm)) return false;
        RmOperand rm;
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm)) return false;
        uint64_t src = reg(cpu, ((modrm >> 3) & 7) | (rex.r << 3));
        uint64_t dst = 0;
        if (!readOperandSized(memory, cpu, rm, opSize, dst)) return false;
        uint64_t result = dst ^ src;
        if (!writeOperandSized(memory, cpu, rm, opSize, result)) return false;
        updateLogicalFlags(cpu, result, opSize);
        return true;
    }

    // ---- XOR: 33 /r (xor r64, r/m64) ----
    if (opcode == 0x33) {
        uint8_t modrm = 0;
        if (!decoder.byte(modrm)) return false;
        RmOperand rm;
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm)) return false;
        uint64_t src = 0;
        readOperandSized(memory, cpu, rm, opSize, src);
        unsigned dstReg = ((modrm >> 3) & 7) | (rex.r << 3);
        uint64_t dst = reg(cpu, dstReg);
        uint64_t result = dst ^ src;
        reg(cpu, dstReg) = result;
        updateLogicalFlags(cpu, result, opSize);
        return true;
    }

    // ---- ADD: 00 /r (add r/m8, r8) ----
    if (opcode == 0x00) {
        uint8_t modrm = 0;
        if (!decoder.byte(modrm)) return false;
        RmOperand rm;
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm)) return false;
        uint64_t src = reg(cpu, ((modrm >> 3) & 7) | (rex.r << 3));
        uint64_t dst = 0;
        if (!readOperandSized(memory, cpu, rm, 1, dst)) return false;
        uint64_t result = dst + (src & 0xff);
        if (!writeOperandSized(memory, cpu, rm, 1, result)) return false;
        updateArithmeticFlags(cpu, result, dst, src & 0xff, false, 1);
        return true;
    }

    // ---- ADD: 02 /r (add r8, r/m8) ----
    if (opcode == 0x02) {
        uint8_t modrm = 0;
        if (!decoder.byte(modrm)) return false;
        RmOperand rm;
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm)) return false;
        uint64_t src = 0;
        if (!readOperandSized(memory, cpu, rm, 1, src)) return false;
        unsigned dstReg = ((modrm >> 3) & 7) | (rex.r << 3);
        uint64_t dst = reg(cpu, dstReg);
        uint64_t result = dst + (src & 0xff);
        reg(cpu, dstReg) = (reg(cpu, dstReg) & ~0xffULL) | (result & 0xff);
        updateArithmeticFlags(cpu, result, dst, src & 0xff, false, 1);
        return true;
    }

    // ---- ADD: 01 /r (add r/m64, r64) ----
    if (opcode == 0x01) {
        uint8_t modrm = 0;
        if (!decoder.byte(modrm)) return false;
        RmOperand rm;
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm)) return false;
        uint64_t src = reg(cpu, ((modrm >> 3) & 7) | (rex.r << 3));
        uint64_t dst = 0;
        if (!readOperandSized(memory, cpu, rm, opSize, dst)) return false;
        uint64_t result = dst + src;
        if (!writeOperandSized(memory, cpu, rm, opSize, result)) return false;
        updateArithmeticFlags(cpu, result, dst, src, false, opSize);
        return true;
    }

    // ---- ADD: 03 /r (add r64, r/m64) ----
    if (opcode == 0x03) {
        uint8_t modrm = 0;
        if (!decoder.byte(modrm)) return false;
        RmOperand rm;
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm)) return false;
        uint64_t src = 0;
        readOperandSized(memory, cpu, rm, opSize, src);
        unsigned dstReg = ((modrm >> 3) & 7) | (rex.r << 3);
        uint64_t dst = reg(cpu, dstReg);
        uint64_t result = dst + src;
        reg(cpu, dstReg) = result;
        updateArithmeticFlags(cpu, result, dst, src, false, opSize);
        return true;
    }

    // ---- ADD: 83 /0 ib (add r/m64, imm8 sign-extended) ----
    // ---- CMP: 83 /7 ib (cmp r/m64, imm8 sign-extended) ----
    if (opcode == 0x83) {
        uint8_t modrm = 0;
        if (!decoder.byte(modrm)) return false;
        const unsigned regField = (modrm >> 3) & 7;
        RmOperand rm;
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm)) return false;
        int8_t imm8 = 0;
        if (!decoder.immediate(imm8)) return false;
        int64_t imm = imm8;
    if (regField == 0) {
        uint64_t dst = 0;
        if (!readOperandSized(memory, cpu, rm, opSize, dst)) return false;
        uint64_t result = dst + static_cast<uint64_t>(imm);
        if (!writeOperandSized(memory, cpu, rm, opSize, result)) return false;
        updateArithmeticFlags(cpu, result, dst, static_cast<uint64_t>(imm), false, opSize);
        return true;
    }
    if (regField == 7) {
        uint64_t dst = 0;
        if (!readOperandSized(memory, cpu, rm, opSize, dst)) return false;
        uint64_t result = dst - static_cast<uint64_t>(imm);
        updateArithmeticFlags(cpu, result, dst, static_cast<uint64_t>(imm), true, opSize);
        return true;
    }
        return false;
    }

    // ---- CMP: 39 /r (cmp r/m64, r64) ----
    if (opcode == 0x39) {
        uint8_t modrm = 0;
        if (!decoder.byte(modrm)) return false;
        RmOperand rm;
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm)) return false;
        uint64_t src = reg(cpu, ((modrm >> 3) & 7) | (rex.r << 3));
        uint64_t dst = 0;
        readOperandSized(memory, cpu, rm, opSize, dst);
        uint64_t result = dst - src;
        updateArithmeticFlags(cpu, result, dst, src, true, opSize);
        return true;
    }

    // ---- CMP: 3b /r (cmp r64, r/m64) ----
    if (opcode == 0x3b) {
        uint8_t modrm = 0;
        if (!decoder.byte(modrm)) return false;
        RmOperand rm;
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm)) return false;
        uint64_t src = 0;
        readOperandSized(memory, cpu, rm, opSize, src);
        uint64_t dst = reg(cpu, ((modrm >> 3) & 7) | (rex.r << 3));
        uint64_t result = dst - src;
        updateArithmeticFlags(cpu, result, dst, src, true, opSize);
        return true;
    }

    // ---- OR: 09 /r (or r/m64, r64) ----
    if (opcode == 0x09) {
        uint8_t modrm = 0;
        if (!decoder.byte(modrm)) return false;
        RmOperand rm;
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm)) return false;
        uint64_t src = reg(cpu, ((modrm >> 3) & 7) | (rex.r << 3));
        uint64_t dst = 0;
        if (!readOperandSized(memory, cpu, rm, opSize, dst)) return false;
        uint64_t result = dst | src;
        if (!writeOperandSized(memory, cpu, rm, opSize, result)) return false;
        updateLogicalFlags(cpu, result, opSize);
        return true;
    }

    // ---- OR: 0b /r (or r64, r/m64) ----
    if (opcode == 0x0b) {
        uint8_t modrm = 0;
        if (!decoder.byte(modrm)) return false;
        RmOperand rm;
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm)) return false;
        uint64_t src = 0;
        readOperandSized(memory, cpu, rm, opSize, src);
        unsigned dstReg = ((modrm >> 3) & 7) | (rex.r << 3);
        uint64_t dst = reg(cpu, dstReg);
        uint64_t result = dst | src;
        reg(cpu, dstReg) = result;
        updateLogicalFlags(cpu, result, opSize);
        return true;
    }

    // ---- AND: 21 /r (and r/m64, r64) ----
    if (opcode == 0x21) {
        uint8_t modrm = 0;
        if (!decoder.byte(modrm)) return false;
        RmOperand rm;
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm)) return false;
        uint64_t src = reg(cpu, ((modrm >> 3) & 7) | (rex.r << 3));
        uint64_t dst = 0;
        if (!readOperandSized(memory, cpu, rm, opSize, dst)) return false;
        uint64_t result = dst & src;
        if (!writeOperandSized(memory, cpu, rm, opSize, result)) return false;
        updateLogicalFlags(cpu, result, opSize);
        return true;
    }

    // ---- AND: 23 /r (and r64, r/m64) ----
    if (opcode == 0x23) {
        uint8_t modrm = 0;
        if (!decoder.byte(modrm)) return false;
        RmOperand rm;
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm)) return false;
        uint64_t src = 0;
        readOperandSized(memory, cpu, rm, opSize, src);
        unsigned dstReg = ((modrm >> 3) & 7) | (rex.r << 3);
        uint64_t dst = reg(cpu, dstReg);
        uint64_t result = dst & src;
        reg(cpu, dstReg) = result;
        updateLogicalFlags(cpu, result, opSize);
        return true;
    }

    // ---- TEST: 85 /r (test r64, r/m64) ----
    if (opcode == 0x85) {
        uint8_t modrm = 0;
        if (!decoder.byte(modrm)) return false;
        RmOperand rm;
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm)) return false;
        uint64_t src = reg(cpu, ((modrm >> 3) & 7) | (rex.r << 3));
        uint64_t dst = 0;
        if (!readOperandSized(memory, cpu, rm, opSize, dst)) return false;
        uint64_t result = dst & src;
        updateLogicalFlags(cpu, result, opSize);
        return true;
    }

    // ---- C1 /0 ib (shl r64, imm8) ----
    // ---- C1 /4 ib (shr r64, imm8) ----
    // ---- C1 /5 ib (sar r64, imm8) ----
    if (opcode == 0xc1) {
        uint8_t modrm = 0;
        if (!decoder.byte(modrm)) return false;
        const unsigned regField = (modrm >> 3) & 7;
        RmOperand rm;
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm)) return false;
        uint8_t imm8 = 0;
        if (!decoder.immediate(imm8)) return false;
        uint64_t count = imm8 & 63;
        uint64_t dst = 0;
        if (!readOperandSized(memory, cpu, rm, opSize, dst)) return false;
        if (count == 0) return true; // shift by 0: no effect, flags unchanged
        uint64_t result = 0;
        if (regField == 4) {
            result = dst << count;
        } else if (regField == 5) {
            result = dst >> count;
        } else if (regField == 7) {
            result = (int64_t)dst >> count;
        } else {
            return false;
        }
        if (!writeOperandSized(memory, cpu, rm, opSize, result)) return false;
        updateLogicalFlags(cpu, result, opSize);
        return true;
    }

    // ---- D1 /0 (shl r/m64, 1) ----
    // ---- D3 /0 (shl r/m64, cl) ----
    if (opcode == 0xd1 || opcode == 0xd3) {
        uint8_t modrm = 0;
        if (!decoder.byte(modrm)) return false;
        const unsigned regField = (modrm >> 3) & 7;
        RmOperand rm;
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm)) return false;
        uint64_t count = (opcode == 0xd1) ? 1 : (cpu.gpr[1] & 63);
        uint64_t dst = 0;
        if (!readOperandSized(memory, cpu, rm, opSize, dst)) return false;
        if (count == 0) return true;
        uint64_t result = 0;
        if (regField == 4) result = dst << count;
        else if (regField == 5) result = dst >> count;
        else if (regField == 7) result = (int64_t)dst >> count;
        else return false;
        if (!writeOperandSized(memory, cpu, rm, opSize, result)) return false;
        updateLogicalFlags(cpu, result, opSize);
        return true;
    }

    // ---- 0F opcodes (two-byte escape) ----
    if (opcode == 0x0f) {
        uint8_t opcode2 = 0;
        if (!decoder.byte(opcode2)) return false;
        // ---- JCC near: 0F 80-8F rel32 ----
        if (opcode2 >= 0x80 && opcode2 <= 0x8f) {
            int32_t rel32 = 0;
            if (!decoder.immediate(rel32)) return false;
            if (conditionTrue(cpu, opcode2)) {
                cpu.rip = decoder.next() + static_cast<int64_t>(rel32);
            } else {
                cpu.rip = decoder.next();
            }
            ripUpdated = true;
            return true;
        }
        // ---- MOVZX/MOVSX: 0F B6/B7/BE/BF ----
        uint8_t modrm = 0;
        if (!decoder.byte(modrm)) return false;
        RmOperand rm;
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm)) return false;
        unsigned dstReg = ((modrm >> 3) & 7) | (rex.r << 3);
        if (opcode2 == 0xb6) {
            uint64_t v = 0;
            readOperandSized(memory, cpu, rm, 1, v);
            reg(cpu, dstReg) = v;
            return true;
        }
        if (opcode2 == 0xb7) {
            uint64_t v = 0;
            readOperandSized(memory, cpu, rm, 2, v);
            reg(cpu, dstReg) = v;
            return true;
        }
        if (opcode2 == 0xbe) {
            int8_t v = 0;
            readOperandSized(memory, cpu, rm, 1, reinterpret_cast<uint64_t&>(v));
            reg(cpu, dstReg) = static_cast<uint64_t>(static_cast<int64_t>(v));
            return true;
        }
        if (opcode2 == 0xbf) {
            int16_t v = 0;
            readOperandSized(memory, cpu, rm, 2, reinterpret_cast<uint64_t&>(v));
            reg(cpu, dstReg) = static_cast<uint64_t>(static_cast<int64_t>(v));
            return true;
        }
        // ---- UD2: 0F 0B ----
        if (opcode2 == 0x0b) {
            return false;
        }
        return false;
    }

    // ---- 63 /r (movsxd r64, r/m32) ----
    if (opcode == 0x63) {
        uint8_t modrm = 0;
        if (!decoder.byte(modrm)) return false;
        RmOperand rm;
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm)) return false;
        unsigned dstReg = ((modrm >> 3) & 7) | (rex.r << 3);
        int32_t v = 0;
        readOperandSized(memory, cpu, rm, 4, reinterpret_cast<uint64_t&>(v));
        reg(cpu, dstReg) = static_cast<uint64_t>(static_cast<int64_t>(v));
        return true;
    }

    // ---- 70-7f rel8 (jcc short) ----
    if (opcode >= 0x70 && opcode <= 0x7f) {
        int8_t rel8 = 0;
        if (!decoder.immediate(rel8)) return false;
        if (conditionTrue(cpu, opcode)) {
            cpu.rip = decoder.next() + static_cast<int64_t>(rel8);
        } else {
            cpu.rip = decoder.next();
        }
        ripUpdated = true;
        return true;
    }

    // ---- E9 rel32 (jmp near) ----
    if (opcode == 0xe9) {
        int32_t rel32 = 0;
        if (!decoder.immediate(rel32)) return false;
        cpu.rip = decoder.next() + static_cast<int64_t>(rel32);
        ripUpdated = true;
        return true;
    }

    // ---- EB rel8 (jmp short) ----
    if (opcode == 0xeb) {
        int8_t rel8 = 0;
        if (!decoder.immediate(rel8)) return false;
        cpu.rip = decoder.next() + static_cast<int64_t>(rel8);
        ripUpdated = true;
        return true;
    }

    // ---- E8 rel32 (call near) ----
    if (opcode == 0xe8) {
        int32_t rel32 = 0;
        if (!decoder.immediate(rel32)) return false;
        uint64_t returnAddr = decoder.next();
        uint64_t target = returnAddr + static_cast<int64_t>(rel32);
        std::fprintf(stderr, "DEBUG: call direct target=0x%llx hostProc=%p\n",
                (unsigned long long)target,
                (void*)resolveHostCallProc(target));
        if (resolveHostCallProc(target) != nullptr) {
            cpu.gpr[4] -= 8;
            if (!writeGuest(memory, cpu.gpr[4], &returnAddr, 8)) return false;
            uint64_t ret = callHostProc(target, cpu, memory);
            reg(cpu, 0) = ret;
            cpu.rip = returnAddr;
            ripUpdated = true;
            return true;
        }
        cpu.gpr[4] -= 8;
        if (!writeGuest(memory, cpu.gpr[4], &returnAddr, 8)) return false;
        cpu.rip = target;
        ripUpdated = true;
        return true;
    }

    // ---- C3 (ret near) ----
    if (opcode == 0xc3) {
        uint64_t returnAddr = 0;
        if (!readGuest(memory, cpu.gpr[4], &returnAddr, 8)) return false;
        cpu.gpr[4] += 8;
        cpu.rip = returnAddr;
        ripUpdated = true;
        return true;
    }

    return false;
}

}  // namespace

X64RunResult runX64Interpreter(rottr_cpu& cpu, const X64GuestMemory& memory,
                               uint64_t instructionBudget) {
    X64RunResult result;
    uint64_t executed = 0;
    while (executed < instructionBudget) {
        Decoder decoder(memory, cpu.rip);
        uint64_t instStart = cpu.rip;

        uint8_t first = 0;
        if (!decoder.byte(first)) {
            result.reason = X64StopReason::InvalidMemory;
            result.faultAddress = decoder.next();
            break;
        }

        RexPrefix rex{};
        size_t opSizeOverride = 8; // default 64-bit
        // Legacy prefixes: 0x66 = operand-size override (16-bit)
        while (first == 0x66 || first == 0xf2 || first == 0xf3) {
            if (first == 0x66) opSizeOverride = 2;
            if (!decoder.byte(first)) {
                result.reason = X64StopReason::InvalidMemory;
                result.faultAddress = decoder.next();
                break;
            }
        }
        if ((first & 0xf0) == 0x40) {
            rex.w = (first & 0x08) != 0;
            rex.r = (first & 0x04) != 0;
            rex.x = (first & 0x02) != 0;
            rex.b = (first & 0x01) != 0;
            if (!decoder.byte(first)) {
                result.reason = X64StopReason::InvalidMemory;
                result.faultAddress = decoder.next();
                break;
            }
        }

        if (first == 0xf4) { cpu.rip = decoder.next(); result.reason = X64StopReason::Halted; executed++; break; }
        if (first == 0x90) { cpu.rip = decoder.next(); executed++; continue; }
        if (first == 0xcc) { result.reason = X64StopReason::InvalidCpuState; result.faultAddress = instStart; break; }

        bool ripUpdated = false;
        X64StopReason stopReason = X64StopReason::UnsupportedInstruction;
        bool ok = executeInstruction(cpu, memory, decoder, rex, first, opSizeOverride, ripUpdated, stopReason);
        if (!ok) {
            std::fprintf(stderr, "DEBUG: executeInstruction failed at 0x%llx opcode=0x%02x stopReason=%d\n",
                    (unsigned long long)instStart, first, (int)stopReason);
            result.reason = stopReason;
            result.faultAddress = instStart;
            break;
        }
        if (!ripUpdated) cpu.rip = decoder.next();
        executed++;
    }
    result.instructionsExecuted = executed;
    return result;
}

bool hasX64InstructionInterpreter() { return true; }

}  // namespace rottr
