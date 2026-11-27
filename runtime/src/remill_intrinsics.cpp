// Native implementations of the Remill runtime intrinsics used by lifted code.
// Signatures follow remill/Arch/Runtime/Intrinsics.h. `State` and `Memory` are
// opaque here, so this file builds without the Remill headers.

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "rottr/remill_runtime.h"

extern "C" {

rottr_events rottr_run_events;

void rottr_memory_reset(rottr_memory *memory) {
    memory->count = 0;
}

int rottr_memory_add(rottr_memory *memory, uint64_t base, uint64_t size,
                     void *host, int writable) {
    if (memory->count >= ROTTR_MAX_REGIONS || size == 0 || host == nullptr ||
        base + (size - 1) < base) {
        return -1;
    }
    memory->regions[memory->count++] = {base, size, static_cast<uint8_t *>(host), writable};
    return 0;
}

uint8_t *rottr_memory_at(rottr_memory *memory, uint64_t address, size_t size,
                         int is_write) {
    for (int i = 0; i < memory->count; ++i) {
        const rottr_region &r = memory->regions[i];
        if (address >= r.base && size <= r.size && address - r.base <= r.size - size &&
            (!is_write || r.writable)) {
            return r.host + (address - r.base);
        }
    }
    if (memory->fault) {
        memory->fault(address, size, is_write);
    }
    std::abort();
}

}  // extern "C"

namespace {

template <typename T>
T load(void *memory, uint64_t address) {
    T value;
    std::memcpy(&value, rottr_memory_at(static_cast<rottr_memory *>(memory), address,
                                        sizeof(T), 0), sizeof(T));
    return value;
}

template <typename T>
void *store(void *memory, uint64_t address, T value) {
    std::memcpy(rottr_memory_at(static_cast<rottr_memory *>(memory), address,
                                sizeof(T), 1), &value, sizeof(T));
    return memory;
}

// Single-threaded only: these do not make the operation atomic between threads.
template <typename T>
void *compare_exchange(void *memory, uint64_t address, T &expected, T desired) {
    const T current = load<T>(memory, address);
    if (current == expected) {
        store<T>(memory, address, desired);
    }
    expected = current;
    return memory;
}

}  // namespace

extern "C" {

// ---- Memory -------------------------------------------------------------
#define ROTTR_ACCESS(suffix, type)                                              \
    type __remill_read_memory_##suffix(void *m, uint64_t a) { return load<type>(m, a); } \
    void *__remill_write_memory_##suffix(void *m, uint64_t a, type v) { return store<type>(m, a, v); }
ROTTR_ACCESS(8, uint8_t)
ROTTR_ACCESS(16, uint16_t)
ROTTR_ACCESS(32, uint32_t)
ROTTR_ACCESS(64, uint64_t)
ROTTR_ACCESS(f32, float)
ROTTR_ACCESS(f64, double)
#undef ROTTR_ACCESS

#define ROTTR_CMPXCHG(bits)                                                     \
    void *__remill_compare_exchange_memory_##bits(void *m, uint64_t a,          \
                                                  uint##bits##_t &expected,     \
                                                  uint##bits##_t desired) {     \
        return compare_exchange<uint##bits##_t>(m, a, expected, desired);       \
    }
ROTTR_CMPXCHG(8)
ROTTR_CMPXCHG(16)
ROTTR_CMPXCHG(32)
ROTTR_CMPXCHG(64)
#undef ROTTR_CMPXCHG

void *__remill_barrier_load_load(void *m) { return m; }
void *__remill_barrier_load_store(void *m) { return m; }
void *__remill_barrier_store_load(void *m) { return m; }
void *__remill_barrier_store_store(void *m) { return m; }
void *__remill_atomic_begin(void *m) { return m; }
void *__remill_atomic_end(void *m) { return m; }

// ---- Control flow -------------------------------------------------------
// Direct calls are lifted as calls to sub_<address>. Anything that arrives here
// other than a return left the lifted code, and is recorded as such.
void *__remill_function_return(void *, uint64_t pc, void *m) {
    rottr_run_events.returned = 1;
    rottr_run_events.return_pc = pc;
    return m;
}
void *__remill_function_call(void *, uint64_t, void *m) { ++rottr_run_events.escaped; return m; }
void *__remill_jump(void *, uint64_t, void *m) { ++rottr_run_events.escaped; return m; }
void *__remill_missing_block(void *, uint64_t, void *m) { ++rottr_run_events.escaped; return m; }
void *__remill_error(void *, uint64_t, void *m) { ++rottr_run_events.unsupported; return m; }
void *__remill_async_hyper_call(void *, uint64_t, void *m) { ++rottr_run_events.unsupported; return m; }
void *__remill_sync_hyper_call(void *, void *m, uint32_t) { ++rottr_run_events.unsupported; return m; }

// ---- Undefined values ---------------------------------------------------
uint8_t  __remill_undefined_8(void)  { ++rottr_run_events.undefined; return 0; }
uint16_t __remill_undefined_16(void) { ++rottr_run_events.undefined; return 0; }
uint32_t __remill_undefined_32(void) { ++rottr_run_events.undefined; return 0; }
uint64_t __remill_undefined_64(void) { ++rottr_run_events.undefined; return 0; }
float    __remill_undefined_f32(void) { ++rottr_run_events.undefined; return 0.0f; }
double   __remill_undefined_f64(void) { ++rottr_run_events.undefined; return 0.0; }

// ---- Flags and comparisons ----------------------------------------------
// The lifted code has already computed the result; these must return it.
bool __remill_flag_computation_zero(bool result, ...) { return result; }
bool __remill_flag_computation_sign(bool result, ...) { return result; }
bool __remill_flag_computation_overflow(bool result, ...) { return result; }
bool __remill_flag_computation_carry(bool result, ...) { return result; }

bool __remill_compare_eq(bool result) { return result; }
bool __remill_compare_neq(bool result) { return result; }
bool __remill_compare_slt(bool result) { return result; }
bool __remill_compare_sle(bool result) { return result; }
bool __remill_compare_sgt(bool result) { return result; }
bool __remill_compare_sge(bool result) { return result; }
bool __remill_compare_ult(bool result) { return result; }
bool __remill_compare_ule(bool result) { return result; }
bool __remill_compare_ugt(bool result) { return result; }
bool __remill_compare_uge(bool result) { return result; }

}  // extern "C"
