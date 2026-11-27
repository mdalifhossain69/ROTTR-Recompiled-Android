// Unit tests for the Remill runtime helpers. Needs no Remill, no game data.
#include <csetjmp>
#include <cstdint>
#include <cstdio>

#include "rottr/remill_runtime.h"

extern "C" {
bool __remill_flag_computation_zero(bool result, ...);
bool __remill_flag_computation_sign(bool result, ...);
bool __remill_flag_computation_overflow(bool result, ...);
bool __remill_flag_computation_carry(bool result, ...);
bool __remill_compare_eq(bool);
bool __remill_compare_neq(bool);
bool __remill_compare_slt(bool);
bool __remill_compare_sle(bool);
bool __remill_compare_sgt(bool);
bool __remill_compare_sge(bool);
bool __remill_compare_ult(bool);
bool __remill_compare_ule(bool);
bool __remill_compare_ugt(bool);
bool __remill_compare_uge(bool);
uint32_t __remill_read_memory_32(void *, uint64_t);
uint64_t __remill_read_memory_64(void *, uint64_t);
void *__remill_write_memory_32(void *, uint64_t, uint32_t);
void *__remill_compare_exchange_memory_32(void *, uint64_t, uint32_t &, uint32_t);
}

static int failures = 0;
#define CHECK(x)                                              \
    do {                                                      \
        if (!(x)) {                                           \
            std::printf("FAIL line %d: %s\n", __LINE__, #x);  \
            ++failures;                                       \
        }                                                     \
    } while (0)

static std::jmp_buf faulted;
static void on_fault(uint64_t, size_t, int) { std::longjmp(faulted, 1); }

static bool read_faults(rottr_memory *m, uint64_t address) {
    if (setjmp(faulted)) return true;
    (void)__remill_read_memory_64(m, address);
    return false;
}

static bool write_faults(rottr_memory *m, uint64_t address) {
    if (setjmp(faulted)) return true;
    __remill_write_memory_32(m, address, 1);
    return false;
}

int main() {
    for (int i = 0; i < 2; ++i) {
        const bool v = i != 0;
        CHECK(__remill_flag_computation_zero(v) == v);
        CHECK(__remill_flag_computation_sign(v) == v);
        CHECK(__remill_flag_computation_overflow(v) == v);
        CHECK(__remill_flag_computation_carry(v) == v);
        CHECK(__remill_compare_eq(v) == v);
        CHECK(__remill_compare_neq(v) == v);
        CHECK(__remill_compare_slt(v) == v);
        CHECK(__remill_compare_sle(v) == v);
        CHECK(__remill_compare_sgt(v) == v);
        CHECK(__remill_compare_sge(v) == v);
        CHECK(__remill_compare_ult(v) == v);
        CHECK(__remill_compare_ule(v) == v);
        CHECK(__remill_compare_ugt(v) == v);
        CHECK(__remill_compare_uge(v) == v);
    }

    uint8_t rw[16] = {0}, ro[16] = {0};
    rottr_memory m = {};
    m.fault = on_fault;
    CHECK(rottr_memory_add(&m, 0x1000, sizeof rw, rw, 1) == 0);
    CHECK(rottr_memory_add(&m, 0x140000000ULL, sizeof ro, ro, 0) == 0);
    CHECK(rottr_memory_add(&m, ~0ULL, 2, rw, 1) == -1);

    __remill_write_memory_32(&m, 0x100C, 0xAABBCCDDu);
    CHECK(rw[12] == 0xDD && rw[15] == 0xAA);
    CHECK(__remill_read_memory_32(&m, 0x100C) == 0xAABBCCDDu);
    /* Addresses above 1 MB are not truncated. */
    ro[0] = 7;
    CHECK(__remill_read_memory_32(&m, 0x140000000ULL) == 7u);

    CHECK(read_faults(&m, 0x1009));       /* runs past the end */
    CHECK(read_faults(&m, 0x101000));     /* same low 20 bits, unmapped */
    CHECK(read_faults(&m, ~0ULL - 3));    /* address overflow */
    CHECK(write_faults(&m, 0x140000000ULL)); /* read-only */
    CHECK(ro[0] == 7);

    uint32_t expected = 0xAABBCCDDu;
    __remill_compare_exchange_memory_32(&m, 0x100C, expected, 5u);
    CHECK(__remill_read_memory_32(&m, 0x100C) == 5u);
    expected = 99u;
    __remill_compare_exchange_memory_32(&m, 0x100C, expected, 6u);
    CHECK(expected == 5u && __remill_read_memory_32(&m, 0x100C) == 5u);

    if (!failures) std::printf("runtime helpers: ok\n");
    return failures != 0;
}
