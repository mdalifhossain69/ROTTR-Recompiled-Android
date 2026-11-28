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

}  // namespace rottr
