#ifndef ROTTR_GUEST_H
#define ROTTR_GUEST_H
#include <stdint.h>

#ifdef __cplusplus
#define ROTTR_ALIGNAS_16 alignas(16)
extern "C" {
#else
#define ROTTR_ALIGNAS_16 _Alignas(16)
#endif

/* x86-64 register state that lifted code will operate on. */
typedef struct {
    uint64_t gpr[16];
    uint64_t rip;
    uint64_t rflags;
    uint64_t gsBase;   /* GS segment base (Windows TEB pointer) */
    ROTTR_ALIGNAS_16 uint8_t xmm[16][16];
} rottr_cpu;

void rottr_cpu_reset(rottr_cpu *cpu, uint64_t entry, uint64_t stack_top);

#ifdef __cplusplus
}
#endif
#undef ROTTR_ALIGNAS_16
#endif
