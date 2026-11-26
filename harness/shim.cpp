// harness/shim.cpp
// Native shim that runs Remill-lifted functions and captures CPU/memory state.
// Called from scripts/compare_lifted.py via ctypes.
//
// The lifted Remill function has signature:
//   void sub_xxx(State*, uint64_t pc, void* memory)
// where `memory` is an opaque guest-memory pointer that Remill intrinsics
// (__remill_read_memory_64, __remill_write_memory_64, etc.) read from / write to.
//
// The Python ctypes Snapshot (gpr[16], flags[8], xmm[16][16]) is mapped
// into a flat guest_memory buffer at the start of harness_run, and the
// lifted function is called with that buffer as the `memory` argument.

#include <cstdint>
#include <cstring>
#include <csignal>
#include <csetjmp>
#include <signal.h>

// Must match Python ctypes Snapshot in compare_lifted.py:
//   ("gpr", c_uint64 * 16), ("flags", c_uint8 * 8),
//   ("xmm", (c_uint8 * 16) * 16)
struct Snapshot {
    uint64_t gpr[16];
    uint8_t  flags[8];
    uint8_t  xmm[16][16];
};

// ---------------------------------------------------------------------------
// Guest memory buffer — 1 MB flat address space managed by remill_intrinsics.cpp.
// ---------------------------------------------------------------------------
static uint8_t guest_memory[0x100000];

// ---------------------------------------------------------------------------
// Region tracking (not used by the trampoline; kept for compatibility).
// ---------------------------------------------------------------------------
struct Region {
    uint64_t base;
    uint64_t size;
    uint8_t* host_ptr;
    int      prot;
};
static Region regions[64];
static int region_count = 0;

extern "C" void harness_clear_regions() {
    region_count = 0;
}

extern "C" void harness_add_region(uint64_t base, uint64_t size,
                                   void* host_ptr, int prot) {
    if (region_count < 64) {
        regions[region_count++] = {base, size, static_cast<uint8_t*>(host_ptr), prot};
    }
}

// WARNING: This function is called by generated stubs in stubs.cpp.
// It must have a global definition visible to the linker.
extern "C" void harness_note_external(void) {
}

// ---------------------------------------------------------------------------
// Signal handling — convert SIGSEGV into a return code instead of crashing
// ---------------------------------------------------------------------------
static sigjmp_buf fault_jmp;

static void segv_handler(int sig, siginfo_t* info, void* ctx) {
    siglongjmp(fault_jmp, 1);
}

// ---------------------------------------------------------------------------
// harness_run: map Snapshot into guest memory, call lifted function, read back
// ---------------------------------------------------------------------------
extern "C" int harness_run(void* func_addr, uint64_t rva,
                           Snapshot* snap_in, Snapshot* snap_out,
                           int timeout, uint32_t* info) {
    info[0] = 0;  // undefined-values-used flag
    info[1] = 0;  // skip flag

    struct sigaction sa, old_sa;
    sa.sa_sigaction = segv_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &old_sa);

    int rc = 0;

    // Map the Python Snapshot into the flat guest_memory buffer.
    // Layout: first 128 bytes = 16 × uint64 GPRs + 8 bytes flags
    // + 256 bytes XMM = exactly the Snapshot size (256 bytes).
    uint8_t* gmem = guest_memory;
    std::memcpy(gmem, snap_in->gpr, 16 * sizeof(uint64_t));   // gpr[0..15]
    gmem += 16 * sizeof(uint64_t);
    std::memcpy(gmem, snap_in->flags, 8);                       // flags[0..7]
    gmem += 8;
    std::memcpy(gmem, snap_in->xmm, 16 * 16);                  // xmm[0..15][0..15]

    // The lifted function expects: fn(gmem_base, pc, memory)
    // gmem_base points to the start of the Snapshot within guest_memory.
    typedef void (*lifted_fn)(void*, uint64_t, void*);
    lifted_fn fn = reinterpret_cast<lifted_fn>(func_addr);

    if (sigsetjmp(fault_jmp, 1) != 0) {
        rc = 1;  // memory fault
        goto done;
    }

    // Call the lifted function: fn(guest_memory_buffer_start, pc, memory)
    fn(gmem - (16 * sizeof(uint64_t) + 8 + 16 * 16), rva, guest_memory);

    // Read results back out of guest_memory into snap_out.
    // Layout mirrors the input mapping above: 16 × uint64 GPRs, 8 flag bytes,
    // then 16 × 16 XMM bytes. The lifted function mutates this buffer in place.
    std::memcpy(snap_out->gpr, guest_memory, 16 * sizeof(uint64_t));
    std::memcpy(snap_out->flags, guest_memory + 16 * sizeof(uint64_t), 8);
    std::memcpy(snap_out->xmm, guest_memory + 16 * sizeof(uint64_t) + 8, 16 * 16);

done:
    sigaction(SIGSEGV, &old_sa, nullptr);
    return rc;
}
