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
// into a real remill::X86State buffer at the start of harness_run, and the
// lifted function is called with that buffer as the `state` argument.

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
// Minimal X86State layout matching remill::X86State (from remill/Arch/X86/Runtime/State.h)
// Only the fields accessed by lifted code are defined.
// Offsets verified against Remill source:
//   ArchState (16) + vec[32]*64 (2048) + aflag (16) + rflag (8) + seg (24) + addr (96) = 2208
//   GPR starts at 2208, with rax at +8, rcx at +40, rsp at +104, rip at +264
//   aflag starts at 2064, cf at +1, pf at +3, af at +5, zf at +7, sf at +9, df at +11, of at +13
// ---------------------------------------------------------------------------
struct ArithFlags {
    uint8_t pad0;
    uint8_t cf;
    uint8_t pad1;
    uint8_t pf;
    uint8_t pad2;
    uint8_t af;
    uint8_t pad3;
    uint8_t zf;
    uint8_t pad4;
    uint8_t sf;
    uint8_t pad5;
    uint8_t df;
    uint8_t pad6;
    uint8_t of;
    uint8_t pad7[2];
};

struct GPR {
    uint64_t pad0;
    uint64_t rax;
    uint64_t pad1;
    uint64_t rbx;
    uint64_t pad2;
    uint64_t rcx;
    uint64_t pad3;
    uint64_t rdx;
    uint64_t pad4;
    uint64_t rsi;
    uint64_t pad5;
    uint64_t rdi;
    uint64_t pad6;
    uint64_t rsp;
    uint64_t pad7;
    uint64_t rbp;
    uint64_t pad8;
    uint64_t r8;
    uint64_t pad9;
    uint64_t r9;
    uint64_t pad10;
    uint64_t r10;
    uint64_t pad11;
    uint64_t r11;
    uint64_t pad12;
    uint64_t r12;
    uint64_t pad13;
    uint64_t r13;
    uint64_t pad14;
    uint64_t r14;
    uint64_t pad15;
    uint64_t r15;
    uint64_t pad16;
    uint64_t rip;
};

struct alignas(16) X86State {
    // ArchState (16 bytes)
    uint8_t arch_state[16];

    // VectorReg vec[32] (32 * 64 = 2048 bytes)
    uint8_t vec[2048];

    // ArithFlags aflag (16 bytes)
    ArithFlags aflag;

    // Flags rflag (8 bytes) - not directly used by lifted code for arithmetic flags
    uint8_t rflag[8];

    // Segments seg (24 bytes)
    uint8_t seg[24];

    // AddressSpace addr (96 bytes)
    uint8_t addr[96];

    // GPR gpr (272 bytes)
    GPR gpr;

    // Remaining fields (not accessed by lifted leaf functions)
    // st(128) + mmx(128) + sw(24) + pad(8) + x87(512) + seg_caches(96) + k_reg(128) = 1024
    uint8_t remaining[1024];
};

// ---------------------------------------------------------------------------
// Signal handling — convert SIGSEGV into a return code instead of crashing
// ---------------------------------------------------------------------------
static sigjmp_buf fault_jmp;

static void segv_handler(int sig, siginfo_t* info, void* ctx) {
    siglongjmp(fault_jmp, 1);
}

// ---------------------------------------------------------------------------
// harness_run: map Snapshot into X86State, call lifted function, read back
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

    // Allocate and zero the X86State buffer
    X86State* state = new X86State();
    std::memset(state, 0, sizeof(X86State));

    // Map Python Snapshot into X86State
    // GPRs: rax, rbx, rcx, rdx, rsi, rdi, rsp, rbp, r8, r9, r10, r11, r12, r13, r14, r15
    state->gpr.rax = snap_in->gpr[0];
    state->gpr.rbx = snap_in->gpr[1];
    state->gpr.rcx = snap_in->gpr[2];
    state->gpr.rdx = snap_in->gpr[3];
    state->gpr.rsi = snap_in->gpr[4];
    state->gpr.rdi = snap_in->gpr[5];
    state->gpr.rsp = snap_in->gpr[6];
    state->gpr.rbp = snap_in->gpr[7];
    state->gpr.r8  = snap_in->gpr[8];
    state->gpr.r9  = snap_in->gpr[9];
    state->gpr.r10 = snap_in->gpr[10];
    state->gpr.r11 = snap_in->gpr[11];
    state->gpr.r12 = snap_in->gpr[12];
    state->gpr.r13 = snap_in->gpr[13];
    state->gpr.r14 = snap_in->gpr[14];
    state->gpr.r15 = snap_in->gpr[15];

    // RIP
    state->gpr.rip = rva;

    // Arithmetic flags (aflag) - only the 7 flags used by compare_lifted.py
    // snap_in->flags order: cf, pf, af, zf, sf, df, of
    state->aflag.cf = snap_in->flags[0];
    state->aflag.pf = snap_in->flags[1];
    state->aflag.af = snap_in->flags[2];
    state->aflag.zf = snap_in->flags[3];
    state->aflag.sf = snap_in->flags[4];
    state->aflag.df = snap_in->flags[5];
    state->aflag.of = snap_in->flags[6];

    // XMM registers (16 registers, 16 bytes each) - stored in vec[] array
    // vec[0..15] correspond to xmm0..xmm15
    for (int i = 0; i < 16; ++i) {
        std::memcpy(&state->vec[i * 64], snap_in->xmm[i], 16);
    }

    // The lifted function expects: fn(state, pc, memory)
    typedef void (*lifted_fn)(void*, uint64_t, void*);
    lifted_fn fn = reinterpret_cast<lifted_fn>(func_addr);

    if (sigsetjmp(fault_jmp, 1) != 0) {
        rc = 1;  // memory fault
        goto done;
    }

    // Call the lifted function: fn(state, pc, memory)
    fn(state, rva, guest_memory);

    // Read results back out of X86State into snap_out
    snap_out->gpr[0]  = state->gpr.rax;
    snap_out->gpr[1]  = state->gpr.rbx;
    snap_out->gpr[2]  = state->gpr.rcx;
    snap_out->gpr[3]  = state->gpr.rdx;
    snap_out->gpr[4]  = state->gpr.rsi;
    snap_out->gpr[5]  = state->gpr.rdi;
    snap_out->gpr[6]  = state->gpr.rsp;
    snap_out->gpr[7]  = state->gpr.rbp;
    snap_out->gpr[8]  = state->gpr.r8;
    snap_out->gpr[9]  = state->gpr.r9;
    snap_out->gpr[10] = state->gpr.r10;
    snap_out->gpr[11] = state->gpr.r11;
    snap_out->gpr[12] = state->gpr.r12;
    snap_out->gpr[13] = state->gpr.r13;
    snap_out->gpr[14] = state->gpr.r14;
    snap_out->gpr[15] = state->gpr.r15;

    snap_out->flags[0] = state->aflag.cf;
    snap_out->flags[1] = state->aflag.pf;
    snap_out->flags[2] = state->aflag.af;
    snap_out->flags[3] = state->aflag.zf;
    snap_out->flags[4] = state->aflag.sf;
    snap_out->flags[5] = state->aflag.df;
    snap_out->flags[6] = state->aflag.of;
    snap_out->flags[7] = 0; // unused

    for (int i = 0; i < 16; ++i) {
        std::memcpy(snap_out->xmm[i], &state->vec[i * 64], 16);
    }

done:
    delete state;
    sigaction(SIGSEGV, &old_sa, nullptr);
    return rc;
}