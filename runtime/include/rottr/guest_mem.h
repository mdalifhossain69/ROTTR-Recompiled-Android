#ifndef ROTTR_GUEST_MEM_H
#define ROTTR_GUEST_MEM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* x86-64 guest page size managed by this module. */
#define ROTTR_GUEST_PAGE_SIZE 4096u
/* Windows allocation granularity. All reserved/committed regions are aligned
 * to this size. */
#define ROTTR_ALLOC_GRANULARITY 65536u

/* Default size of the reserved guest address space. */
#define ROTTR_GUEST_DEFAULT_RANGE_SIZE (1ull << 32) /* 4 GB */

/* Page permission bits. Tracked in software so 4 KB granularity is enforced
 * even on hosts with 16 KB pages (mprotect can only act on whole host
 * pages; the software check is the authoritative enforcement). */
#define ROTTR_MEM_NONE   0u
#define ROTTR_MEM_READ   1u
#define ROTTR_MEM_WRITE  2u
#define ROTTR_MEM_EXEC   4u

/* Region states. */
#define ROTTR_REGION_FREE      0u
#define ROTTR_REGION_RESERVED  1u
#define ROTTR_REGION_COMMITTED 2u

/* Result of a translation / access check. */
typedef struct {
    void *host;  /* host pointer for the guest address, or NULL on fault */
    int ok;      /* 1 if the access is permitted, 0 on any fault */
} rottr_mem_result;

/* Information about a region, mirroring Windows MEMORY_BASIC_INFORMATION. */
typedef struct {
    void *BaseAddress;
    void *AllocationBase;
    uint32_t AllocationProt;
    size_t RegionSize;
    uint32_t State;     /* ROTTR_REGION_FREE / RESERVED / COMMITTED */
    uint32_t Protect;
} rottr_mem_region_info;

/* A reserved guest address range. Opaque to callers. */
typedef struct rottr_guest_mem rottr_guest_mem;

/* Reserve a contiguous guest address range of `size` bytes with mmap. No
 * pages are committed. Returns NULL on failure. */
rottr_guest_mem *rottr_guest_reserve(size_t size);

/* Release a reserved range and all of its pages. */
void rottr_guest_release(rottr_guest_mem *mem);

/* Return the host base address of the reservation (for tests and low-level
 * access; the interpreter normally goes through rottr_guest_translate). */
void *rottr_guest_base(rottr_guest_mem *mem);

/* Reserve a sub-region with 64 KB allocation granularity. `addr` may be NULL
 * (the manager picks an address) or must be 64 KB aligned. `size` is rounded
 * up to 64 KB. Overlapping regions, unaligned bases and integer overflow are
 * rejected (return NULL). The region starts uncommitted. */
void *rottr_guest_reserve_region(rottr_guest_mem *mem, void *addr, size_t size);

/* Decommit and release a previously reserved region. The base must match a
 * region exactly and size must be 0; double free is rejected (returns -1). */
int rottr_guest_release_region(rottr_guest_mem *mem, void *addr, size_t size);

/* Query the state of the region containing `addr`. Returns 1 and fills
 * `info` when `addr` lies inside a known region, 0 otherwise (free address). */
int rottr_guest_query_region(rottr_guest_mem *mem, uintptr_t addr,
                             rottr_mem_region_info *info);

/* Commit or decommit 4 KB pages covering [addr, addr+size). `addr` and `size`
 * must be 4 KB aligned and inside the reserved range. Returns 0 on success,
 * -1 on fault. Committed pages always read zero. */
int rottr_guest_commit_pages(rottr_guest_mem *mem, uintptr_t addr, size_t size);
int rottr_guest_decommit_pages(rottr_guest_mem *mem, uintptr_t addr, size_t size);

/* Set permissions on 4 KB pages covering [addr, addr+size). Returns 0 on
 * success, -1 on fault or invalid permission bits. */
int rottr_guest_protect_pages(rottr_guest_mem *mem, uintptr_t addr, size_t size,
                              uint32_t prot);

/* Translate a guest address to a host pointer after a bounds check only
 * (no commit or permission requirement). Returns 1 on success, 0 if the
 * range lies outside the reservation. */
int rottr_guest_translate(rottr_guest_mem *mem, uintptr_t addr, size_t size,
                          rottr_mem_result *result);

/* Check that [addr, addr+size) is inside the reservation, committed and
 * allows `prot`. Fills `result.host` on success. Returns 1 on success,
 * 0 on any fault (out of range, uncommitted, or forbidden permission).
 * This is the authoritative check, so 4 KB protections are enforced
 * identically on 4 KB and 16 KB host pages. */
int rottr_guest_check(rottr_guest_mem *mem, uintptr_t addr, size_t size,
                      uint32_t prot, rottr_mem_result *result);

/* Read / write helpers: check permissions first, then copy. Return 0 on
 * success, -1 on fault. */
int rottr_guest_read(rottr_guest_mem *mem, uintptr_t addr, void *dst, size_t size);
int rottr_guest_write(rottr_guest_mem *mem, uintptr_t addr, const void *src,
                      size_t size);

/* ---- Windows API equivalents routed to the manager above (step 8) ---- */

#define ROTTR_MEM_COMMIT       0x1000u
#define ROTTR_MEM_RESERVE      0x2000u
#define ROTTR_MEM_DECOMMIT     0x4000u
#define ROTTR_MEM_RELEASE      0x8000u

#define ROTTR_PAGE_NOACCESS          0x01u
#define ROTTR_PAGE_READONLY          0x02u
#define ROTTR_PAGE_READWRITE         0x04u
#define ROTTR_PAGE_WRITECOPY         0x08u
#define ROTTR_PAGE_EXECUTE           0x10u
#define ROTTR_PAGE_EXECUTE_READ      0x20u
#define ROTTR_PAGE_EXECUTE_READWRITE 0x40u
#define ROTTR_PAGE_EXECUTE_WRITECOPY 0x80u

/* Windows-style error codes, matching GetLastError semantics. */
#define ROTTR_ERROR_SUCCESS             0u
#define ROTTR_ERROR_INVALID_PARAMETER   87u
#define ROTTR_ERROR_NOT_ENOUGH_MEMORY   8u
#define ROTTR_ERROR_ACCESS_DENIED       5u

/* The process-wide default guest address space used by the Virtual*
 * wrappers (the guest equivalent of the Windows process address space). */
rottr_guest_mem *rottr_guest_default_mem(void);

void *rottr_VirtualAlloc(void *addr, size_t size, uint32_t flAllocationType,
                         uint32_t flProt);
int rottr_VirtualFree(void *addr, size_t size, uint32_t flFreeType);
int rottr_VirtualProtect(void *addr, size_t size, uint32_t flNewProt,
                         uint32_t *lpflOldProt);
int rottr_VirtualQuery(const void *addr, rottr_mem_region_info *buffer,
                       size_t length);

uint32_t rottr_GetLastError(void);
void rottr_SetLastError(uint32_t error);

#ifdef __cplusplus
}
#endif
#endif /* ROTTR_GUEST_MEM_H */
