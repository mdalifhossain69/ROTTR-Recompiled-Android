/* Small Windows test program built with mingw-w64 in CI (roadmap step 3).
 * It exercises the real Windows Virtual* API so its output documents the
 * semantics the guest memory manager (roadmap step 8) must match. */
#include <stdint.h>
#include <stdio.h>
#include <windows.h>

int main(void)
{
    /* Commit + reserve a page. */
    LPVOID p = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE,
                            PAGE_READWRITE);
    if (p == NULL) {
        printf("VirtualAlloc failed: %lu\n", GetLastError());
        return 1;
    }
    printf("alloc=%p 64k-aligned=%d\n", p, ((uintptr_t)p % 65536) == 0);

    *(volatile unsigned char *)p = 0x41;
    printf("write committed page: ok\n");

    DWORD old = 0xdeadbeefu;
    if (!VirtualProtect(p, 4096, PAGE_NOACCESS, &old)) {
        printf("VirtualProtect failed: %lu\n", GetLastError());
        return 1;
    }
    printf("protect old=0x%lx\n", (unsigned long)old);

    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(p, &mbi, sizeof(mbi)) == 0) {
        printf("VirtualQuery failed: %lu\n", GetLastError());
        return 1;
    }
    printf("query state=0x%lx protect=0x%lx region=%zu\n",
           (unsigned long)mbi.State, (unsigned long)mbi.Protect,
           mbi.RegionSize);

    if (!VirtualFree(p, 0, MEM_DECOMMIT)) {
        printf("VirtualFree decommit failed: %lu\n", GetLastError());
        return 1;
    }
    VirtualQuery(p, &mbi, sizeof(mbi));
    printf("after decommit state=0x%lx\n", (unsigned long)mbi.State);

    if (!VirtualFree(p, 0, MEM_RELEASE)) {
        printf("VirtualFree release failed: %lu\n", GetLastError());
        return 1;
    }
    printf("release ok\n");

    /* Rejection cases, documenting documented error codes. */
    SetLastError(0);
    LPVOID bad = VirtualAlloc(NULL, 0, MEM_COMMIT | MEM_RESERVE,
                              PAGE_READWRITE);
    printf("zero-size alloc: %p err=%lu (expect NULL, 87)\n", bad,
           GetLastError());

    LPVOID p2 = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE,
                             PAGE_READWRITE);
    LPVOID overlap = VirtualAlloc(p2, 4096, MEM_COMMIT | MEM_RESERVE,
                                  PAGE_READWRITE);
    printf("overlapping alloc: %p err=%lu (expect NULL)\n", overlap,
           GetLastError());
    VirtualFree(p2, 0, MEM_RELEASE);

    SetLastError(0);
    BOOL again = VirtualFree(p, 0, MEM_RELEASE);
    printf("double release: %d err=%lu (expect 0, 87)\n", again,
           GetLastError());

    printf("done\n");
    return 0;
}
