#include "rottr/arm64_block_translator.h"
#include "rottr/arm64_emitter.h"
#include "rottr/x64_runtime.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace rottr {
namespace arm64 {
namespace {

struct Decoder {
    const X64GuestMemory& mem;
    uint64_t start;
    uint64_t cursor;

    Decoder(const X64GuestMemory& m, uint64_t ip) : mem(m), start(ip), cursor(ip) {}

    bool fetch(void* out, size_t size) {
        if (cursor < mem.base) return false;
        const size_t off = static_cast<size_t>(cursor - mem.base);
        if (off + size > mem.size) return false;
        std::memcpy(out, mem.data + off, size);
        return true;
    }

    bool byte(uint8_t& v) {
        if (!fetch(&v, 1)) return false;
        ++cursor;
        return true;
    }

    template <typename T>
    bool imm(T& v) {
        if (!fetch(&v, sizeof(T))) return false;
        cursor += sizeof(T);
        return true;
    }

    uint64_t next() const { return cursor; }
};

struct Rex {
    bool w = false, r = false, x = false, b = false;
};

struct ModRM {
    uint8_t mod = 0, reg = 0, rm = 0;
    bool isRegister() const { return mod == 3; }
};

bool readModRM(Decoder& dec, ModRM& m) {
    uint8_t b = 0;
    if (!dec.byte(b)) return false;
    m.mod = b >> 6;
    m.reg = (b >> 3) & 7;
    m.rm = b & 7;
    return true;
}

constexpr uint64_t kRipOffset = offsetof(rottr_cpu, rip);
constexpr uint64_t kRflagsOffset = offsetof(rottr_cpu, rflags);

constexpr uint64_t kFlagCarry = 1ULL << 0;
constexpr uint64_t kFlagParity = 1ULL << 2;
constexpr uint64_t kFlagAuxiliary = 1ULL << 4;
constexpr uint64_t kFlagZero = 1ULL << 6;
constexpr uint64_t kFlagSign = 1ULL << 7;
constexpr uint64_t kFlagOverflow = 1ULL << 11;
constexpr uint64_t kArithmeticFlags = kFlagCarry | kFlagParity | kFlagAuxiliary |
                                      kFlagZero | kFlagSign | kFlagOverflow;

constexpr uint32_t TMP_RESULT = 9;
constexpr uint32_t TMP_A = 10;
constexpr uint32_t TMP_B = 11;
constexpr uint32_t TMP_T = 12;
constexpr uint32_t TMP_FLAGS = 13;
constexpr uint32_t TMP_EXPR = 14;
constexpr uint32_t TMP_MASK = 15;
constexpr uint32_t TMP_MASK16 = 8;

using Words = std::vector<uint32_t>;

void append(Words& code, const Words& more) {
    code.insert(code.end(), more.begin(), more.end());
}

Words loadA(unsigned reg) { return {ldrX(TMP_A, 0, 8ULL * reg)}; }
Words loadB(unsigned reg) { return {ldrX(TMP_B, 0, 8ULL * reg)}; }
Words storeResult(unsigned reg) { return {strX(TMP_RESULT, 0, 8ULL * reg)}; }

void emitParity(Words& code) {
    // x13 becomes 1 when the low byte of x9 has even parity, matching the x86 PF.
    append(code, movImm(TMP_MASK, 0xff));
    code.push_back(andReg(TMP_T, TMP_RESULT, TMP_MASK));
    code.push_back(lsrImm(TMP_FLAGS, TMP_T, 4));
    code.push_back(eorReg(TMP_T, TMP_T, TMP_FLAGS));
    code.push_back(lsrImm(TMP_FLAGS, TMP_T, 2));
    code.push_back(eorReg(TMP_T, TMP_T, TMP_FLAGS));
    code.push_back(lsrImm(TMP_FLAGS, TMP_T, 1));
    code.push_back(eorReg(TMP_T, TMP_T, TMP_FLAGS));
    append(code, movImm(TMP_MASK, 1));
    code.push_back(andReg(TMP_T, TMP_T, TMP_MASK));
    code.push_back(eorReg(TMP_T, TMP_T, TMP_MASK));
}

void emitZeroSignParity(Words& code) {
    code.push_back(eorReg(TMP_EXPR, TMP_EXPR, TMP_EXPR));
    append(code, movImm(TMP_MASK, 1));
    // ZF
    code.push_back(cmpImm(TMP_RESULT, 0));
    code.push_back(cset(TMP_T, Condition::Eq));
    code.push_back(lslImm(TMP_T, TMP_T, 6));
    code.push_back(orrReg(TMP_EXPR, TMP_EXPR, TMP_T));
    // SF
    code.push_back(lsrImm(TMP_T, TMP_RESULT, 63));
    code.push_back(andReg(TMP_T, TMP_T, TMP_MASK));
    code.push_back(lslImm(TMP_T, TMP_T, 7));
    code.push_back(orrReg(TMP_EXPR, TMP_EXPR, TMP_T));
    // PF
    emitParity(code);
    code.push_back(lslImm(TMP_T, TMP_T, 2));
    code.push_back(orrReg(TMP_EXPR, TMP_EXPR, TMP_T));
}

void emitMergeFlags(Words& code, bool preserveCarry) {
    uint64_t clearMask = kArithmeticFlags;
    if (preserveCarry) clearMask &= ~kFlagCarry;
    append(code, movImm(TMP_MASK, ~clearMask));
    code.push_back(ldrX(TMP_FLAGS, 0, kRflagsOffset));
    code.push_back(andReg(TMP_FLAGS, TMP_FLAGS, TMP_MASK));
    code.push_back(orrReg(TMP_FLAGS, TMP_FLAGS, TMP_EXPR));
    code.push_back(strX(TMP_FLAGS, 0, kRflagsOffset));
}

void emitArithmeticFlags(Words& code, bool sub, bool preserveCarry) {
    // x9 = result, x10 = operand1, x11 = operand2.
    code.push_back(sub ? subsReg(31, TMP_A, TMP_B) : addsReg(31, TMP_A, TMP_B));
    code.push_back(mrsNzcv(TMP_FLAGS));
    append(code, movImm(TMP_MASK, 1));
    append(code, movImm(TMP_MASK16, 0x10));
    code.push_back(eorReg(TMP_EXPR, TMP_EXPR, TMP_EXPR));

    // NZCV.C -> x86 CF (inverted for SUB). NZCV.Z/N/V -> x86 ZF/SF/OF.
    code.push_back(lsrImm(TMP_T, TMP_FLAGS, 29));
    code.push_back(andReg(TMP_T, TMP_T, TMP_MASK));
    if (sub) code.push_back(eorReg(TMP_T, TMP_T, TMP_MASK));
    code.push_back(orrReg(TMP_EXPR, TMP_EXPR, TMP_T));

    code.push_back(lsrImm(TMP_T, TMP_FLAGS, 30));
    code.push_back(andReg(TMP_T, TMP_T, TMP_MASK));
    code.push_back(lslImm(TMP_T, TMP_T, 6));
    code.push_back(orrReg(TMP_EXPR, TMP_EXPR, TMP_T));

    code.push_back(lsrImm(TMP_T, TMP_FLAGS, 31));
    code.push_back(andReg(TMP_T, TMP_T, TMP_MASK));
    code.push_back(lslImm(TMP_T, TMP_T, 7));
    code.push_back(orrReg(TMP_EXPR, TMP_EXPR, TMP_T));

    code.push_back(lsrImm(TMP_T, TMP_FLAGS, 28));
    code.push_back(andReg(TMP_T, TMP_T, TMP_MASK));
    code.push_back(lslImm(TMP_T, TMP_T, 11));
    code.push_back(orrReg(TMP_EXPR, TMP_EXPR, TMP_T));

    // AF: bit 4 of (operand1 ^ operand2 ^ result).
    code.push_back(eorReg(TMP_T, TMP_A, TMP_B));
    code.push_back(eorReg(TMP_T, TMP_T, TMP_RESULT));
    code.push_back(andReg(TMP_T, TMP_T, TMP_MASK16));
    code.push_back(lsrImm(TMP_T, TMP_T, 4));
    code.push_back(andReg(TMP_T, TMP_T, TMP_MASK));
    code.push_back(lslImm(TMP_T, TMP_T, 4));
    code.push_back(orrReg(TMP_EXPR, TMP_EXPR, TMP_T));

    // PF.
    emitParity(code);
    code.push_back(lslImm(TMP_T, TMP_T, 2));
    code.push_back(orrReg(TMP_EXPR, TMP_EXPR, TMP_T));

    emitMergeFlags(code, preserveCarry);
}

void emitLogicalFlags(Words& code) {
    emitZeroSignParity(code);
    emitMergeFlags(code, false);
}

void emitBitTest(Words& code, uint32_t out, uint64_t bitMask, bool wantSet) {
    append(code, movImm(TMP_MASK, bitMask));
    code.push_back(andReg(out, TMP_FLAGS, TMP_MASK));
    code.push_back(cmpImm(out, 0));
    code.push_back(cset(out, wantSet ? Condition::Ne : Condition::Eq));
}

void emitCondition(Words& code, unsigned condition) {
    // x9 holds x86 RFLAGS on entry; result is a 0/1 value in TMP_A.
    code.push_back(ldrX(TMP_FLAGS, 0, kRflagsOffset));
    switch (condition & 15U) {
        case 0: emitBitTest(code, TMP_A, kFlagOverflow, true); break;
        case 1: emitBitTest(code, TMP_A, kFlagOverflow, false); break;
        case 2: emitBitTest(code, TMP_A, kFlagCarry, true); break;
        case 3: emitBitTest(code, TMP_A, kFlagCarry, false); break;
        case 4: emitBitTest(code, TMP_A, kFlagZero, true); break;
        case 5: emitBitTest(code, TMP_A, kFlagZero, false); break;
        case 6:
            emitBitTest(code, TMP_A, kFlagCarry, true);
            emitBitTest(code, TMP_B, kFlagZero, true);
            code.push_back(orrReg(TMP_A, TMP_A, TMP_B));
            break;
        case 7:
            emitBitTest(code, TMP_A, kFlagCarry, false);
            emitBitTest(code, TMP_B, kFlagZero, false);
            code.push_back(andReg(TMP_A, TMP_A, TMP_B));
            break;
        case 8: emitBitTest(code, TMP_A, kFlagSign, true); break;
        case 9: emitBitTest(code, TMP_A, kFlagSign, false); break;
        case 10: emitBitTest(code, TMP_A, kFlagParity, true); break;
        case 11: emitBitTest(code, TMP_A, kFlagParity, false); break;
        case 12:
            emitBitTest(code, TMP_A, kFlagSign, true);
            emitBitTest(code, TMP_B, kFlagOverflow, true);
            code.push_back(eorReg(TMP_A, TMP_A, TMP_B));
            break;
        case 13:
            emitBitTest(code, TMP_A, kFlagSign, true);
            emitBitTest(code, TMP_B, kFlagOverflow, true);
            code.push_back(eorReg(TMP_A, TMP_A, TMP_B));
            code.push_back(cmpImm(TMP_A, 0));
            code.push_back(cset(TMP_A, Condition::Eq));
            break;
        case 14:
            emitBitTest(code, TMP_A, kFlagZero, true);
            emitBitTest(code, TMP_B, kFlagSign, true);
            emitBitTest(code, TMP_T, kFlagOverflow, true);
            code.push_back(eorReg(TMP_B, TMP_B, TMP_T));
            code.push_back(orrReg(TMP_A, TMP_A, TMP_B));
            break;
        default:
            emitBitTest(code, TMP_A, kFlagZero, true);
            emitBitTest(code, TMP_B, kFlagSign, true);
            emitBitTest(code, TMP_T, kFlagOverflow, true);
            code.push_back(eorReg(TMP_B, TMP_B, TMP_T));
            code.push_back(cmpImm(TMP_B, 0));
            code.push_back(cset(TMP_B, Condition::Eq));
            code.push_back(cmpImm(TMP_A, 0));
            code.push_back(cset(TMP_A, Condition::Eq));
            code.push_back(andReg(TMP_A, TMP_A, TMP_B));
            break;
    }
}

void emitConditionalBranch(Words& code, unsigned condition, uint64_t target,
                           uint64_t nextRip) {
    emitCondition(code, condition);
    const size_t cbzIndex = code.size();
    code.push_back(cbz(TMP_A, 0));
    append(code, movImm(TMP_T, target));
    const size_t branchIndex = code.size();
    code.push_back(b(0));
    const size_t notTakenIndex = code.size();
    append(code, movImm(TMP_T, nextRip));
    const size_t afterIndex = code.size();
    code[cbzIndex] = cbz(TMP_A, static_cast<int64_t>(notTakenIndex - cbzIndex) * 4);
    code[branchIndex] = b(static_cast<int64_t>(afterIndex - branchIndex) * 4);
    code.push_back(strX(TMP_T, 0, kRipOffset));
}

void emitUnconditionalBranch(Words& code, uint64_t target) {
    append(code, movImm(TMP_T, target));
    code.push_back(strX(TMP_T, 0, kRipOffset));
}

}  // namespace

TranslationResult translateBasicBlock(const X64GuestMemory& memory,
                                      uint64_t rip,
                                      size_t maxInstructions) {
    TranslationResult result;
    result.startRip = rip;
    Words code;

    Decoder dec(memory, rip);
    size_t emitted = 0;
    bool hitUnsupported = false;
    const char* unsupportedReason = nullptr;
    uint64_t lastValidRip = rip;
    bool dynamicRip = false;

    auto markUnsupported = [&](const char* why) {
        hitUnsupported = true;
        unsupportedReason = why;
    };

    while (emitted < maxInstructions) {
        uint8_t first = 0;
        if (!dec.byte(first)) {
            result.status = TranslationResult::Status::InvalidMemory;
            result.detail = "cannot fetch opcode";
            return result;
        }

        Rex rex{};
        bool hasPrefix = false;
        while (first == 0x64 || first == 0x65 || first == 0x66 ||
               first == 0xf0 || first == 0xf2 || first == 0xf3) {
            if (first != 0x66) { markUnsupported("legacy prefix"); break; }
            hasPrefix = true;
            if (!dec.byte(first)) {
                result.status = TranslationResult::Status::InvalidMemory;
                result.detail = "cannot fetch after prefix";
                return result;
            }
        }
        if (hitUnsupported) break;
        if ((first & 0xf0) == 0x40) {
            rex.w = (first & 0x08) != 0;
            rex.r = (first & 0x04) != 0;
            rex.x = (first & 0x02) != 0;
            rex.b = (first & 0x01) != 0;
            if (!dec.byte(first)) {
                result.status = TranslationResult::Status::InvalidMemory;
                result.detail = "cannot fetch after REX";
                return result;
            }
        }

        auto need64 = [&]() {
            if (!rex.w || hasPrefix) {
                markUnsupported("only 64-bit register forms are translated");
                return false;
            }
            return true;
        };

        if (first == 0x90 && !rex.w && !hasPrefix) {
            code.push_back(nop());
            lastValidRip = dec.next();
            ++emitted;
            continue;
        }

        if (first == 0xc3 && !rex.w && !hasPrefix) {
            markUnsupported("RET requires interpreter stack semantics");
            break;
        }

        if (first >= 0xb8 && first <= 0xbf && !hasPrefix) {
            const unsigned reg = (first & 7) | (rex.r << 3);
            if (rex.w) {
                uint64_t value = 0;
                if (!dec.imm(value)) { markUnsupported("mov imm64 fetch"); break; }
                append(code, movImm(TMP_RESULT, value));
            } else {
                uint32_t value = 0;
                if (!dec.imm(value)) { markUnsupported("mov imm32 fetch"); break; }
                append(code, movImm(TMP_RESULT, value));
            }
            append(code, storeResult(reg));
            lastValidRip = dec.next();
            ++emitted;
            continue;
        }

        if ((first == 0x89 || first == 0x8b) && !hasPrefix) {
            ModRM m;
            if (!readModRM(dec, m)) { markUnsupported("mov modrm"); break; }
            if (!m.isRegister()) { markUnsupported("mov memory operand"); break; }
            const unsigned src = first == 0x89 ? ((m.reg) | (rex.r << 3))
                                               : (m.rm | (rex.b << 3));
            const unsigned dst = first == 0x89 ? (m.rm | (rex.b << 3))
                                               : ((m.reg) | (rex.r << 3));
            append(code, loadA(src));
            append(code, storeResult(dst));
            lastValidRip = dec.next();
            ++emitted;
            continue;
        }

        bool handledArithmetic = false;
        if (first == 0x01 || first == 0x29 || first == 0x03 || first == 0x2b ||
            first == 0x09 || first == 0x0b || first == 0x21 || first == 0x23 ||
            first == 0x31 || first == 0x33 || first == 0x39 || first == 0x3b ||
            first == 0x85) {
            if (!need64()) break;
            ModRM m;
            if (!readModRM(dec, m)) { markUnsupported("arithmetic modrm"); break; }
            if (!m.isRegister()) { markUnsupported("arithmetic memory operand"); break; }
            bool regIsDst = (first == 0x03 || first == 0x2b || first == 0x0b ||
                             first == 0x23 || first == 0x33 || first == 0x3b);
            const unsigned dst = regIsDst ? ((m.reg) | (rex.r << 3))
                                          : (m.rm | (rex.b << 3));
            const unsigned src = regIsDst ? (m.rm | (rex.b << 3))
                                          : ((m.reg) | (rex.r << 3));
            append(code, loadA(dst));
            append(code, loadB(src));

            bool compareOnly = (first == 0x39 || first == 0x3b || first == 0x85);
            if (first == 0x01 || first == 0x03) {
                code.push_back(addReg(TMP_RESULT, TMP_A, TMP_B));
            } else if (first == 0x29 || first == 0x2b || first == 0x39 || first == 0x3b) {
                code.push_back(subReg(TMP_RESULT, TMP_A, TMP_B));
            } else if (first == 0x09 || first == 0x0b || first == 0x85) {
                code.push_back(andReg(TMP_RESULT, TMP_A, TMP_B));
            } else if (first == 0x31 || first == 0x33) {
                code.push_back(eorReg(TMP_RESULT, TMP_A, TMP_B));
            } else {
                code.push_back(andReg(TMP_RESULT, TMP_A, TMP_B));
            }
            if (!compareOnly) append(code, storeResult(dst));
            if (first == 0x09 || first == 0x0b || first == 0x21 || first == 0x23 ||
                first == 0x31 || first == 0x33 || first == 0x85) {
                emitLogicalFlags(code);
            } else {
                emitArithmeticFlags(code, true, false);
            }
            lastValidRip = dec.next();
            ++emitted;
            handledArithmetic = true;
        }
        if (handledArithmetic) continue;

        if (first == 0x83 && !hasPrefix) {
            if (!need64()) break;
            ModRM m;
            if (!readModRM(dec, m)) { markUnsupported("imm8 modrm"); break; }
            if (!m.isRegister()) { markUnsupported("imm8 memory operand"); break; }
            uint8_t raw = 0;
            if (!dec.byte(raw)) { markUnsupported("imm8 fetch"); break; }
            const int64_t value = static_cast<int8_t>(raw);
            const unsigned dst = m.rm | (rex.b << 3);
            append(code, loadA(dst));
            append(code, movImm(TMP_B, static_cast<uint64_t>(value)));
            if (m.reg == 0) {
                code.push_back(addReg(TMP_RESULT, TMP_A, TMP_B));
                append(code, storeResult(dst));
                emitArithmeticFlags(code, false, false);
            } else if (m.reg == 5 || m.reg == 7) {
                code.push_back(subReg(TMP_RESULT, TMP_A, TMP_B));
                if (m.reg == 5) append(code, storeResult(dst));
                emitArithmeticFlags(code, true, false);
            } else {
                markUnsupported("imm8 /reg");
                break;
            }
            lastValidRip = dec.next();
            ++emitted;
            continue;
        }

        if (first == 0xff && !hasPrefix) {
            if (!need64()) break;
            ModRM m;
            if (!readModRM(dec, m)) { markUnsupported("ff modrm"); break; }
            if (!m.isRegister()) { markUnsupported("inc/dec memory operand"); break; }
            if (m.reg != 0 && m.reg != 1) { markUnsupported("ff /reg"); break; }
            const unsigned dst = m.rm | (rex.b << 3);
            append(code, loadA(dst));
            append(code, movImm(TMP_B, 1));
            code.push_back(m.reg == 0 ? addReg(TMP_RESULT, TMP_A, TMP_B)
                                      : subReg(TMP_RESULT, TMP_A, TMP_B));
            append(code, storeResult(dst));
            emitArithmeticFlags(code, m.reg == 1, true);
            lastValidRip = dec.next();
            ++emitted;
            continue;
        }

        if (first >= 0x70 && first <= 0x7f && !rex.w && !hasPrefix) {
            int8_t rel = 0;
            if (!dec.imm(rel)) { markUnsupported("jcc rel8"); break; }
            const uint64_t next = dec.next();
            const uint64_t target = static_cast<uint64_t>(
                static_cast<int64_t>(next) + static_cast<int64_t>(rel));
            emitConditionalBranch(code, first & 15U, target, next);
            lastValidRip = next;
            dynamicRip = true;
            ++emitted;
            break;
        }

        if ((first == 0xeb || first == 0xe9) && !rex.w && !hasPrefix) {
            uint64_t target = 0;
            if (first == 0xeb) {
                int8_t rel = 0;
                if (!dec.imm(rel)) { markUnsupported("jmp rel8"); break; }
                target = static_cast<uint64_t>(static_cast<int64_t>(dec.next()) + rel);
            } else {
                int32_t rel = 0;
                if (!dec.imm(rel)) { markUnsupported("jmp rel32"); break; }
                target = static_cast<uint64_t>(static_cast<int64_t>(dec.next()) + rel);
            }
            emitUnconditionalBranch(code, target);
            lastValidRip = dec.next();
            dynamicRip = true;
            ++emitted;
            break;
        }

        if (first == 0x0f && !rex.w && !hasPrefix) {
            uint8_t second = 0;
            if (!dec.byte(second)) { markUnsupported("0f opcode"); break; }
            if (second >= 0x80 && second <= 0x8f) {
                int32_t rel = 0;
                if (!dec.imm(rel)) { markUnsupported("jcc rel32"); break; }
                const uint64_t next = dec.next();
                const uint64_t target = static_cast<uint64_t>(
                    static_cast<int64_t>(next) + static_cast<int64_t>(rel));
                emitConditionalBranch(code, second & 15U, target, next);
                lastValidRip = next;
                dynamicRip = true;
                ++emitted;
                break;
            }
            markUnsupported("0f opcode not translated");
            break;
        }

        markUnsupported("opcode not implemented");
        break;
    }

    result.endRip = lastValidRip;
    result.instructionCount = emitted;
    if (!hitUnsupported && emitted > 0) {
        if (!dynamicRip) {
            append(code, movImm(TMP_T, result.endRip));
            code.push_back(strX(TMP_T, 0, kRipOffset));
        }
        code.push_back(ret());
    }
    result.code = std::move(code);
    if (hitUnsupported || emitted == 0) {
        result.status = TranslationResult::Status::Unsupported;
        result.detail = unsupportedReason ? unsupportedReason : "no instructions decoded";
        result.code.clear();
    } else {
        result.status = TranslationResult::Status::Translated;
    }
    return result;
}

}  // namespace arm64
}  // namespace rottr