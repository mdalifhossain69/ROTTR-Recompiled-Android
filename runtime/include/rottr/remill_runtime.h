/* Host side of the Remill runtime: guest memory and per-run events. */
#ifndef ROTTR_REMILL_RUNTIME_H
#define ROTTR_REMILL_RUNTIME_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROTTR_MAX_REGIONS 16

typedef struct {
    uint64_t base;   /* guest address */
    uint64_t size;
    uint8_t *host;   /* backing bytes */
    int writable;
} rottr_region;

/* Lifted code receives a pointer to this as its opaque `Memory *`. */
typedef struct rottr_memory {
    rottr_region regions[ROTTR_MAX_REGIONS];
    int count;
    /* Called on an unmapped or read-only access. Must not return. */
    void (*fault)(uint64_t address, size_t size, int is_write);
} rottr_memory;

/* What happened during a run, apart from register and memory changes. */
typedef struct {
    uint32_t undefined;   /* uses of a value x86 leaves undefined */
    uint32_t escaped;     /* control left the lifted code */
    uint32_t unsupported; /* trap, hyper call or CPU error */
    uint32_t returned;    /* a `ret` was reached */
    uint64_t return_pc;   /* target of the last `ret` */
} rottr_events;

extern rottr_events rottr_run_events;

void rottr_memory_reset(rottr_memory *memory);
/* Returns 0 on success, -1 if the table is full or the range is invalid. */
int rottr_memory_add(rottr_memory *memory, uint64_t base, uint64_t size,
                     void *host, int writable);
/* Host pointer for [address, address + size), or calls the fault handler. */
uint8_t *rottr_memory_at(rottr_memory *memory, uint64_t address, size_t size,
                         int is_write);

#ifdef __cplusplus
}
#endif
#endif
