// Native shim that runs Remill-lifted functions and captures CPU and memory state.
// Called from scripts/compare_lifted.py through ctypes.
//
// Lifted functions have the signature
//   Memory *sub_<address>(State *, uint64_t pc, Memory *)
// Here `Memory *` is a rottr_memory that maps the same guest addresses the
// reference emulator uses, backed by buffers supplied by the caller.

#include <csetjmp>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <unistd.h>

#include "remill/Arch/X86/Runtime/State.h"
#include "rottr/remill_runtime.h"

// Must match the ctypes Snapshot in compare_lifted.py.
struct Snapshot {
    uint64_t gpr[16];
    uint8_t flags[8];  // cf, pf, af, zf, sf, df, of, unused
    uint8_t xmm[16][16];
};

// Return codes of harness_run.
enum { RUN_OK = 0, RUN_FAULT = 1, RUN_TIMEOUT = 2 };

static sigjmp_buf run_exit;
static rottr_memory memory;

static uint64_t fault_address;

static void on_memory_fault(uint64_t address, size_t, int) {
    fault_address = address;
    siglongjmp(run_exit, RUN_FAULT);
}
static void on_timeout(int) { siglongjmp(run_exit, RUN_TIMEOUT); }

// Guest address of the access that ended the last run with RUN_FAULT.
extern "C" uint64_t harness_fault_address() { return fault_address; }

extern "C" void harness_clear_regions() { rottr_memory_reset(&memory); }

extern "C" int harness_add_region(uint64_t base, uint64_t size, void *host, int writable) {
    return rottr_memory_add(&memory, base, size, host, writable);
}

// Called by the generated stubs that stand in for callees that were not lifted.
extern "C" void harness_note_external() { ++rottr_run_events.escaped; }

#define EACH_GPR(X)                                                              \
    X(0, rax) X(1, rbx) X(2, rcx) X(3, rdx) X(4, rsi) X(5, rdi) X(6, rsp) X(7, rbp) \
    X(8, r8) X(9, r9) X(10, r10) X(11, r11) X(12, r12) X(13, r13) X(14, r14) X(15, r15)

extern "C" int harness_run(void *function, uint64_t entry, const Snapshot *in,
                           Snapshot *out, int timeout_seconds, rottr_events *events) {
    static State state;
    std::memset(static_cast<void *>(&state), 0, sizeof(state));

#define SET(i, reg) state.gpr.reg.qword = in->gpr[i];
    EACH_GPR(SET)
#undef SET
    state.gpr.rip.qword = entry;
    state.aflag.cf = in->flags[0];
    state.aflag.pf = in->flags[1];
    state.aflag.af = in->flags[2];
    state.aflag.zf = in->flags[3];
    state.aflag.sf = in->flags[4];
    state.aflag.df = in->flags[5];
    state.aflag.of = in->flags[6];
    for (int i = 0; i < 16; ++i) {
        std::memcpy(&state.vec[i], in->xmm[i], 16);
    }

    memory.fault = on_memory_fault;
    std::memset(&rottr_run_events, 0, sizeof(rottr_run_events));

    struct sigaction action = {}, previous = {};
    action.sa_handler = on_timeout;
    sigemptyset(&action.sa_mask);
    sigaction(SIGALRM, &action, &previous);

    const int result = sigsetjmp(run_exit, 1);
    if (result == RUN_OK) {
        using lifted_fn = void *(*)(State *, uint64_t, void *);
        alarm(timeout_seconds > 0 ? timeout_seconds : 1);
        reinterpret_cast<lifted_fn>(function)(&state, entry, &memory);
    }
    alarm(0);
    sigaction(SIGALRM, &previous, nullptr);

#define GET(i, reg) out->gpr[i] = state.gpr.reg.qword;
    EACH_GPR(GET)
#undef GET
    out->flags[0] = state.aflag.cf;
    out->flags[1] = state.aflag.pf;
    out->flags[2] = state.aflag.af;
    out->flags[3] = state.aflag.zf;
    out->flags[4] = state.aflag.sf;
    out->flags[5] = state.aflag.df;
    out->flags[6] = state.aflag.of;
    out->flags[7] = 0;
    for (int i = 0; i < 16; ++i) {
        std::memcpy(out->xmm[i], &state.vec[i], 16);
    }
    *events = rottr_run_events;
    return result;
}
