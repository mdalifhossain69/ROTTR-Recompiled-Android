// Step 19: Decoder implementation (can be shared between interpreter and JIT)
#include "rottr/x64_decoder.h"

namespace rottr {

// Constructor - initialize with guest memory and start IP
Decoder::Decoder(const X64GuestMemory& memory, uint64_t ip)
    : memory_(memory), start_(ip), cursor_(ip) {}

// Fetch a single byte from guest memory
bool Decoder::byte(uint8_t& value) {
    if (cursor_ - start_ >= kMaximumInstructionLength || 
        !fetch(memory_, cursor_, &value, sizeof(value))) return false;
    ++cursor_;
    return true;
}

// Fetch immediate value of type T
template <typename T>
bool Decoder::immediate(T& value) {
    if (cursor_ - start_ > kMaximumInstructionLength || 
        sizeof(T) > kMaximumInstructionLength - (cursor_ - start_) ||
        !fetch(memory_, cursor_, &value, sizeof(T))) return false;
    cursor_ += sizeof(T);
    return true;
}

// Get next position (cursor)
uint64_t Decoder::next() const { return cursor_; }

// Get total decoded length
uint64_t Decoder::length() const { return cursor_ - start_; }

// Instantiate the decoder (explicit template instantiation)
template class Decoder;
