// runtime/src/remill_intrinsics.cpp
// Native aarch64 implementations of Remill runtime intrinsics.
// These replace the x86_64-only bitcode that Remill ships, allowing the
// lifted .o files to link and run on an aarch64 host.
//
// The `memory` pointer passed to read/write intrinsics is an opaque guest
// memory context managed by the shim (harness/shim.cpp). The shim provides
// a flat 1 MB buffer and passes it as the third argument to lifted functions.

#include <cstdint>
#include <cstring>

extern "C" {

// ---- Memory reads -------------------------------------------------------
// The `memory` argument is the guest memory buffer passed from the shim.
uint8_t  __remill_read_memory_8 (void* memory, uint64_t address) {
    uint8_t v; memcpy(&v, static_cast<uint8_t*>(memory) + (address & 0xFFFFF), 1); return v;
}
uint16_t __remill_read_memory_16(void* memory, uint64_t address) {
    uint16_t v; memcpy(&v, static_cast<uint8_t*>(memory) + (address & 0xFFFFF), 2); return v;
}
uint32_t __remill_read_memory_32(void* memory, uint64_t address) {
    uint32_t v; memcpy(&v, static_cast<uint8_t*>(memory) + (address & 0xFFFFF), 4); return v;
}
uint64_t __remill_read_memory_64(void* memory, uint64_t address) {
    uint64_t v; memcpy(&v, static_cast<uint8_t*>(memory) + (address & 0xFFFFF), 8); return v;
}
float    __remill_read_memory_f32(void* memory, uint64_t address) {
    float v; memcpy(&v, static_cast<uint8_t*>(memory) + (address & 0xFFFFF), 4); return v;
}

// ---- Memory writes ------------------------------------------------------
void* __remill_write_memory_8 (void* memory, uint64_t address, uint8_t  value) {
    memcpy(static_cast<uint8_t*>(memory) + (address & 0xFFFFF), &value, 1); return memory;
}
void* __remill_write_memory_16(void* memory, uint64_t address, uint16_t value) {
    memcpy(static_cast<uint8_t*>(memory) + (address & 0xFFFFF), &value, 2); return memory;
}
void* __remill_write_memory_32(void* memory, uint64_t address, uint32_t value) {
    memcpy(static_cast<uint8_t*>(memory) + (address & 0xFFFFF), &value, 4); return memory;
}
void* __remill_write_memory_64(void* memory, uint64_t address, uint64_t value) {
    memcpy(static_cast<uint8_t*>(memory) + (address & 0xFFFFF), &value, 8); return memory;
}
void* __remill_write_memory_f32(void* memory, uint64_t address, float value) {
    memcpy(static_cast<uint8_t*>(memory) + (address & 0xFFFFF), &value, 4); return memory;
}

// ---- Control flow stubs -------------------------------------------------
void* __remill_function_call  (void* state, uint64_t pc, void* memory) { return memory; }
void* __remill_function_return(void* state, uint64_t ra,  void* memory) { return memory; }
void* __remill_missing_block  (void* state, uint64_t pc, void* memory) { return memory; }
void* __remill_error          (void* state, uint64_t code, void* memory) { return memory; }
void* __remill_sync_hyper_call(void* state, void* memory, uint32_t vector) { return memory; }

// ---- Undefined value ----------------------------------------------------
uint8_t __remill_undefined_8(void) { return 0; }

// ---- Flag computations (varargs tail ignored) ---------------------------
uint8_t __remill_flag_computation_zero (uint8_t result, ...) { return result == 0; }
uint8_t __remill_flag_computation_carry(uint8_t result, ...) { return result != 0; }
uint8_t __remill_flag_computation_sign (uint8_t result, ...) { return (int8_t)result < 0; }
uint8_t __remill_flag_computation_overflow(uint8_t result, ...) { return 0; }

// ---- Comparisons --------------------------------------------------------
uint8_t __remill_compare_eq  (uint8_t result) { return result; }
uint8_t __remill_compare_neq (uint8_t result) { return !result; }
uint8_t __remill_compare_ugt (uint8_t result) { return result; }
uint8_t __remill_compare_ule (uint8_t result) { return !result; }

} // extern "C"