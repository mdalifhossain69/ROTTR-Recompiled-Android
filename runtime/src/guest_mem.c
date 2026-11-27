#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "rottr/guest_mem.h"

/* Maximum number of tracked sub-regions inside one reservation. */
#define ROTTR_MAX_REGIONS 4096

struct rottr_guest_mem {
    void *base;                 /* mmap'd host address of the reservation */
    size_t size;                /* reservation size in bytes */
    size_t page_count;          /* size / ROTTR_GUEST_PAGE_SIZE */
    uint8_t *state;             /* per 4 KB page: committed or not */
    uint8_t *prot;              /* per 4 KB page: permission bits */
    rottr_mem_region_info regions[ROTTR_MAX_REGIONS];
    size_t region_count;
};

static size_t host_page_size(void)
{
    long ps = sysconf(_SC_PAGESIZE);
    return ps > 0 ? (size_t)ps : ROTTR_GUEST_PAGE_SIZE;
}

/* Best effort enforcement at host page granularity. The software page table
 * is the authoritative check, so this running on a 16 KB host page system is
 * just as correct as on a 4 KB host system. */
static void apply_mprotect(rottr_guest_mem *mem, size_t first_page,
                           size_t last_page, uint32_t prot)
{
    size_t hps = host_page_size();
    uintptr_t start = (uintptr_t)mem->base + first_page * ROTTR_GUEST_PAGE_SIZE;
    uintptr_t end = (uintptr_t)mem->base + (last_page + 1) * ROTTR_GUEST_PAGE_SIZE;
    uintptr_t mstart = start & ~(uintptr_t)(hps - 1);
    uintptr_t mend = (end + hps - 1) & ~(uintptr_t)(hps - 1);
    int mprot = PROT_NONE;
    if (prot & ROTTR_MEM_READ)  mprot |= PROT_READ;
    if (prot & ROTTR_MEM_WRITE) mprot |= PROT_WRITE;
    if (prot & ROTTR_MEM_EXEC)  mprot |= PROT_EXEC;
    if (mend > mstart) {
        (void)mprotect((void *)mstart, mend - mstart, mprot);
    }
}

/* Check that [addr, addr+size) lies inside the reservation, returning the
 * first and last 4 KB page indices. Overflow-safe. */
static int check_range(const rottr_guest_mem *mem, uintptr_t addr, size_t size,
                       size_t *first, size_t *last)
{
    uintptr_t base = (uintptr_t)mem->base;
    if (size == 0 || addr < base) {
        return -1;
    }
    if (addr - base > mem->size - size) {
        return -1;
    }
    *first = (addr - base) / ROTTR_GUEST_PAGE_SIZE;
    *last = (addr - base + size - 1) / ROTTR_GUEST_PAGE_SIZE;
    return 0;
}

rottr_guest_mem *rottr_guest_reserve(size_t size)
{
    rottr_guest_mem *mem;
    size_t page_count;

    if (size == 0) {
        return NULL;
    }
    page_count = (size + ROTTR_GUEST_PAGE_SIZE - 1) / ROTTR_GUEST_PAGE_SIZE;

    mem = calloc(1, sizeof(*mem));
    if (mem == NULL) {
        return NULL;
    }
    /* Windows addresses are 64 KB aligned. mmap only guarantees host page
     * alignment (4 KB here, 16 KB on some Android devices), so reserve an
     * extra granularity and keep the aligned middle of the mapping. */
    {
        const size_t raw_size = size + ROTTR_ALLOC_GRANULARITY;
        void *raw = mmap(NULL, raw_size, PROT_NONE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        uintptr_t aligned;
        if (raw == MAP_FAILED) {
            free(mem);
            return NULL;
        }
        aligned = ((uintptr_t)raw + ROTTR_ALLOC_GRANULARITY - 1) &
                  ~(uintptr_t)(ROTTR_ALLOC_GRANULARITY - 1);
        if (aligned > (uintptr_t)raw)
            munmap(raw, (size_t)(aligned - (uintptr_t)raw));
        {
            const uintptr_t tail = aligned + size;
            const uintptr_t raw_end = (uintptr_t)raw + raw_size;
            if (raw_end > tail)
                munmap((void *)tail, (size_t)(raw_end - tail));
        }
        mem->base = (void *)aligned;
    }
    mem->state = calloc(page_count, sizeof(*mem->state));
    mem->prot = calloc(page_count, sizeof(*mem->prot));
    if (mem->state == NULL || mem->prot == NULL) {
        munmap(mem->base, size);
        free(mem->state);
        free(mem->prot);
        free(mem);
        return NULL;
    }
    mem->size = size;
    mem->page_count = page_count;
    return mem;
}

void rottr_guest_release(rottr_guest_mem *mem)
{
    if (mem == NULL) {
        return;
    }
    if (mem->base != NULL) {
        munmap(mem->base, mem->size);
    }
    free(mem->state);
    free(mem->prot);
    free(mem);
}

void *rottr_guest_base(rottr_guest_mem *mem)
{
    return mem == NULL ? NULL : mem->base;
}

/* Find the region whose range contains `addr`. Returns -1 when none. */
static int find_region(const rottr_guest_mem *mem, uintptr_t addr)
{
    size_t i;
    for (i = 0; i < mem->region_count; i++) {
        uintptr_t base = (uintptr_t)mem->regions[i].BaseAddress;
        if (addr >= base && addr < base + mem->regions[i].RegionSize) {
            return (int)i;
        }
    }
    return -1;
}

/* True when [a, a+asz) and [b, b+bsz) overlap. */
static int regions_overlap(uintptr_t a, size_t asz, uintptr_t b, size_t bsz)
{
    return a < b + bsz && b < a + asz;
}

static size_t align_up64k(size_t size)
{
    return (size + ROTTR_ALLOC_GRANULARITY - 1) &
           ~(ROTTR_ALLOC_GRANULARITY - 1);
}

void *rottr_guest_reserve_region(rottr_guest_mem *mem, void *addr, size_t size)
{
    size_t alloc_size;
    uintptr_t region_base;

    if (mem->region_count >= ROTTR_MAX_REGIONS) {
        return NULL;
    }
    if (size == 0) {
        return NULL;
    }
    alloc_size = align_up64k(size);
    if (alloc_size == 0 || alloc_size < size) {
        return NULL; /* size overflowed 64 KB alignment */
    }

    if (addr != NULL) {
        if (((uintptr_t)addr & (ROTTR_ALLOC_GRANULARITY - 1)) != 0 ||
            (uintptr_t)addr < (uintptr_t)mem->base) {
            return NULL; /* unaligned base or before the reservation */
        }
        /* region_base is an offset inside the reservation, not a host address. */
        region_base = (uintptr_t)addr - (uintptr_t)mem->base;
    } else if (mem->region_count == 0) {
        region_base = 0;
    } else {
        uintptr_t last_end = (uintptr_t)mem->regions[mem->region_count - 1]
                                 .BaseAddress -
                             (uintptr_t)mem->base;
        last_end += mem->regions[mem->region_count - 1].RegionSize;
        region_base = align_up64k(last_end);
    }
    if (region_base > mem->size || alloc_size > mem->size - region_base) {
        return NULL; /* integer overflow / out of range */
    }
    {
        uintptr_t region_base_host = (uintptr_t)mem->base + region_base;
        size_t i;
        for (i = 0; i < mem->region_count; i++) {
            uintptr_t rb = (uintptr_t)mem->regions[i].BaseAddress;
            if (regions_overlap(region_base_host, alloc_size, rb,
                                mem->regions[i].RegionSize)) {
                return NULL; /* overlap */
            }
        }
    }

    mem->regions[mem->region_count].BaseAddress = (void *)((uintptr_t)mem->base + region_base);
    mem->regions[mem->region_count].AllocationBase = (void *)((uintptr_t)mem->base + region_base);
    mem->regions[mem->region_count].AllocationProt = ROTTR_MEM_NONE;
    mem->regions[mem->region_count].RegionSize = alloc_size;
    mem->regions[mem->region_count].State = ROTTR_REGION_RESERVED;
    mem->regions[mem->region_count].Protect = ROTTR_MEM_NONE;
    mem->region_count++;
    return (void *)((uintptr_t)mem->base + region_base);
}

int rottr_guest_release_region(rottr_guest_mem *mem, void *addr, size_t size)
{
    int idx;
    size_t i;

    if (size != 0 || addr == NULL) {
        return -1;
    }
    idx = find_region(mem, (uintptr_t)addr);
    if (idx < 0 ||
        (uintptr_t)addr !=
            (uintptr_t)mem->regions[idx].BaseAddress) {
        return -1; /* not a known region base, or double free */
    }

    /* Decommit the pages the region touched. */
    {
        uintptr_t off = (uintptr_t)mem->regions[idx].BaseAddress -
                        (uintptr_t)mem->base;
        size_t first = off / ROTTR_GUEST_PAGE_SIZE;
        size_t last = (off + mem->regions[idx].RegionSize - 1) /
                      ROTTR_GUEST_PAGE_SIZE;
        for (i = first; i <= last; i++) {
            mem->state[i] = ROTTR_REGION_FREE;
            mem->prot[i] = ROTTR_MEM_NONE;
        }
        apply_mprotect(mem, first, last, ROTTR_MEM_NONE);
    }

    mem->region_count--;
    for (i = (size_t)idx; i < mem->region_count; i++) {
        mem->regions[i] = mem->regions[i + 1];
    }
    return 0;
}

int rottr_guest_query_region(rottr_guest_mem *mem, uintptr_t addr,
                             rottr_mem_region_info *info)
{
    int idx = find_region(mem, addr);
    if (idx < 0) {
        return 0;
    }
    *info = mem->regions[idx];
    info->RegionSize -= addr - (uintptr_t)mem->regions[idx].BaseAddress;
    return 1;
}

int rottr_guest_commit_pages(rottr_guest_mem *mem, uintptr_t addr, size_t size)
{
    size_t first, last, i;

    if (check_range(mem, addr, size, &first, &last) != 0) {
        return -1;
    }
    if (((addr & (ROTTR_GUEST_PAGE_SIZE - 1)) != 0) ||
        (size & (ROTTR_GUEST_PAGE_SIZE - 1)) != 0) {
        return -1;
    }
    for (i = first; i <= last; i++) {
        mem->state[i] = ROTTR_REGION_COMMITTED;
        if (mem->prot[i] == ROTTR_MEM_NONE) {
            mem->prot[i] = ROTTR_MEM_READ | ROTTR_MEM_WRITE;
        }
    }
    /* Zero pages under read+write, then apply the final protection. */
    apply_mprotect(mem, first, last, ROTTR_MEM_READ | ROTTR_MEM_WRITE);
    {
        uintptr_t host = (uintptr_t)mem->base + (addr - (uintptr_t)mem->base);
        memset((void *)host, 0, size);
    }
    apply_mprotect(mem, first, last,
                   mem->prot[first]); /* best effort; software check is authoritative */
    return 0;
}

int rottr_guest_decommit_pages(rottr_guest_mem *mem, uintptr_t addr,
                               size_t size)
{
    size_t first, last, i;

    if (check_range(mem, addr, size, &first, &last) != 0) {
        return -1;
    }
    if (((addr & (ROTTR_GUEST_PAGE_SIZE - 1)) != 0) ||
        (size & (ROTTR_GUEST_PAGE_SIZE - 1)) != 0) {
        return -1;
    }
    for (i = first; i <= last; i++) {
        mem->state[i] = ROTTR_REGION_FREE;
        mem->prot[i] = ROTTR_MEM_NONE;
    }
    apply_mprotect(mem, first, last, ROTTR_MEM_NONE);
    return 0;
}

int rottr_guest_protect_pages(rottr_guest_mem *mem, uintptr_t addr,
                              size_t size, uint32_t prot)
{
    size_t first, last, i;

    if (prot & ~(ROTTR_MEM_READ | ROTTR_MEM_WRITE | ROTTR_MEM_EXEC)) {
        return -1;
    }
    if (check_range(mem, addr, size, &first, &last) != 0) {
        return -1;
    }
    if (((addr & (ROTTR_GUEST_PAGE_SIZE - 1)) != 0) ||
        (size & (ROTTR_GUEST_PAGE_SIZE - 1)) != 0) {
        return -1;
    }
    for (i = first; i <= last; i++) {
        mem->prot[i] = (uint8_t)prot;
    }
    apply_mprotect(mem, first, last, prot);
    return 0;
}

int rottr_guest_translate(rottr_guest_mem *mem, uintptr_t addr, size_t size,
                          rottr_mem_result *result)
{
    size_t first, last;
    uintptr_t base = (uintptr_t)mem->base;

    if (check_range(mem, addr, size, &first, &last) != 0) {
        return 0;
    }
    result->host = (void *)(base + (addr - base));
    result->ok = 1;
    return 1;
}

int rottr_guest_check(rottr_guest_mem *mem, uintptr_t addr, size_t size,
                      uint32_t prot, rottr_mem_result *result)
{
    size_t first, last, i;

    if (check_range(mem, addr, size, &first, &last) != 0) {
        return 0;
    }
    for (i = first; i <= last; i++) {
        if (mem->state[i] != ROTTR_REGION_COMMITTED ||
            (mem->prot[i] & prot) != prot) {
            return 0;
        }
    }
    result->host = (void *)((uintptr_t)mem->base + (addr - (uintptr_t)mem->base));
    result->ok = 1;
    return 1;
}

int rottr_guest_read(rottr_guest_mem *mem, uintptr_t addr, void *dst,
                     size_t size)
{
    rottr_mem_result r;
    if (rottr_guest_check(mem, addr, size, ROTTR_MEM_READ, &r) != 1) {
        return -1;
    }
    memcpy(dst, r.host, size);
    return 0;
}

int rottr_guest_write(rottr_guest_mem *mem, uintptr_t addr, const void *src,
                      size_t size)
{
    rottr_mem_result r;
    if (rottr_guest_check(mem, addr, size, ROTTR_MEM_WRITE, &r) != 1) {
        return -1;
    }
    memcpy(r.host, src, size);
    return 0;
}

/* ---- Windows API equivalents ---- */

static rottr_guest_mem *g_default_mem;
static uint32_t g_last_error;

rottr_guest_mem *rottr_guest_default_mem(void)
{
    if (g_default_mem == NULL) {
        g_default_mem = rottr_guest_reserve(ROTTR_GUEST_DEFAULT_RANGE_SIZE);
    }
    return g_default_mem;
}

uint32_t rottr_GetLastError(void)
{
    return g_last_error;
}

void rottr_SetLastError(uint32_t error)
{
    g_last_error = error;
}

static uint32_t map_prot_in(uint32_t prot)
{
    switch (prot) {
    case ROTTR_PAGE_NOACCESS:          return ROTTR_MEM_NONE;
    case ROTTR_PAGE_READONLY:          return ROTTR_MEM_READ;
    case ROTTR_PAGE_READWRITE:         return ROTTR_MEM_READ | ROTTR_MEM_WRITE;
    case ROTTR_PAGE_WRITECOPY:         return ROTTR_MEM_READ | ROTTR_MEM_WRITE;
    case ROTTR_PAGE_EXECUTE:           return ROTTR_MEM_EXEC;
    case ROTTR_PAGE_EXECUTE_READ:      return ROTTR_MEM_EXEC | ROTTR_MEM_READ;
    case ROTTR_PAGE_EXECUTE_READWRITE: return ROTTR_MEM_EXEC | ROTTR_MEM_READ | ROTTR_MEM_WRITE;
    case ROTTR_PAGE_EXECUTE_WRITECOPY: return ROTTR_MEM_EXEC | ROTTR_MEM_READ | ROTTR_MEM_WRITE;
    default:                           return 0x80000000u; /* invalid marker */
    }
}

static uint32_t map_prot_out(uint32_t prot)
{
    if (prot == ROTTR_MEM_NONE) return ROTTR_PAGE_NOACCESS;
    if (prot == ROTTR_MEM_READ) return ROTTR_PAGE_READONLY;
    if (prot == (ROTTR_MEM_READ | ROTTR_MEM_WRITE)) return ROTTR_PAGE_READWRITE;
    if (prot == ROTTR_MEM_EXEC) return ROTTR_PAGE_EXECUTE;
    if (prot == (ROTTR_MEM_EXEC | ROTTR_MEM_READ)) return ROTTR_PAGE_EXECUTE_READ;
    if (prot == (ROTTR_MEM_EXEC | ROTTR_MEM_READ | ROTTR_MEM_WRITE))
        return ROTTR_PAGE_EXECUTE_READWRITE;
    return ROTTR_PAGE_READWRITE;
}

void *rottr_VirtualAlloc(void *addr, size_t size, uint32_t flAllocationType,
                         uint32_t flProt)
{
    rottr_guest_mem *mem = rottr_guest_default_mem();
    int commit, reserve;
    uint32_t prot;
    size_t alloc_size;
    uintptr_t region_base;
    void *p;

    rottr_SetLastError(ROTTR_ERROR_SUCCESS);
    if (mem == NULL) {
        rottr_SetLastError(ROTTR_ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    if (size == 0) {
        rottr_SetLastError(ROTTR_ERROR_INVALID_PARAMETER);
        return NULL;
    }
    prot = map_prot_in(flProt);
    if (prot == 0x80000000u) {
        rottr_SetLastError(ROTTR_ERROR_INVALID_PARAMETER);
        return NULL;
    }
    reserve = (flAllocationType & ROTTR_MEM_RESERVE) != 0;
    commit = (flAllocationType & ROTTR_MEM_COMMIT) != 0;
    if (!reserve && !commit) {
        rottr_SetLastError(ROTTR_ERROR_INVALID_PARAMETER);
        return NULL;
    }
    if (addr != NULL &&
        ((uintptr_t)addr & (ROTTR_ALLOC_GRANULARITY - 1)) != 0) {
        rottr_SetLastError(ROTTR_ERROR_INVALID_PARAMETER);
        return NULL;
    }
    alloc_size = align_up64k(size);
    if (addr != NULL) {
        region_base = (uintptr_t)addr;
    } else {
        uintptr_t last_end = mem->region_count == 0
                                 ? 0
                                 : (uintptr_t)mem->regions
                                       [mem->region_count - 1]
                                       .BaseAddress -
                                         (uintptr_t)mem->base
                                     + mem->regions[mem->region_count - 1]
                                         .RegionSize;
        region_base = align_up64k(last_end) + (uintptr_t)mem->base;
    }
    {
        uintptr_t offset = region_base - (uintptr_t)mem->base;
        if (offset > mem->size || alloc_size > mem->size - offset) {
            rottr_SetLastError(ROTTR_ERROR_NOT_ENOUGH_MEMORY);
            return NULL;
        }
    }
    {
        /* Commit-in-place: the requested range lies inside an existing
         * reserved region. Reserving there again is an overlap; committing
         * extends the reservation in place with no new region. */
        int inside = find_region(mem, region_base);
        if (inside >= 0 &&
            region_base == (uintptr_t)mem->regions[inside].BaseAddress &&
            alloc_size <= mem->regions[inside].RegionSize) {
            if (reserve) {
                rottr_SetLastError(ROTTR_ERROR_INVALID_PARAMETER);
                return NULL; /* overlap */
            }
            if (commit) {
                uintptr_t off = region_base - (uintptr_t)mem->base;
                size_t first = off / ROTTR_GUEST_PAGE_SIZE;
                size_t last = (off + alloc_size - 1) / ROTTR_GUEST_PAGE_SIZE;
                size_t i;
                for (i = first; i <= last; i++) {
                    mem->state[i] = ROTTR_REGION_COMMITTED;
                    if (mem->prot[i] == ROTTR_MEM_NONE) {
                        mem->prot[i] = (uint8_t)prot;
                    }
                }
                apply_mprotect(mem, first, last, ROTTR_MEM_READ | ROTTR_MEM_WRITE);
                {
                    uintptr_t host = (uintptr_t)mem->base + off;
                    memset((void *)host, 0, alloc_size);
                }
                apply_mprotect(mem, first, last, prot);
                mem->regions[inside].State = ROTTR_REGION_COMMITTED;
                mem->regions[inside].Protect = (uint8_t)prot;
            }
            rottr_SetLastError(ROTTR_ERROR_SUCCESS);
            return (void *)region_base;
        }
    }
    {
        size_t i;
        for (i = 0; i < mem->region_count; i++) {
            uintptr_t rb = (uintptr_t)mem->regions[i].BaseAddress;
            if (regions_overlap(region_base, alloc_size, rb,
                                mem->regions[i].RegionSize)) {
                rottr_SetLastError(ROTTR_ERROR_INVALID_PARAMETER);
                return NULL; /* overlap */
            }
        }
    }
    if (mem->region_count >= ROTTR_MAX_REGIONS) {
        rottr_SetLastError(ROTTR_ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    mem->regions[mem->region_count].BaseAddress = (void *)region_base;
    mem->regions[mem->region_count].AllocationBase = (void *)region_base;
    mem->regions[mem->region_count].AllocationProt = prot;
    mem->regions[mem->region_count].RegionSize = alloc_size;
    mem->regions[mem->region_count].State = ROTTR_REGION_RESERVED;
    mem->regions[mem->region_count].Protect = ROTTR_MEM_NONE;
    mem->region_count++;

    if (commit) {
        uintptr_t off = region_base - (uintptr_t)mem->base;
        size_t first = off / ROTTR_GUEST_PAGE_SIZE;
        size_t last = (off + alloc_size - 1) / ROTTR_GUEST_PAGE_SIZE;
        size_t i;
        for (i = first; i <= last; i++) {
            mem->state[i] = ROTTR_REGION_COMMITTED;
            if (mem->prot[i] == ROTTR_MEM_NONE) {
                mem->prot[i] = (uint8_t)prot;
            }
        }
        apply_mprotect(mem, first, last, ROTTR_MEM_READ | ROTTR_MEM_WRITE);
        {
            uintptr_t host = (uintptr_t)mem->base + off;
            memset((void *)host, 0, alloc_size);
        }
        apply_mprotect(mem, first, last, prot);
        mem->regions[mem->region_count - 1].State = ROTTR_REGION_COMMITTED;
        mem->regions[mem->region_count - 1].Protect = (uint8_t)prot;
    }

    p = (void *)region_base;
    rottr_SetLastError(ROTTR_ERROR_SUCCESS);
    return p;
}

int rottr_VirtualFree(void *addr, size_t size, uint32_t flFreeType)
{
    rottr_guest_mem *mem = rottr_guest_default_mem();
    int idx;

    rottr_SetLastError(ROTTR_ERROR_SUCCESS);
    if (mem == NULL || addr == NULL) {
        rottr_SetLastError(ROTTR_ERROR_INVALID_PARAMETER);
        return 0;
    }
    idx = find_region(mem, (uintptr_t)addr);
    if (idx < 0 ||
        (uintptr_t)addr != (uintptr_t)mem->regions[idx].BaseAddress) {
        rottr_SetLastError(ROTTR_ERROR_INVALID_PARAMETER);
        return 0; /* unknown address or double free */
    }

    if (flFreeType == ROTTR_MEM_DECOMMIT) {
        uintptr_t off = (uintptr_t)addr - (uintptr_t)mem->base;
        size_t first = off / ROTTR_GUEST_PAGE_SIZE;
        size_t last = (off + mem->regions[idx].RegionSize - 1) /
                      ROTTR_GUEST_PAGE_SIZE;
        size_t i;
        for (i = first; i <= last; i++) {
            mem->state[i] = ROTTR_REGION_FREE;
            mem->prot[i] = ROTTR_MEM_NONE;
        }
        apply_mprotect(mem, first, last, ROTTR_MEM_NONE);
        mem->regions[idx].State = ROTTR_REGION_RESERVED;
        mem->regions[idx].Protect = ROTTR_MEM_NONE;
        rottr_SetLastError(ROTTR_ERROR_SUCCESS);
        return 1;
    }
    if (flFreeType == ROTTR_MEM_RELEASE) {
        if (size != 0) {
            rottr_SetLastError(ROTTR_ERROR_INVALID_PARAMETER);
            return 0;
        }
        {
            uintptr_t off = (uintptr_t)addr - (uintptr_t)mem->base;
            size_t first = off / ROTTR_GUEST_PAGE_SIZE;
            size_t last = (off + mem->regions[idx].RegionSize - 1) /
                          ROTTR_GUEST_PAGE_SIZE;
            size_t i;
            for (i = first; i <= last; i++) {
                mem->state[i] = ROTTR_REGION_FREE;
                mem->prot[i] = ROTTR_MEM_NONE;
            }
            apply_mprotect(mem, first, last, ROTTR_MEM_NONE);
        }
        mem->region_count--;
        {
            size_t i;
            for (i = (size_t)idx; i < mem->region_count; i++) {
                mem->regions[i] = mem->regions[i + 1];
            }
        }
        rottr_SetLastError(ROTTR_ERROR_SUCCESS);
        return 1;
    }
    rottr_SetLastError(ROTTR_ERROR_INVALID_PARAMETER);
    return 0;
}

int rottr_VirtualProtect(void *addr, size_t size, uint32_t flNewProt,
                         uint32_t *lpflOldProt)
{
    rottr_guest_mem *mem = rottr_guest_default_mem();
    size_t first, last;
    uint32_t prot = map_prot_in(flNewProt);
    uint32_t old = ROTTR_MEM_NONE;

    rottr_SetLastError(ROTTR_ERROR_SUCCESS);
    if (mem == NULL || prot == 0x80000000u) {
        rottr_SetLastError(ROTTR_ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (check_range(mem, (uintptr_t)addr, size, &first, &last) != 0) {
        rottr_SetLastError(ROTTR_ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (((uintptr_t)addr & (ROTTR_GUEST_PAGE_SIZE - 1)) != 0 ||
        (size & (ROTTR_GUEST_PAGE_SIZE - 1)) != 0) {
        rottr_SetLastError(ROTTR_ERROR_INVALID_PARAMETER);
        return 0;
    }
    {
        size_t i;
        for (i = first; i <= last; i++) {
            if (mem->state[i] == ROTTR_REGION_COMMITTED) {
                old = mem->prot[i];
                break;
            }
        }
    }
    if (rottr_guest_protect_pages(mem, (uintptr_t)addr, size, prot) != 0) {
        rottr_SetLastError(ROTTR_ERROR_ACCESS_DENIED);
        return 0;
    }
    if (lpflOldProt != NULL) {
        *lpflOldProt = map_prot_out(old);
    }
    {
        size_t i;
        for (i = 0; i < mem->region_count; i++) {
            uintptr_t rb = (uintptr_t)mem->regions[i].BaseAddress;
            if ((uintptr_t)addr >= rb &&
                (uintptr_t)addr < rb + mem->regions[i].RegionSize) {
                mem->regions[i].Protect = (uint8_t)prot;
                break;
            }
        }
    }
    rottr_SetLastError(ROTTR_ERROR_SUCCESS);
    return 1;
}

int rottr_VirtualQuery(const void *addr, rottr_mem_region_info *buffer,
                       size_t length)
{
    rottr_guest_mem *mem = rottr_guest_default_mem();
    int idx;

    rottr_SetLastError(ROTTR_ERROR_SUCCESS);
    if (mem == NULL || buffer == NULL || length < sizeof(*buffer)) {
        rottr_SetLastError(ROTTR_ERROR_INVALID_PARAMETER);
        return 0;
    }
    idx = find_region(mem, (uintptr_t)addr);
    if (idx < 0) {
        buffer->BaseAddress = (void *)(uintptr_t)addr;
        buffer->AllocationBase = NULL;
        buffer->AllocationProt = ROTTR_PAGE_NOACCESS;
        buffer->RegionSize = 0;
        buffer->State = ROTTR_REGION_FREE;
        buffer->Protect = ROTTR_MEM_NONE;
        rottr_SetLastError(ROTTR_ERROR_SUCCESS);
        return (int)sizeof(*buffer);
    }
    *buffer = mem->regions[idx];
    buffer->RegionSize -= (uintptr_t)addr -
                          (uintptr_t)mem->regions[idx].BaseAddress;
    /* Report Windows-style protection constants. */
    buffer->Protect = map_prot_out(mem->regions[idx].Protect);
    buffer->AllocationProt = map_prot_out(mem->regions[idx].AllocationProt);
    rottr_SetLastError(ROTTR_ERROR_SUCCESS);
    return (int)sizeof(*buffer);
}
