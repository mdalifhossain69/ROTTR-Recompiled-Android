#include "rottr/x64_runtime.h"
#include "rottr/x64_decoder.h"
#include "rottr/x64_bridge.h"

#include <array>
#include <atomic>
#include <chrono>
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

bool isAtomicSize(size_t size) {
    return size == 1 || size == 2 || size == 4 || size == 8;
}

// Guest memory is shared by every host thread running the interpreter, so all
// naturally aligned accesses go through relaxed atomics to keep the program
// free of data races. Unaligned accesses fall back to memcpy (x86 permits them
// but they cannot participate in guest atomic operations).
[[maybe_unused]] bool readGuest(const X64GuestMemory& memory, uint64_t address, void* out, size_t size) {
    if (!contains(memory, address, size)) return false;
    uint8_t* p = memory.data + static_cast<size_t>(address - memory.base);
    if (isAtomicSize(size) && (address % size) == 0) {
        switch (size) {
            case 1: *static_cast<uint8_t*>(out) =
                        __atomic_load_n(reinterpret_cast<uint8_t*>(p), __ATOMIC_RELAXED); break;
            case 2: *static_cast<uint16_t*>(out) =
                        __atomic_load_n(reinterpret_cast<uint16_t*>(p), __ATOMIC_RELAXED); break;
            case 4: *static_cast<uint32_t*>(out) =
                        __atomic_load_n(reinterpret_cast<uint32_t*>(p), __ATOMIC_RELAXED); break;
            default: *static_cast<uint64_t*>(out) =
                        __atomic_load_n(reinterpret_cast<uint64_t*>(p), __ATOMIC_RELAXED); break;
        }
    } else {
        std::memcpy(out, p, size);
    }
    return true;
}

[[maybe_unused]] bool writeGuest(const X64GuestMemory& memory, uint64_t address,
                const void* value, size_t size) {
    if (!memory.writable || !contains(memory, address, size)) return false;
    uint8_t* p = memory.data + static_cast<size_t>(address - memory.base);
    if (isAtomicSize(size) && (address % size) == 0) {
        switch (size) {
            case 1: __atomic_store_n(reinterpret_cast<uint8_t*>(p),
                        *static_cast<const uint8_t*>(value), __ATOMIC_RELAXED); break;
            case 2: __atomic_store_n(reinterpret_cast<uint16_t*>(p),
                        *static_cast<const uint16_t*>(value), __ATOMIC_RELAXED); break;
            case 4: __atomic_store_n(reinterpret_cast<uint32_t*>(p),
                        *static_cast<const uint32_t*>(value), __ATOMIC_RELAXED); break;
            default: __atomic_store_n(reinterpret_cast<uint64_t*>(p),
                        *static_cast<const uint64_t*>(value), __ATOMIC_RELAXED); break;
        }
    } else {
        std::memcpy(p, value, size);
    }
    return true;
}

// ---- Atomic read-modify-write helpers (LOCK prefix / XCHG) ----
enum class AtomicOp { Add, Sub, Or, And, Xor, Exchange };

template <typename T>
bool atomicRmwT(AtomicOp op, void* p, uint64_t value, uint64_t& old) {
    T v = static_cast<T>(value);
    T* typed = static_cast<T*>(p);
    switch (op) {
        case AtomicOp::Add: old = __atomic_fetch_add(typed, v, __ATOMIC_SEQ_CST); break;
        case AtomicOp::Sub: old = __atomic_fetch_sub(typed, v, __ATOMIC_SEQ_CST); break;
        case AtomicOp::Or:  old = __atomic_fetch_or(typed, v, __ATOMIC_SEQ_CST); break;
        case AtomicOp::And: old = __atomic_fetch_and(typed, v, __ATOMIC_SEQ_CST); break;
        case AtomicOp::Xor: old = __atomic_fetch_xor(typed, v, __ATOMIC_SEQ_CST); break;
        case AtomicOp::Exchange:
            old = __atomic_exchange_n(typed, v, __ATOMIC_SEQ_CST);
            break;
    }
    return true;
}

bool guestAtomicRmw(const X64GuestMemory& memory, uint64_t address,
                    AtomicOp op, uint64_t value, size_t size, uint64_t& old) {
    if (!memory.writable || !contains(memory, address, size) || !isAtomicSize(size))
        return false;
    if ((address % size) != 0) return false;  // C++ atomics require alignment
    void* p = memory.data + static_cast<size_t>(address - memory.base);
    switch (size) {
        case 1: return atomicRmwT<uint8_t>(op, p, value, old);
        case 2: return atomicRmwT<uint16_t>(op, p, value, old);
        case 4: return atomicRmwT<uint32_t>(op, p, value, old);
        case 8: return atomicRmwT<uint64_t>(op, p, value, old);
        default: return false;
    }
}

// Returns the previous memory value; `ok` reports whether it matched `expected`.
bool guestAtomicCmpxchg(const X64GuestMemory& memory, uint64_t address,
                        uint64_t expected, uint64_t desired, size_t size,
                        uint64_t& old, bool& matched) {
    if (!memory.writable || !contains(memory, address, size) || !isAtomicSize(size))
        return false;
    if ((address % size) != 0) return false;
    void* p = memory.data + static_cast<size_t>(address - memory.base);
    auto run = [&](auto tag) {
        using T = decltype(tag);
        T exp = static_cast<T>(expected);
        bool ok = __atomic_compare_exchange_n(static_cast<T*>(p), &exp,
                                              static_cast<T>(desired), false,
                                              __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
        matched = ok;
        old = static_cast<uint64_t>(exp);
    };
    switch (size) {
        case 1: run(uint8_t{}); break;
        case 2: run(uint16_t{}); break;
        case 4: run(uint32_t{}); break;
        case 8: run(uint64_t{}); break;
        default: return false;
    }
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
                     unsigned rexB, unsigned rexX, RmOperand& operand,
                     uint64_t segBase = 0) {
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
                      (index << scale) + static_cast<uint64_t>(displacement) +
                      segBase;
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

// Monotonic timestamp counter for RDTSC. Wall-clock nanoseconds are used when
// available, but the value is forced to strictly increase so back-to-back
// reads never go backward.
uint64_t nextTsc() {
    static std::atomic<uint64_t> counter{0};
    const uint64_t now = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    uint64_t prev = counter.load(std::memory_order_relaxed);
    uint64_t next = 0;
    do {
        next = (now > prev) ? now : prev + 1;
    } while (!counter.compare_exchange_weak(prev, next, std::memory_order_relaxed));
    return next;
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

struct DecodedInstruction {
    RexPrefix rex{};
    uint8_t opcode = 0;
    size_t opSizeOverride = 8;
    unsigned repPrefix = 0;
    bool segmentOverride = false;
    bool lockPrefix = false;
};

bool decodePrefixesAndOpcode(Decoder& decoder,
                             DecodedInstruction& instruction) {
    uint8_t first = 0;
    if (!decoder.byte(first)) return false;

    while (first == 0x64 || first == 0x65 || first == 0x66 ||
           first == 0xf0 || first == 0xf2 || first == 0xf3) {
        if (first == 0x64 || first == 0x65)
            instruction.segmentOverride = true;
        else if (first == 0x66)
            instruction.opSizeOverride = 2;
        else if (first == 0xf0)
            instruction.lockPrefix = true;
        else if (first == 0xf2)
            instruction.repPrefix = 2;
        else if (first == 0xf3)
            instruction.repPrefix = 1;
        if (!decoder.byte(first)) return false;
    }

    if ((first & 0xf0) == 0x40) {
        instruction.rex.w = (first & 0x08) != 0;
        instruction.rex.r = (first & 0x04) != 0;
        instruction.rex.x = (first & 0x02) != 0;
        instruction.rex.b = (first & 0x01) != 0;
        if (!decoder.byte(first)) return false;
    }

    instruction.opcode = first;
    return true;
}

bool executeInstruction(rottr_cpu& cpu, const X64GuestMemory& memory,
                        Decoder& decoder, const RexPrefix& rex,
                        uint8_t opcode, size_t opSizeOverride,
                        unsigned repPrefix, uint64_t segBase, bool lockPrefix,
                        bool& ripUpdated, X64StopReason& stopReason) {
    ripUpdated = false;
    stopReason = X64StopReason::InvalidMemory;
    const bool rexW = rex.w;
    const size_t opSize = rexW ? 8 : (opSizeOverride == 2 ? 2 : 8);

    // ---- MOV: B0+r (mov r8, imm8) ----
    if (opcode >= 0xb0 && opcode <= 0xb7) {
        unsigned regIndex = (opcode & 7) | (rex.r << 3);
        uint8_t v = 0;
        if (!decoder.immediate(v)) return false;
        reg(cpu, regIndex) = (reg(cpu, regIndex) & ~0xffULL) | static_cast<uint64_t>(v);
        return true;
    }

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
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm, segBase)) return false;
        uint64_t src = reg(cpu, ((modrm >> 3) & 7) | (rex.r << 3));
        return writeOperandSized(memory, cpu, rm, opSize, src);
    }

    // ---- MOV: 8B /r (mov r64, r/m64) ----
    if (opcode == 0x8b) {
        uint8_t modrm = 0;
        if (!decoder.byte(modrm)) return false;
        RmOperand rm;
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm, segBase)) return false;
        uint64_t src = 0;
        if (!readOperandSized(memory, cpu, rm, opSize, src)) return false;
        unsigned dstReg = ((modrm >> 3) & 7) | (rex.r << 3);
        reg(cpu, dstReg) = src;
        return true;
    }

    // ---- XCHG: 86 /r (xchg r/m8, r8), 87 /r (xchg r/m, r) ----
    // XCHG is implicitly locked, so a memory form always uses the atomic path.
    if (opcode == 0x86 || opcode == 0x87) {
        const size_t size = (opcode == 0x86) ? 1 : opSize;
        uint8_t modrm = 0;
        if (!decoder.byte(modrm)) return false;
        RmOperand rm;
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm, segBase)) return false;
        unsigned regIndex = ((modrm >> 3) & 7) | (rex.r << 3);
        uint64_t regValue = reg(cpu, regIndex);
        if (rm.isRegister) {
            uint64_t other = reg(cpu, rm.registerIndex);
            writeOperandSized(memory, cpu, rm, size, regValue);
            writeOperandSized(memory, cpu, RmOperand{true, regIndex, 0}, size, other);
            return true;
        }
        uint64_t old = 0;
        if (!guestAtomicRmw(memory, rm.address, AtomicOp::Exchange, regValue, size, old))
            return false;
        writeOperandSized(memory, cpu, RmOperand{true, regIndex, 0}, size, old);
        return true;
    }

    // ---- MOV: C6 /0 (mov r/m8, imm8) ----
    if (opcode == 0xc6) {
        uint8_t modrm = 0;
        if (!decoder.byte(modrm)) return false;
        RmOperand rm;
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm, segBase)) return false;
        uint8_t imm = 0;
        if (!decoder.immediate(imm)) return false;
        return writeOperandSized(memory, cpu, rm, 1, static_cast<uint64_t>(imm));
    }

    // ---- MOV: C7 /0 (mov r/m32, imm32) ----
    if (opcode == 0xc7) {
        uint8_t modrm = 0;
        if (!decoder.byte(modrm)) return false;
        const unsigned mod = modrm >> 6U;
        const unsigned rm = modrm & 7U;
        if (mod == 0 && rm == 5) {
            // RIP-relative: [rip + disp32]. decoder.next() is after
            // opcode+modrm+disp32 (start+6); correct RIP is start+10
            // (after imm32), so add the imm32 size (4) to compensate.
            int32_t disp32 = 0;
            if (!decoder.immediate(disp32)) return false;
            uint64_t addr = decoder.next() + static_cast<uint64_t>(disp32) + 4U;
            uint32_t imm = 0;
            if (!decoder.immediate(imm)) return false;
            if (rexW) {
                uint64_t value = static_cast<uint64_t>(static_cast<int64_t>(
                    static_cast<int32_t>(imm)));
                return writeGuest(memory, addr, &value, 8);
            }
            return writeGuest(memory, addr, &imm, 4);
        }
        RmOperand rmOp;
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rmOp, segBase)) return false;
        uint32_t imm = 0;
        if (!decoder.immediate(imm)) return false;
        if (rexW) {
            uint64_t value = static_cast<uint64_t>(static_cast<int64_t>(
                static_cast<int32_t>(imm)));
            return writeOperandSized(memory, cpu, rmOp, 8, value);
        }
        return writeOperandSized(memory, cpu, rmOp, 4, static_cast<uint64_t>(imm));
    }

    // ---- PUSH: 50+r (push r64) ----
    if (opcode >= 0x50 && opcode <= 0x57) {
        unsigned regIndex = (opcode & 7) | (rex.b << 3);
        uint64_t value = reg(cpu, regIndex);
        cpu.gpr[4] -= 8;
        if (!writeGuest(memory, cpu.gpr[4], &value, 8)) return false;
        return true;
    }

    // ---- FF /0 (inc r/m), FF /1 (dec r/m), FF /2 (call r/m64), FF /6 (push r/m64) ----
    if (opcode == 0xff) {
        uint8_t modrm = 0;
        if (!decoder.byte(modrm)) return false;
        const unsigned regField = (modrm >> 3) & 7;
        if (regField == 0 || regField == 1) { // inc / dec
            RmOperand rm;
            if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm, segBase)) return false;
            const bool inc = (regField == 0);
            const uint64_t savedCarry = cpu.rflags & kFlagCarry;
            uint64_t old = 0;
            if (lockPrefix && !rm.isRegister) {
                if (!guestAtomicRmw(memory, rm.address,
                                    inc ? AtomicOp::Add : AtomicOp::Sub, 1, opSize, old))
                    return false;
            } else {
                if (!readOperandSized(memory, cpu, rm, opSize, old)) return false;
            }
            uint64_t result = inc ? old + 1 : old - 1;
            if (!(lockPrefix && !rm.isRegister)) {
                if (!writeOperandSized(memory, cpu, rm, opSize, result)) return false;
            }
            updateArithmeticFlags(cpu, result, old, 1, !inc, opSize);
            cpu.rflags = (cpu.rflags & ~kFlagCarry) | savedCarry; // INC/DEC keep CF
            return true;
        }
        if (regField == 2) { // call r/m64
            RmOperand rm;
            if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm, segBase)) return false;
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
            if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm, segBase)) return false;
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
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm, segBase)) return false;
        uint64_t src = reg(cpu, ((modrm >> 3) & 7) | (rex.r << 3));
        if (lockPrefix && !rm.isRegister) {
            uint64_t old = 0;
            if (!guestAtomicRmw(memory, rm.address, AtomicOp::Xor, src, opSize, old)) return false;
            uint64_t result = old ^ src;
            updateLogicalFlags(cpu, result, opSize);
            return true;
        }
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
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm, segBase)) return false;
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
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm, segBase)) return false;
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
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm, segBase)) return false;
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
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm, segBase)) return false;
        uint64_t src = reg(cpu, ((modrm >> 3) & 7) | (rex.r << 3));
        if (lockPrefix && !rm.isRegister) {
            uint64_t old = 0;
            if (!guestAtomicRmw(memory, rm.address, AtomicOp::Add, src, opSize, old)) return false;
            uint64_t result = old + src;
            updateArithmeticFlags(cpu, result, old, src, false, opSize);
            return true;
        }
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
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm, segBase)) return false;
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
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm, segBase)) return false;
        int8_t imm8 = 0;
        if (!decoder.immediate(imm8)) return false;
        int64_t imm = imm8;
    if (regField == 0) {
        if (lockPrefix && !rm.isRegister) {
            uint64_t old = 0;
            if (!guestAtomicRmw(memory, rm.address, AtomicOp::Add,
                                static_cast<uint64_t>(imm), opSize, old))
                return false;
            uint64_t result = old + static_cast<uint64_t>(imm);
            updateArithmeticFlags(cpu, result, old, static_cast<uint64_t>(imm),
                                  false, opSize);
            return true;
        }
        uint64_t dst = 0;
        if (!readOperandSized(memory, cpu, rm, opSize, dst)) return false;
        uint64_t result = dst + static_cast<uint64_t>(imm);
        if (!writeOperandSized(memory, cpu, rm, opSize, result)) return false;
        updateArithmeticFlags(cpu, result, dst, static_cast<uint64_t>(imm), false, opSize);
        return true;
    }
    if (regField == 5) { // sub r/m, imm8
        if (lockPrefix && !rm.isRegister) {
            uint64_t old = 0;
            if (!guestAtomicRmw(memory, rm.address, AtomicOp::Sub,
                                static_cast<uint64_t>(imm), opSize, old))
                return false;
            uint64_t result = old - static_cast<uint64_t>(imm);
            updateArithmeticFlags(cpu, result, old, static_cast<uint64_t>(imm),
                                  true, opSize);
            return true;
        }
        uint64_t dst = 0;
        if (!readOperandSized(memory, cpu, rm, opSize, dst)) return false;
        uint64_t result = dst - static_cast<uint64_t>(imm);
        if (!writeOperandSized(memory, cpu, rm, opSize, result)) return false;
        updateArithmeticFlags(cpu, result, dst, static_cast<uint64_t>(imm), true, opSize);
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
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm, segBase)) return false;
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
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm, segBase)) return false;
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
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm, segBase)) return false;
        uint64_t src = reg(cpu, ((modrm >> 3) & 7) | (rex.r << 3));
        if (lockPrefix && !rm.isRegister) {
            uint64_t old = 0;
            if (!guestAtomicRmw(memory, rm.address, AtomicOp::Or, src, opSize, old)) return false;
            uint64_t result = old | src;
            updateLogicalFlags(cpu, result, opSize);
            return true;
        }
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
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm, segBase)) return false;
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
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm, segBase)) return false;
        uint64_t src = reg(cpu, ((modrm >> 3) & 7) | (rex.r << 3));
        if (lockPrefix && !rm.isRegister) {
            uint64_t old = 0;
            if (!guestAtomicRmw(memory, rm.address, AtomicOp::And, src, opSize, old)) return false;
            uint64_t result = old & src;
            updateLogicalFlags(cpu, result, opSize);
            return true;
        }
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
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm, segBase)) return false;
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
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm, segBase)) return false;
        uint64_t src = reg(cpu, ((modrm >> 3) & 7) | (rex.r << 3));
        uint64_t dst = 0;
        if (!readOperandSized(memory, cpu, rm, opSize, dst)) return false;
        uint64_t result = dst & src;
        updateLogicalFlags(cpu, result, opSize);
        return true;
    }

    // ---- String instructions: MOVS/CMPS/SCAS/LODS/STOS (with REP prefixes) ----
    if (opcode >= 0xa4 && opcode <= 0xaf) {
        bool isMovs = (opcode == 0xa4 || opcode == 0xa5);
        bool isCmps = (opcode == 0xa6 || opcode == 0xa7);
        bool isScas = (opcode == 0xae || opcode == 0xaf);
        bool isLods = (opcode == 0xac || opcode == 0xad);
        bool isStos = (opcode == 0xaa || opcode == 0xab);

        size_t size = (opcode == 0xa4 || opcode == 0xa6 || opcode == 0xae ||
                       opcode == 0xac || opcode == 0xaa) ? 1 :
                      (rexW ? 8 : (opSizeOverride == 2 ? 2 : 4));
        bool df = (cpu.rflags & (1ULL << 10)) != 0;
        const bool repeated = repPrefix != 0;
        if (repeated && cpu.gpr[1] == 0) return true;

        do {
            if (isMovs) {
                uint64_t src = 0;
                if (!readGuest(memory, cpu.gpr[6], &src, size)) return false;
                if (!writeGuest(memory, cpu.gpr[7], &src, size)) return false;
                cpu.gpr[6] += df ? -(int64_t)size : (int64_t)size;
                cpu.gpr[7] += df ? -(int64_t)size : (int64_t)size;
            } else if (isCmps) {
                uint64_t src = 0, dst = 0;
                if (!readGuest(memory, cpu.gpr[6], &src, size)) return false;
                if (!readGuest(memory, cpu.gpr[7], &dst, size)) return false;
                updateArithmeticFlags(cpu, dst - src, dst, src, true, size);
                cpu.gpr[6] += df ? -(int64_t)size : (int64_t)size;
                cpu.gpr[7] += df ? -(int64_t)size : (int64_t)size;
            } else if (isScas) {
                uint64_t mask = (size == 8) ? ~0ULL : ((1ULL << (size * 8)) - 1);
                uint64_t al = cpu.gpr[0] & mask;
                uint64_t dst = 0;
                if (!readGuest(memory, cpu.gpr[7], &dst, size)) return false;
                updateArithmeticFlags(cpu, al - dst, al, dst, true, size);
                cpu.gpr[7] += df ? -(int64_t)size : (int64_t)size;
            } else if (isLods) {
                uint64_t value = 0;
                if (!readGuest(memory, cpu.gpr[6], &value, size)) return false;
                if (size == 8) cpu.gpr[0] = value;
                else if (size == 4) cpu.gpr[0] = static_cast<uint32_t>(value);
                else if (size == 2) cpu.gpr[0] = static_cast<uint16_t>(value);
                else cpu.gpr[0] = static_cast<uint8_t>(value);
                cpu.gpr[6] += df ? -(int64_t)size : (int64_t)size;
            } else if (isStos) {
                uint64_t mask = (size == 8) ? ~0ULL : ((1ULL << (size * 8)) - 1);
                uint64_t value = cpu.gpr[0] & mask;
                if (!writeGuest(memory, cpu.gpr[7], &value, size)) return false;
                cpu.gpr[7] += df ? -(int64_t)size : (int64_t)size;
            }

            if (repeated) cpu.gpr[1] -= 1;

            if (!repeated) break;
            if (cpu.gpr[1] == 0) break;
            if (repPrefix == 1) {
                if (isMovs || isLods || isStos) continue;
                if ((cpu.rflags & kFlagZero) == 0) break;
                continue;
            }
            if (repPrefix == 2) {
                if (isMovs || isLods || isStos) continue;
                if ((cpu.rflags & kFlagZero) != 0) break;
                continue;
            }
        } while (true);

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
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm, segBase)) return false;
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
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm, segBase)) return false;
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
        // ---- CPUID: 0F A2 ----
        if (opcode2 == 0xa2) {
            const uint32_t leaf = static_cast<uint32_t>(cpu.gpr[0]);
            uint32_t a = 0, b = 0, c = 0, d = 0;
            if (leaf == 0) {
                a = 1;  // highest basic leaf
                std::memcpy(&b, "ROTT", 4);
                std::memcpy(&d, "R-RE", 4);
                std::memcpy(&c, "COMP", 4);
            } else if (leaf == 1) {
                a = 0x00000663;                       // family/model/stepping
                b = (1U << 16) | (8U << 8);           // 1 logical CPU, 64-byte line
                c = 0;                                // no unimplemented ECX features
                d = 0;                                // no unimplemented EDX features
            } else if (leaf == 0x80000000U) {
                a = 0x80000000U;                      // no extended leaves
            }
            cpu.gpr[0] = a;
            cpu.gpr[3] = b;
            cpu.gpr[1] = c;
            cpu.gpr[2] = d;
            return true;
        }
        // ---- RDTSC: 0F 31 ----
        if (opcode2 == 0x31) {
            const uint64_t tsc = nextTsc();
            cpu.gpr[0] = static_cast<uint32_t>(tsc);        // EAX, zero-extended
            cpu.gpr[2] = static_cast<uint32_t>(tsc >> 32);  // EDX, zero-extended
            return true;
        }
        // ---- XGETBV / XSETBV: 0F 01 /2, /3 ----
        if (opcode2 == 0x01) {
            uint8_t modrm = 0;
            if (!decoder.byte(modrm)) return false;
            if (modrm == 0xd0) { // XGETBV
                const uint32_t index = static_cast<uint32_t>(cpu.gpr[1]);
                if (index != 0) return false;  // invalid XCR -> #GP
                // x87/SSE/AVX state is not implemented, so XCR0 advertises 0.
                cpu.gpr[0] = 0;
                cpu.gpr[2] = 0;
                return true;
            }
            return false;
        }
        // ---- Fences: 0F AE F0 (MFENCE), 0F AE E8 (LFENCE), 0F AE F8 (SFENCE) ----
        if (opcode2 == 0xae) {
            uint8_t modrm = 0;
            if (!decoder.byte(modrm)) return false;
            if (modrm == 0xf0) { // MFENCE
                std::atomic_thread_fence(std::memory_order_seq_cst);
                return true;
            }
            if (modrm == 0xe8) { // LFENCE
                std::atomic_thread_fence(std::memory_order_acquire);
                return true;
            }
            if (modrm == 0xf8) { // SFENCE
                std::atomic_thread_fence(std::memory_order_release);
                return true;
            }
            return false;
        }
        // ---- MOVZX/MOVSX: 0F B6/B7/BE/BF ----
        uint8_t modrm = 0;
        if (!decoder.byte(modrm)) return false;
        RmOperand rm;
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm, segBase)) return false;
        unsigned dstReg = ((modrm >> 3) & 7) | (rex.r << 3);
        // ---- XADD: 0F C0/C1 (xadd r/m, r) ----
        if (opcode2 == 0xc0 || opcode2 == 0xc1) {
            const size_t size = (opcode2 == 0xc0) ? 1 : opSize;
            uint64_t src = 0;
            if (!readOperandSized(memory, cpu, RmOperand{true, dstReg, 0}, size, src))
                return false;
            uint64_t old = 0;
            if (!rm.isRegister) {
                if (!guestAtomicRmw(memory, rm.address, AtomicOp::Add, src, size, old))
                    return false;
            } else {
                if (!readOperandSized(memory, cpu, rm, size, old)) return false;
                if (!writeOperandSized(memory, cpu, rm, size, old + src)) return false;
            }
            if (!writeOperandSized(memory, cpu, RmOperand{true, dstReg, 0}, size, old))
                return false;
            updateArithmeticFlags(cpu, old + src, old, src, false, size);
            return true;
        }
        // ---- CMPXCHG: 0F B0/B1 (cmpxchg r/m, r) ----
        if (opcode2 == 0xb0 || opcode2 == 0xb1) {
            const size_t size = (opcode2 == 0xb0) ? 1 : opSize;
            uint64_t src = 0;
            if (!readOperandSized(memory, cpu, RmOperand{true, dstReg, 0}, size, src))
                return false;
            uint64_t acc = 0;
            if (!readOperandSized(memory, cpu, RmOperand{true, 0, 0}, size, acc))
                return false;
            uint64_t old = 0;
            bool matched = false;
            if (!rm.isRegister) {
                if (!guestAtomicCmpxchg(memory, rm.address, acc, src, size, old, matched))
                    return false;
            } else {
                if (!readOperandSized(memory, cpu, rm, size, old)) return false;
                matched = (old == acc);
                if (matched && !writeOperandSized(memory, cpu, rm, size, src)) return false;
            }
            if (matched) {
                cpu.rflags |= kFlagZero;
            } else {
                cpu.rflags &= ~kFlagZero;
                if (!writeOperandSized(memory, cpu, RmOperand{true, 0, 0}, size, old))
                    return false;
            }
            return true;
        }
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
        if (!decodeRmOperand(decoder, cpu, modrm, rex.b, rex.x, rm, segBase)) return false;
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

        DecodedInstruction instruction;
        if (!decodePrefixesAndOpcode(decoder, instruction)) {
            result.reason = X64StopReason::InvalidMemory;
            result.faultAddress = decoder.next();
            break;
        }

        if (instruction.opcode == 0xf4) { cpu.rip = decoder.next(); result.reason = X64StopReason::Halted; executed++; break; }
        if (instruction.opcode == 0x90) { cpu.rip = decoder.next(); executed++; continue; }
        if (instruction.opcode == 0xcc) { result.reason = X64StopReason::InvalidCpuState; result.faultAddress = instStart; break; }

        bool ripUpdated = false;
        X64StopReason stopReason = X64StopReason::UnsupportedInstruction;
        uint64_t segBase = instruction.segmentOverride ? cpu.gsBase : 0;
        bool ok = executeInstruction(
                cpu, memory, decoder, instruction.rex, instruction.opcode,
                instruction.opSizeOverride, instruction.repPrefix, segBase,
                instruction.lockPrefix, ripUpdated, stopReason);
        if (!ok) {
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
