#include "rottr/arm64_emitter.h"

#include <cassert>

namespace rottr {
namespace arm64 {
namespace {

uint32_t reg(uint32_t value) {
    assert(value < 32);
    return value & 31U;
}

uint32_t shifted(uint32_t base, uint32_t rd, uint32_t rn, uint32_t rm,
                 Shift shift, unsigned amount) {
    assert(amount < 64);
    return base | (static_cast<uint32_t>(shift) << 22) | (reg(rm) << 16) |
           ((amount & 63U) << 10) | (reg(rn) << 5) | reg(rd);
}

uint32_t branchImmediate(int64_t byteOffset, uint32_t base, unsigned bits) {
    assert((byteOffset % 4) == 0);
    const int64_t words = byteOffset / 4;
    const int64_t limit = static_cast<int64_t>(1) << (bits - 1);
    assert(words >= -limit && words < limit);
    const uint32_t mask = (1U << bits) - 1U;
    return base | (static_cast<uint32_t>(words) & mask);
}

uint32_t loadStore(uint32_t base, uint32_t rt, uint32_t rn, unsigned scaled,
                   unsigned maximum) {
    assert(scaled <= maximum);
    return base | ((scaled & 0xfffU) << 10) | (reg(rn) << 5) | reg(rt);
}

}  // namespace

uint32_t nop() { return 0xd503201fU; }

uint32_t ret(uint32_t rn) { return 0xd65f0000U | (reg(rn) << 5); }

uint32_t br(uint32_t rn) { return 0xd61f0000U | (reg(rn) << 5); }

uint32_t blr(uint32_t rn) { return 0xd63f0000U | (reg(rn) << 5); }

uint32_t svc(uint16_t immediate) {
    return 0xd4000001U | (static_cast<uint32_t>(immediate) << 5);
}

uint32_t brk(uint16_t immediate) {
    return 0xd4200000U | (static_cast<uint32_t>(immediate) << 5);
}

uint32_t movReg(uint32_t rd, uint32_t rm) {
    return 0xaa0003e0U | (reg(rm) << 16) | reg(rd);
}

uint32_t movz(uint32_t rd, uint16_t immediate, unsigned shift) {
    assert((shift % 16) == 0 && shift <= 48);
    return 0xd2800000U | ((shift / 16U) << 21) |
           (static_cast<uint32_t>(immediate) << 5) | reg(rd);
}

uint32_t movn(uint32_t rd, uint16_t immediate, unsigned shift) {
    assert((shift % 16) == 0 && shift <= 48);
    return 0x92800000U | ((shift / 16U) << 21) |
           (static_cast<uint32_t>(immediate) << 5) | reg(rd);
}

uint32_t movk(uint32_t rd, uint16_t immediate, unsigned shift) {
    assert((shift % 16) == 0 && shift <= 48);
    return 0xf2800000U | ((shift / 16U) << 21) |
           (static_cast<uint32_t>(immediate) << 5) | reg(rd);
}

std::vector<uint32_t> movImm(uint32_t rd, uint64_t value) {
    std::vector<uint32_t> words;
    bool first = true;
    for (unsigned shift = 0; shift < 64; shift += 16) {
        const uint16_t part = static_cast<uint16_t>((value >> shift) & 0xffffU);
        if (part == 0 && !(first && value == 0)) continue;
        words.push_back(first ? movz(rd, part, shift) : movk(rd, part, shift));
        first = false;
    }
    if (words.empty()) words.push_back(movz(rd, 0, 0));
    return words;
}

uint32_t addImm(uint32_t rd, uint32_t rn, uint32_t immediate, unsigned shift) {
    assert(shift == 0 || shift == 12);
    assert(immediate < 4096);
    return 0x91000000U | ((shift == 12 ? 1U : 0U) << 22) |
           ((immediate & 0xfffU) << 10) | (reg(rn) << 5) | reg(rd);
}

uint32_t subImm(uint32_t rd, uint32_t rn, uint32_t immediate, unsigned shift) {
    assert(shift == 0 || shift == 12);
    assert(immediate < 4096);
    return 0xd1000000U | ((shift == 12 ? 1U : 0U) << 22) |
           ((immediate & 0xfffU) << 10) | (reg(rn) << 5) | reg(rd);
}

uint32_t cmpImm(uint32_t rn, uint32_t immediate, unsigned shift) {
    assert(shift == 0 || shift == 12);
    assert(immediate < 4096);
    return 0xf1000000U | ((shift == 12 ? 1U : 0U) << 22) |
           ((immediate & 0xfffU) << 10) | (reg(rn) << 5) | reg(kXzr);
}

uint32_t addReg(uint32_t rd, uint32_t rn, uint32_t rm, Shift shift,
                unsigned amount) {
    return shifted(0x8b000000U, rd, rn, rm, shift, amount);
}

uint32_t subReg(uint32_t rd, uint32_t rn, uint32_t rm, Shift shift,
                unsigned amount) {
    return shifted(0xcb000000U, rd, rn, rm, shift, amount);
}

uint32_t cmpReg(uint32_t rn, uint32_t rm, Shift shift, unsigned amount) {
    return shifted(0xeb000000U, kXzr, rn, rm, shift, amount);
}

uint32_t andReg(uint32_t rd, uint32_t rn, uint32_t rm, Shift shift,
                unsigned amount) {
    return shifted(0x8a000000U, rd, rn, rm, shift, amount);
}

uint32_t orrReg(uint32_t rd, uint32_t rn, uint32_t rm, Shift shift,
                unsigned amount) {
    return shifted(0xaa000000U, rd, rn, rm, shift, amount);
}

uint32_t eorReg(uint32_t rd, uint32_t rn, uint32_t rm, Shift shift,
                unsigned amount) {
    return shifted(0xca000000U, rd, rn, rm, shift, amount);
}

uint32_t lslImm(uint32_t rd, uint32_t rn, unsigned amount) {
    assert(amount < 64);
    const uint32_t immr = (64U - amount) & 63U;
    const uint32_t imms = 63U - amount;
    return 0xd3400000U | (immr << 16) | (imms << 10) | (reg(rn) << 5) | reg(rd);
}

uint32_t lsrImm(uint32_t rd, uint32_t rn, unsigned amount) {
    assert(amount < 64);
    return 0xd3400000U | (amount << 16) | (63U << 10) | (reg(rn) << 5) | reg(rd);
}

uint32_t asrImm(uint32_t rd, uint32_t rn, unsigned amount) {
    assert(amount < 64);
    return 0x93400000U | (amount << 16) | (63U << 10) | (reg(rn) << 5) | reg(rd);
}

uint32_t lslReg(uint32_t rd, uint32_t rn, uint32_t rm) {
    return 0x9ac02000U | (reg(rm) << 16) | (reg(rn) << 5) | reg(rd);
}

uint32_t lsrReg(uint32_t rd, uint32_t rn, uint32_t rm) {
    return 0x9ac02400U | (reg(rm) << 16) | (reg(rn) << 5) | reg(rd);
}

uint32_t asrReg(uint32_t rd, uint32_t rn, uint32_t rm) {
    return 0x9ac02800U | (reg(rm) << 16) | (reg(rn) << 5) | reg(rd);
}

uint32_t mul(uint32_t rd, uint32_t rn, uint32_t rm) {
    return 0x9b000000U | (reg(rm) << 16) | (reg(kXzr) << 10) | (reg(rn) << 5) |
           reg(rd);
}

uint32_t msub(uint32_t rd, uint32_t rn, uint32_t rm, uint32_t ra) {
    return 0x9b008000U | (reg(rm) << 16) | (reg(ra) << 10) | (reg(rn) << 5) |
           reg(rd);
}

uint32_t ldrX(uint32_t rt, uint32_t rn, unsigned byteOffset) {
    assert((byteOffset % 8) == 0);
    return loadStore(0xf9400000U, rt, rn, byteOffset / 8, 4095);
}

uint32_t strX(uint32_t rt, uint32_t rn, unsigned byteOffset) {
    assert((byteOffset % 8) == 0);
    return loadStore(0xf9000000U, rt, rn, byteOffset / 8, 4095);
}

uint32_t ldrW(uint32_t rt, uint32_t rn, unsigned byteOffset) {
    assert((byteOffset % 4) == 0);
    return loadStore(0xb9400000U, rt, rn, byteOffset / 4, 4095);
}

uint32_t strW(uint32_t rt, uint32_t rn, unsigned byteOffset) {
    assert((byteOffset % 4) == 0);
    return loadStore(0xb9000000U, rt, rn, byteOffset / 4, 4095);
}

uint32_t ldrb(uint32_t rt, uint32_t rn, unsigned byteOffset) {
    return loadStore(0x39400000U, rt, rn, byteOffset, 4095);
}

uint32_t strb(uint32_t rt, uint32_t rn, unsigned byteOffset) {
    return loadStore(0x39000000U, rt, rn, byteOffset, 4095);
}

uint32_t ldrh(uint32_t rt, uint32_t rn, unsigned byteOffset) {
    assert((byteOffset % 2) == 0);
    return loadStore(0x79400000U, rt, rn, byteOffset / 2, 4095);
}

uint32_t strh(uint32_t rt, uint32_t rn, unsigned byteOffset) {
    assert((byteOffset % 2) == 0);
    return loadStore(0x79000000U, rt, rn, byteOffset / 2, 4095);
}

uint32_t ldrsw(uint32_t rt, uint32_t rn, unsigned byteOffset) {
    assert((byteOffset % 4) == 0);
    return loadStore(0xb9800000U, rt, rn, byteOffset / 4, 4095);
}

uint32_t b(int64_t byteOffset) {
    return branchImmediate(byteOffset, 0x14000000U, 26);
}

uint32_t bl(int64_t byteOffset) {
    return branchImmediate(byteOffset, 0x94000000U, 26);
}

uint32_t bTo(uint64_t target, uint64_t pc) {
    return b(static_cast<int64_t>(target) - static_cast<int64_t>(pc));
}

uint32_t blTo(uint64_t target, uint64_t pc) {
    return bl(static_cast<int64_t>(target) - static_cast<int64_t>(pc));
}

uint32_t bCond(Condition condition, int64_t byteOffset) {
    assert(static_cast<uint8_t>(condition) < 16);
    assert((byteOffset % 4) == 0);
    const int64_t words = byteOffset / 4;
    assert(words >= -(1LL << 18) && words < (1LL << 18));
    return 0x54000000U | static_cast<uint32_t>(condition) |
           ((static_cast<uint32_t>(words) & 0x7ffffU) << 5);
}

uint32_t cbz(uint32_t rt, int64_t byteOffset) {
    assert((byteOffset % 4) == 0);
    const int64_t words = byteOffset / 4;
    assert(words >= -(1LL << 18) && words < (1LL << 18));
    return 0xb4000000U | ((static_cast<uint32_t>(words) & 0x7ffffU) << 5) |
           reg(rt);
}

uint32_t cbnz(uint32_t rt, int64_t byteOffset) {
    assert((byteOffset % 4) == 0);
    const int64_t words = byteOffset / 4;
    assert(words >= -(1LL << 18) && words < (1LL << 18));
    return 0xb5000000U | ((static_cast<uint32_t>(words) & 0x7ffffU) << 5) |
           reg(rt);
}

uint32_t tbz(uint32_t rt, unsigned bit, int64_t byteOffset) {
    assert(bit < 64);
    assert((byteOffset % 4) == 0);
    const int64_t words = byteOffset / 4;
    assert(words >= -8192 && words < 8192);
    return ((bit >> 5) << 31) | 0x36000000U | ((bit & 31U) << 19) |
           ((static_cast<uint32_t>(words) & 0x3fffU) << 5) | reg(rt);
}

uint32_t tbnz(uint32_t rt, unsigned bit, int64_t byteOffset) {
    return tbz(rt, bit, byteOffset) | 0x01000000U;
}

uint32_t adr(uint32_t rd, int64_t byteOffset) {
    assert(byteOffset >= -(1LL << 20) && byteOffset < (1LL << 20));
    const uint32_t value = static_cast<uint32_t>(byteOffset) & 0x1fffffU;
    return 0x10000000U | ((value & 3U) << 29) | ((value >> 2) << 5) | reg(rd);
}

uint32_t adrp(uint32_t rd, int64_t byteOffset) {
    assert((byteOffset % 4096) == 0);
    const int64_t pages = byteOffset / 4096;
    assert(pages >= -(1LL << 20) && pages < (1LL << 20));
    const uint32_t value = static_cast<uint32_t>(pages) & 0x1fffffU;
    return 0x90000000U | ((value & 3U) << 29) | ((value >> 2) << 5) | reg(rd);
}

}  // namespace arm64
}  // namespace rottr
