// harness/shim.cpp
// Native shim that runs Remill-lifted functions and captures CPU/memory state.
// Called from scripts/compare_lifted.py via ctypes.
//
// The Remill state layout must match your Remill build. Adjust the
// RemillState struct below to match remill/Arch/X86/Runtime/State.h
// from your Remill installation.
#include <cstdint>
#include <cstring>
#include <csignal>
#include <csetjmp>
#include <signal.h>

// Must match Python ctypes Snapshot in compare_lifted.py:
//   ("gpr", c_uint64 * 16), ("flags", c_uint8 * 8), ("xmm", (c_uint8 * 16) * 16)
struct Snapshot {
    uint64_t gpr[16];
    uint8_t  flags[8];
    uint8_t  xmm[16][16];
};

// ---------------------------------------------------------------------------
// Remill CPU state.  Offsets must match your Remill version's State.h.
// ---------------------------------------------------------------------------
struct RemillState {
    uint64_t gpr[16];   // rax..r15
    uint64_t rip;
    uint64_t rflags;
    uint64_t xmm[16][2]; // 128-bit XMM as two 64-bit lanes
    void*    memory_state;
};

// ---------------------------------------------------------------------------
// Memory region tracking (for harness_add_region / harness_clear_regions)
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

static uint8_t* find_host_ptr(uint64_t guest_addr) {
    for (int i = 0; i < region_count; i++) {
        if (guest_addr >= regions[i].base &&
            guest_addr < regions[i].base + regions[i].size) {
            return regions[i].host_ptr + (guest_addr - regions[i].base);
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Signal handling — convert SIGSEGV into a return code instead of crashing
// ---------------------------------------------------------------------------
static sigjmp_buf fault_jmp;

static void segv_handler(int sig, siginfo_t* info, void* ctx) {
    siglongjmp(fault_jmp, 1);
}

// ---------------------------------------------------------------------------
// harness_run: set up state, call lifted function, read back state
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
    if (sigsetjmp(fault_jmp, 1) != 0) {
        rc = 1;  // memory fault
        goto done;
    }

    // Build Remill state from input snapshot
    RemillState state;
    std::memset(&state, 0, sizeof(state));
    std::memcpy(state.gpr, snap_in->gpr, sizeof(state.gpr));
    state.rip = rva;

    // EFLAGS: bits 0,2,4,6,7,10,11 = cf,pf,af,zf,sf,df,of
    uint64_t eflags = 0x2;  // reserved bit always set
    static const int flag_bits[] = {0, 2, 4, 6, 7, 10, 11};
    for (int i = 0; i < 7; i++) {
        if (snap_in->flags[i]) {
            eflags |= (1ULL << flag_bits[i]);
        }
    }
    state.rflags = eflags;

    for (int i = 0; i < 16; i++) {
        std::memcpy(state.xmm[i], snap_in->xmm[i], 16);
    }

    // Call the lifted Remill function: void sub_xxx(void* state)
    typedef void (*lifted_fn)(void*);
    lifted_fn fn = static_cast<lifted_fn>(func_addr);
    fn(&state);

    // Read state back into output snapshot
    std::memcpy(snap_out->gpr, state.gpr, sizeof(snap_out->gpr));

    uint64_t final_eflags = state.rflags;
    for (int i = 0; i < 7; i++) {
        snap_out->flags[i] = (final_eflags >> flag_bits[i]) & 1;
    }

    for (int i = 0; i < 16; i++) {
        std::memcpy(snap_out->xmm[i], state.xmm[i], 16);
    }

done:
    sigaction(SIGSEGV, &old_sa, nullptr);
    return rc;
}