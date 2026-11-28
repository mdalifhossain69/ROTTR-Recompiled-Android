// Bounded instruction-byte reader shared by the interpreter and translators.

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

#include "rottr/x64_runtime.h"

namespace rottr {

constexpr size_t kMaximumInstructionLength = 15;

class Decoder {
public:
    Decoder(const X64GuestMemory& memory, uint64_t ip);

    bool byte(uint8_t& value);

    template <typename T>
    bool immediate(T& value) {
        if (length() > kMaximumInstructionLength ||
            sizeof(T) > kMaximumInstructionLength - length())
            return false;
        if (!fetch(memory_, cursor_, &value, sizeof(T))) return false;
        cursor_ += sizeof(T);
        return true;
    }

    uint64_t next() const;
    size_t length() const;

private:
    static bool fetch(const X64GuestMemory& memory, uint64_t address,
                      void* out, size_t size);

    const X64GuestMemory& memory_;
    uint64_t start_;
    uint64_t cursor_;
};

} // namespace rottr
