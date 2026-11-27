#include <stdio.h>
#include <string.h>

#include "rottr/guest_mem.h"

#define RANGE_SIZE (1ull << 32) /* 4 GB reservation for the test */

static int failures;

static void check_fail(const char *what, int cond)
{
    if (cond) {
        printf("  ok: %s\n", what);
    } else {
        printf("  FAIL: %s\n", what);
        failures++;
    }
}

int main(void)
{
    rottr_guest_mem *mem;
    uintptr_t base;
    rottr_mem_result r;
    uint8_t buf[4096];
    uint8_t wbuf[16] = {0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
                        0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10};

    /* ---- Step 4: reserve + bounds-checked translation ---- */
    printf("step 4: reserve and translate\n");
    mem = rottr_guest_reserve(RANGE_SIZE);
    check_fail("reservation non-null", mem != NULL);
    base = (uintptr_t)rottr_guest_base(mem);

    check_fail("first byte translates",
               rottr_guest_translate(mem, base, 1, &r) == 1 &&
               r.host == (void *)base);
    check_fail("last byte translates",
               rottr_guest_translate(mem, base + RANGE_SIZE - 1, 1, &r) == 1 &&
               r.host == (void *)(base + RANGE_SIZE - 1));
    check_fail("one byte outside fails",
               rottr_guest_translate(mem, base + RANGE_SIZE, 1, &r) == 0);
    check_fail("range crossing end fails",
               rottr_guest_translate(mem, base + RANGE_SIZE - 1, 2, &r) == 0);
    check_fail("translate before base fails",
               rottr_guest_translate(mem, base - 1, 1, &r) == 0);

    /* ---- Step 5: commit / decommit 4 KB pages ---- */
    printf("step 5: commit and decommit\n");
    {
        uintptr_t page = base + 4096;
        check_fail("commit page", rottr_guest_commit_pages(mem, page, 4096) == 0);
        check_fail("committed page reads zero",
                   rottr_guest_read(mem, page, buf, 4096) == 0);
        {
            int all_zero = 1;
            for (size_t i = 0; i < 4096; i++) { if (buf[i] != 0) all_zero = 0; }
            check_fail("committed page reads zero (all bytes)", all_zero);
        }
        check_fail("write to committed page",
                   rottr_guest_write(mem, page, wbuf, 16) == 0);
        check_fail("read back written pattern",
                   rottr_guest_read(mem, page, buf, 16) == 0 &&
                   memcmp(buf, wbuf, 16) == 0);

        check_fail("decommit page",
                   rottr_guest_decommit_pages(mem, page, 4096) == 0);
        check_fail("uncommitted read fails",
                   rottr_guest_read(mem, page, buf, 1) == -1);
        check_fail("uncommitted write fails",
                   rottr_guest_write(mem, page, wbuf, 1) == -1);

        check_fail("recommit page",
                   rottr_guest_commit_pages(mem, page, 4096) == 0);
        check_fail("recommitted page reads zero",
                   rottr_guest_read(mem, page, buf, 4096) == 0);
        {
            int all_zero = 1;
            for (int i = 0; i < 4096; i++) { if (buf[i] != 0) all_zero = 0; }
            check_fail("recommitted page reads zero (all bytes)", all_zero);
        }
    }

    /* ---- Step 6: page protections, 4 KB granularity ---- */
    printf("step 6: page protections\n");
    {
        uintptr_t p0 = base + 2 * 4096;
        uintptr_t p1 = p0 + 4096; /* adjacent 4 KB page */
        check_fail("commit p0,p1",
                   rottr_guest_commit_pages(mem, p0, 4096) == 0 &&
                   rottr_guest_commit_pages(mem, p1, 4096) == 0);

        check_fail("protect p0 none",
                   rottr_guest_protect_pages(mem, p0, 4096, ROTTR_MEM_NONE) == 0);
        check_fail("protect p1 read|write",
                   rottr_guest_protect_pages(mem, p1, 4096,
                                             ROTTR_MEM_READ | ROTTR_MEM_WRITE) == 0);

        check_fail("none page: read rejected",
                   rottr_guest_check(mem, p0, 1, ROTTR_MEM_READ, &r) == 0);
        check_fail("none page: write rejected",
                   rottr_guest_check(mem, p0, 1, ROTTR_MEM_WRITE, &r) == 0);
        check_fail("none page: exec rejected",
                   rottr_guest_check(mem, p0, 1, ROTTR_MEM_EXEC, &r) == 0);

        check_fail("read|write page: read allowed",
                   rottr_guest_check(mem, p1, 1, ROTTR_MEM_READ, &r) == 1);
        check_fail("read|write page: write allowed",
                   rottr_guest_check(mem, p1, 1, ROTTR_MEM_WRITE, &r) == 1);
        check_fail("read|write page: exec rejected",
                   rottr_guest_check(mem, p1, 1, ROTTR_MEM_EXEC, &r) == 0);

        {
            uintptr_t pe = base + 4 * 4096;
            check_fail("commit exec page",
                       rottr_guest_commit_pages(mem, pe, 4096) == 0);
            check_fail("protect exec page",
                       rottr_guest_protect_pages(mem, pe, 4096, ROTTR_MEM_EXEC) == 0);
            check_fail("exec page: exec allowed",
                       rottr_guest_check(mem, pe, 1, ROTTR_MEM_EXEC, &r) == 1);
            check_fail("exec page: read rejected",
                       rottr_guest_check(mem, pe, 1, ROTTR_MEM_READ, &r) == 0);
        }

        /* 4 KB granularity: adjacent 4 KB pages with different permissions
         * must each be enforced independently, even when they share a 16 KB
         * host page. The software permission table is the authority. */
        check_fail("cross-page range hits none page",
                   rottr_guest_check(mem, p0, 2, ROTTR_MEM_READ, &r) == 0);
        check_fail("adjacent pages enforced independently",
                   rottr_guest_check(mem, p0 + 4095, 1, ROTTR_MEM_READ, &r) == 0 &&
                   rottr_guest_check(mem, p0 + 4096, 1, ROTTR_MEM_READ, &r) == 1);
    }

    /* ---- Step 7: release and query regions, 64 KB granularity ---- */
    printf("step 7: region reserve, query and release\n");
    {
        void *ra = rottr_guest_reserve_region(mem, NULL, 4096);
        check_fail("reserve region non-null", ra != NULL);
        check_fail("reserve region 64 KB aligned",
                   ra != NULL && ((uintptr_t)ra & 65535u) == 0);

        void *rb = rottr_guest_reserve_region(mem, ra, 4096);
        check_fail("overlapping reserve rejected", rb == NULL);

        void *unaligned = (void *)(base + 4096);
        void *ru = rottr_guest_reserve_region(mem, unaligned, 4096);
        check_fail("unaligned base rejected", ru == NULL);

        void *far = (void *)(base + RANGE_SIZE - 100);
        void *rf = rottr_guest_reserve_region(mem, far, 4096);
        check_fail("out-of-range reserve rejected", rf == NULL);

        void *huge = rottr_guest_reserve_region(mem, NULL, ~0ull);
        check_fail("integer overflow reserve rejected", huge == NULL);

        rottr_mem_region_info info;
        check_fail("query reports reserved state",
                   rottr_guest_query_region(mem, (uintptr_t)ra, &info) == 1 &&
                   info.State == ROTTR_REGION_RESERVED &&
                   info.RegionSize == 65536 &&
                     (uintptr_t)info.AllocationBase == (uintptr_t)ra);
        check_fail("query free address reports free",
                   rottr_guest_query_region(mem, base + 10000000ull, &info) == 0);

        check_fail("release region",
                   rottr_guest_release_region(mem, ra, 0) == 0);
        check_fail("double release rejected",
                   rottr_guest_release_region(mem, ra, 0) == -1);
        check_fail("query after release reports free",
                   rottr_guest_query_region(mem, (uintptr_t)ra, &info) == 0);
    }

    /* ---- Step 8: VirtualAlloc/Free/Protect/Query wrappers ---- */
    printf("step 8: Windows API wrappers\n");
    {
        rottr_mem_region_info info;
        uint32_t old = 0xdeadbeefu;
        void *p = rottr_VirtualAlloc(NULL, 4096,
                                     ROTTR_MEM_COMMIT | ROTTR_MEM_RESERVE,
                                     ROTTR_PAGE_READWRITE);
        check_fail("virtual alloc succeeds", p != NULL);
        check_fail("virtual alloc aligned",
                   p != NULL && ((uintptr_t)p & 65535u) == 0);
        check_fail("virtual alloc last error success",
                   rottr_GetLastError() == ROTTR_ERROR_SUCCESS);

        check_fail("virtual zero size rejected",
                   rottr_VirtualAlloc(NULL, 0,
                                      ROTTR_MEM_COMMIT | ROTTR_MEM_RESERVE,
                                      ROTTR_PAGE_READWRITE) == NULL &&
                   rottr_GetLastError() == ROTTR_ERROR_INVALID_PARAMETER);

        void *unaligned = (void *)((uintptr_t)p + 4096);
        check_fail("unaligned base rejected",
                   rottr_VirtualAlloc(unaligned, 4096,
                                      ROTTR_MEM_COMMIT | ROTTR_MEM_RESERVE,
                                      ROTTR_PAGE_READWRITE) == NULL &&
                   rottr_GetLastError() == ROTTR_ERROR_INVALID_PARAMETER);

        /* Overlap rejection. */
        void *overlap = rottr_VirtualAlloc(p, 4096,
                                           ROTTR_MEM_COMMIT | ROTTR_MEM_RESERVE,
                                           ROTTR_PAGE_READWRITE);
        check_fail("overlapping alloc rejected", overlap == NULL &&
                   rottr_GetLastError() == ROTTR_ERROR_INVALID_PARAMETER);

        /* Committed memory is readable/writable. */
        check_fail("allocated memory writable",
                   rottr_guest_write(rottr_guest_default_mem(),
                                     (uintptr_t)p, wbuf, 16) == 0);
        check_fail("allocated memory readable",
                   rottr_guest_read(rottr_guest_default_mem(),
                                    (uintptr_t)p, buf, 16) == 0 &&
                   memcmp(buf, wbuf, 16) == 0);

        /* Decommit through the wrapper. */
        check_fail("virtual decommit",
                   rottr_VirtualFree(p, 0, ROTTR_MEM_DECOMMIT) == 1 &&
                   rottr_GetLastError() == ROTTR_ERROR_SUCCESS);
        check_fail("query reports reserved after decommit",
                   rottr_VirtualQuery(p, &info, sizeof(info)) == (int)sizeof(info) &&
                   info.State == ROTTR_REGION_RESERVED &&
                   info.Protect == ROTTR_PAGE_NOACCESS);

        /* Re-commit at the same address for the protect test
         * (MEM_COMMIT without MEM_RESERVE on an already reserved range). */
        check_fail("virtual re-commit",
                   rottr_VirtualAlloc(p, 65536,
                                      ROTTR_MEM_COMMIT,
                                      ROTTR_PAGE_READONLY) == p);
        check_fail("virtual protect",
                   rottr_VirtualProtect(p, 4096, ROTTR_PAGE_NOACCESS, &old) == 1 &&
                   rottr_GetLastError() == ROTTR_ERROR_SUCCESS &&
                   old == ROTTR_PAGE_READONLY);
        check_fail("protected memory rejects access",
                   rottr_guest_check(rottr_guest_default_mem(),
                                     (uintptr_t)p, 1, ROTTR_MEM_READ, &r) == 0);

        /* Double release rejected. */
        check_fail("virtual release",
                   rottr_VirtualFree(p, 0, ROTTR_MEM_RELEASE) == 1);
        check_fail("virtual double release rejected",
                   rottr_VirtualFree(p, 0, ROTTR_MEM_RELEASE) == 0 &&
                   rottr_GetLastError() == ROTTR_ERROR_INVALID_PARAMETER);
        check_fail("query after release reports free",
                   rottr_VirtualQuery(p, &info, sizeof(info)) == (int)sizeof(info) &&
                   info.State == ROTTR_REGION_FREE);
    }

    rottr_guest_release(mem);
    printf("%s\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
