// Step 19: Separate instruction decoder from execution
// Extracted Decoder class from x64_interpreter.cpp
// Can be shared between interpreter and future JIT translator.

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

#include "rottr/x64_runtime.h"

namespace rottr {

constexpr size_t kMaximumInstructionLength = 15;

// Decoded instruction result
struct DecodedInstruction {
    uint64_t offset;       // Instruction offset (RIP relative)
    uint8_t  opcode;       // Raw opcode byte(s)
    uint8_t  opcode_len;   // Length of opcode in bytes
    
    // Operands
    struct Operand {
        uint8_t type;      // 0 = none, 1 = immediate, 2 = register, 3 = memory
        uint8_t reg;       // Register index (if register operand)
        uint64_t imm;      // Immediate value (if immediate operand)
        uint64_t address;  // Memory address (if memory operand)
    };
    Operand operands[4];   // Up to 4 operands x86-64 can have
    
    // Decoding metadata
    bool valid;            // Whether decoding succeeded
    uint8_t  num_operands; // Number of valid operands
};

// Decoder class - separates decoding from execution
class Decoder {
public:
    Decoder(const X64GuestMemory& memory, uint64_t ip) : memory_(memory), start_(ip), cursor_(ip) {}
    
    bool byte(uint8_t& value) {
        if (length() >= kMaximumInstructionLength ||
            !fetch(memory_, cursor_, &value, sizeof(value))) return false;
        ++cursor_;
        return true;
    }
    
    template <typename T>
    bool immediate(T& value) {
        if (length() > kMaximumInstructionLength ||
            sizeof(T) > kMaximumInstructionLength - length())
            return false;
        if (!fetch(memory_, cursor_, &value, sizeof(T))) return false;
        cursor_ += sizeof(T);
        return true;
    }    
    uint64_t next() const { return cursor_; }
    size_t length() const {
        return cursor_ >= start_ ? static_cast<size_t>(cursor_ - start_)
                                 : kMaximumInstructionLength + 1U;
    }
    
private:
    static bool fetch(const X64GuestMemory& memory, uint64_t address,
                      void* out, size_t size) {
        if (!memory.executable || memory.data == nullptr || address < memory.base)
            return false;
        const uint64_t offset = address - memory.base;
        if (offset > memory.size ||
            size > memory.size - static_cast<size_t>(offset))
            return false;
        std::memcpy(out, memory.data + static_cast<size_t>(offset), size);
        return true;
    }
    
    const X64GuestMemory& memory_;
    uint64_t start_;
    uint64_t cursor_;
};

} // namespace rottr
