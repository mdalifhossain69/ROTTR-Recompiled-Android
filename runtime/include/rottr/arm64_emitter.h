// Roadmap step 46: AArch64 instruction emitter.
//
// Every helper returns the 32-bit little-endian instruction word. The host
// test assembles the equivalent source with llvm-mc and compares the bytes, so
// the emitter is checked against the LLVM assembler rather than hand values.
#pragma once

#include <cstdint>
#include <vector>

namespace rottr {
namespace arm64 {

constexpr uint32_t kSp = 31;
constexpr uint32_t kXzr = 31;
constexpr uint32_t kLr = 30;

enum class Shift : uint8_t { Lsl = 0, Lsr = 1, Asr = 2 };

enum class Condition : uint8_t {
    Eq = 0, Ne = 1, Cs = 2, Cc = 3, Mi = 4, Pl = 5, Vs = 6, Vc = 7,
    Hi = 8, Ls = 9, Ge = 10, Lt = 11, Gt = 12, Le = 13, Al = 14,
};

uint32_t nop();
uint32_t ret(uint32_t rn = kLr);
uint32_t br(uint32_t rn);
uint32_t blr(uint32_t rn);
uint32_t svc(uint16_t immediate);
uint32_t brk(uint16_t immediate);

uint32_t movReg(uint32_t rd, uint32_t rm);
uint32_t movz(uint32_t rd, uint16_t immediate, unsigned shift = 0);
uint32_t movn(uint32_t rd, uint16_t immediate, unsigned shift = 0);
uint32_t movk(uint32_t rd, uint16_t immediate, unsigned shift = 0);
// Materialises a full 64-bit constant as movz + movk (up to four words).
std::vector<uint32_t> movImm(uint32_t rd, uint64_t value);

uint32_t addImm(uint32_t rd, uint32_t rn, uint32_t immediate, unsigned shift = 0);
uint32_t subImm(uint32_t rd, uint32_t rn, uint32_t immediate, unsigned shift = 0);
uint32_t cmpImm(uint32_t rn, uint32_t immediate, unsigned shift = 0);

uint32_t addReg(uint32_t rd, uint32_t rn, uint32_t rm, Shift shift = Shift::Lsl,
                unsigned amount = 0);
uint32_t subReg(uint32_t rd, uint32_t rn, uint32_t rm, Shift shift = Shift::Lsl,
                unsigned amount = 0);
uint32_t subsReg(uint32_t rd, uint32_t rn, uint32_t rm, Shift shift = Shift::Lsl,
                 unsigned amount = 0);
uint32_t addsReg(uint32_t rd, uint32_t rn, uint32_t rm, Shift shift = Shift::Lsl,
                 unsigned amount = 0);
uint32_t cmpReg(uint32_t rn, uint32_t rm, Shift shift = Shift::Lsl,
                unsigned amount = 0);

uint32_t andReg(uint32_t rd, uint32_t rn, uint32_t rm, Shift shift = Shift::Lsl,
                unsigned amount = 0);
uint32_t orrReg(uint32_t rd, uint32_t rn, uint32_t rm, Shift shift = Shift::Lsl,
                unsigned amount = 0);
uint32_t eorReg(uint32_t rd, uint32_t rn, uint32_t rm, Shift shift = Shift::Lsl,
                unsigned amount = 0);

uint32_t lslImm(uint32_t rd, uint32_t rn, unsigned amount);
uint32_t lsrImm(uint32_t rd, uint32_t rn, unsigned amount);
uint32_t asrImm(uint32_t rd, uint32_t rn, unsigned amount);
uint32_t lslReg(uint32_t rd, uint32_t rn, uint32_t rm);
uint32_t lsrReg(uint32_t rd, uint32_t rn, uint32_t rm);
uint32_t asrReg(uint32_t rd, uint32_t rn, uint32_t rm);

uint32_t mul(uint32_t rd, uint32_t rn, uint32_t rm);
uint32_t msub(uint32_t rd, uint32_t rn, uint32_t rm, uint32_t ra);

uint32_t ldrX(uint32_t rt, uint32_t rn, unsigned byteOffset);
uint32_t strX(uint32_t rt, uint32_t rn, unsigned byteOffset);
uint32_t ldrW(uint32_t rt, uint32_t rn, unsigned byteOffset);
uint32_t strW(uint32_t rt, uint32_t rn, unsigned byteOffset);
uint32_t ldrb(uint32_t rt, uint32_t rn, unsigned byteOffset);
uint32_t strb(uint32_t rt, uint32_t rn, unsigned byteOffset);
uint32_t ldrh(uint32_t rt, uint32_t rn, unsigned byteOffset);
uint32_t strh(uint32_t rt, uint32_t rn, unsigned byteOffset);
uint32_t ldrsw(uint32_t rt, uint32_t rn, unsigned byteOffset);

uint32_t b(int64_t byteOffset);
uint32_t bl(int64_t byteOffset);
uint32_t bTo(uint64_t target, uint64_t pc);
uint32_t blTo(uint64_t target, uint64_t pc);
uint32_t bCond(Condition condition, int64_t byteOffset);
uint32_t cbz(uint32_t rt, int64_t byteOffset);
uint32_t cbnz(uint32_t rt, int64_t byteOffset);
uint32_t tbz(uint32_t rt, unsigned bit, int64_t byteOffset);
uint32_t tbnz(uint32_t rt, unsigned bit, int64_t byteOffset);

uint32_t adr(uint32_t rd, int64_t byteOffset);
uint32_t adrp(uint32_t rd, int64_t byteOffset);

uint32_t mrsNzcv(uint32_t rt);
uint32_t msrNzcv(uint32_t rt);
uint32_t cset(uint32_t rd, Condition condition);

}  // namespace arm64
}  // namespace rottr
