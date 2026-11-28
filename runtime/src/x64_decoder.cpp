#include "rottr/x64_decoder.h"

#include <cstring>

namespace rottr {

Decoder::Decoder(const X64GuestMemory& memory, uint64_t ip)
    : memory_(memory), start_(ip), cursor_(ip) {}

bool Decoder::fetch(const X64GuestMemory& memory, uint64_t address,
                    void* out, size_t size) {
    if (!memory.executable || memory.data == nullptr || address < memory.base)
        return false;
    const uint64_t offset = address - memory.base;
    if (offset > memory.size) return false;
    const size_t index = static_cast<size_t>(offset);
    if (size > memory.size - index) return false;
    std::memcpy(out, memory.data + index, size);
    return true;
}

bool Decoder::byte(uint8_t& value) {
    if (length() >= kMaximumInstructionLength ||
        !fetch(memory_, cursor_, &value, sizeof(value)))
        return false;
    ++cursor_;
    return true;
}

uint64_t Decoder::next() const {
    return cursor_;
}

size_t Decoder::length() const {
    return cursor_ >= start_ ? static_cast<size_t>(cursor_ - start_)
                             : kMaximumInstructionLength + 1U;
}

bool decodeRmOperand(Decoder& decoder, const rottr_cpu& cpu, uint8_t modrm,
                     unsigned rexB, unsigned rexX, RmOperand& operand,
                     uint64_t segBase) {
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

}  // namespace rottr
