// Native runtime for Remill-lifted x86-64 functions. Test harness only.
//
// Implements the Remill helper functions that lifted code calls, over a small
// set of memory regions supplied by the caller, and runs one lifted function
// from a given register snapshot.
#include <csetjmp>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <sys/time.h>

#include "remill/Arch/X86/Runtime/State.h"

namespace {

struct Region {
  uint64_t base;
  uint64_t size;
  uint8_t *host;
  int writable;
};

constexpr int kMaxRegions = 8;
Region g_regions[kMaxRegions];
int g_region_count = 0;

volatile int g_fault = 0;     // access outside the regions, or write to read-only
volatile int g_error = 0;     // __remill_error reached
volatile int g_external = 0;  // lifted code called or jumped elsewhere
volatile int g_undef = 0;     // an undefined value was requested
sigjmp_buf g_jump;

uint8_t *Resolve(uint64_t addr, uint64_t len, bool write) {
  for (int i = 0; i < g_region_count; ++i) {
    const Region &r = g_regions[i];
    if (addr >= r.base && len <= r.size && addr - r.base <= r.size - len) {
      if (write && !r.writable) break;
      return r.host + (addr - r.base);
    }
  }
  g_fault = 1;
  return nullptr;
}

template <typename T>
T ReadMem(uint64_t addr) {
  T value{};
  if (uint8_t *p = Resolve(addr, sizeof(T), false)) memcpy(&value, p, sizeof(T));
  return value;
}

template <typename T>
void WriteMem(uint64_t addr, T value) {
  if (uint8_t *p = Resolve(addr, sizeof(T), true)) memcpy(p, &value, sizeof(T));
}

void OnAlarm(int) { siglongjmp(g_jump, 1); }

}  // namespace

extern "C" {

// ---- Remill runtime functions -------------------------------------------

uint8_t __remill_read_memory_8(void *, uint64_t a) { return ReadMem<uint8_t>(a); }
uint16_t __remill_read_memory_16(void *, uint64_t a) { return ReadMem<uint16_t>(a); }
uint32_t __remill_read_memory_32(void *, uint64_t a) { return ReadMem<uint32_t>(a); }
uint64_t __remill_read_memory_64(void *, uint64_t a) { return ReadMem<uint64_t>(a); }
float __remill_read_memory_f32(void *, uint64_t a) { return ReadMem<float>(a); }
double __remill_read_memory_f64(void *, uint64_t a) { return ReadMem<double>(a); }

void *__remill_write_memory_8(void *m, uint64_t a, uint8_t v) { WriteMem(a, v); return m; }
void *__remill_write_memory_16(void *m, uint64_t a, uint16_t v) { WriteMem(a, v); return m; }
void *__remill_write_memory_32(void *m, uint64_t a, uint32_t v) { WriteMem(a, v); return m; }
void *__remill_write_memory_64(void *m, uint64_t a, uint64_t v) { WriteMem(a, v); return m; }
void *__remill_write_memory_f32(void *m, uint64_t a, float v) { WriteMem(a, v); return m; }
void *__remill_write_memory_f64(void *m, uint64_t a, double v) { WriteMem(a, v); return m; }

bool __remill_flag_computation_zero(bool r, ...) { return r; }
bool __remill_flag_computation_sign(bool r, ...) { return r; }
bool __remill_flag_computation_overflow(bool r, ...) { return r; }
bool __remill_flag_computation_carry(bool r, ...) { return r; }

bool __remill_compare_eq(bool r) { return r; }
bool __remill_compare_neq(bool r) { return r; }
bool __remill_compare_sle(bool r) { return r; }
bool __remill_compare_slt(bool r) { return r; }
bool __remill_compare_sgt(bool r) { return r; }
bool __remill_compare_sge(bool r) { return r; }
bool __remill_compare_ule(bool r) { return r; }
bool __remill_compare_ult(bool r) { return r; }
bool __remill_compare_ugt(bool r) { return r; }
bool __remill_compare_uge(bool r) { return r; }

uint8_t __remill_undefined_8(void) { ++g_undef; return 0; }
uint16_t __remill_undefined_16(void) { ++g_undef; return 0; }
uint32_t __remill_undefined_32(void) { ++g_undef; return 0; }
uint64_t __remill_undefined_64(void) { ++g_undef; return 0; }
float __remill_undefined_f32(void) { ++g_undef; return 0; }
double __remill_undefined_f64(void) { ++g_undef; return 0; }

void *__remill_function_return(void *, uint64_t, void *m) { return m; }
void *__remill_function_call(void *, uint64_t, void *m) { g_external = 1; return m; }
void *__remill_jump(void *, uint64_t, void *m) { g_external = 1; return m; }
void *__remill_missing_block(void *, uint64_t, void *m) { g_external = 1; return m; }
void *__remill_async_hyper_call(void *, uint64_t, void *m) { g_external = 1; return m; }
void *__remill_error(void *, uint64_t, void *m) { g_error = 1; return m; }

void *__remill_barrier_load_load(void *m) { return m; }
void *__remill_barrier_load_store(void *m) { return m; }
void *__remill_barrier_store_load(void *m) { return m; }
void *__remill_barrier_store_store(void *m) { return m; }
void *__remill_atomic_begin(void *m) { return m; }
void *__remill_atomic_end(void *m) { return m; }

// Called by generated stubs for functions that are not part of the sample.
void harness_note_external(void) { g_external = 1; }

// ---- Harness API --------------------------------------------------------

// Register order: rax rbx rcx rdx rsi rdi rsp rbp r8 r9 r10 r11 r12 r13 r14 r15
// Flag order: cf pf af zf sf df of
struct Snapshot {
  uint64_t gpr[16];
  uint8_t flags[8];
  uint8_t xmm[16][16];
};

void harness_clear_regions(void) { g_region_count = 0; }

int harness_add_region(uint64_t base, uint64_t size, uint8_t *host, int writable) {
  if (g_region_count >= kMaxRegions) return -1;
  g_regions[g_region_count++] = Region{base, size, host, writable};
  return 0;
}

uint64_t harness_state_size(void) { return sizeof(State); }

// Returns 0 ok, 1 memory fault, 2 error intrinsic, 3 timeout.
// info = {undefined values requested, external calls, error intrinsics, faults}
int harness_run(void *fn, uint64_t pc, const Snapshot *in, Snapshot *out,
                int timeout_ms, uint32_t *info) {
  alignas(16) static State st;
  memset(&st, 0, sizeof(st));

  Reg *regs[16] = {&st.gpr.rax, &st.gpr.rbx, &st.gpr.rcx, &st.gpr.rdx,
                   &st.gpr.rsi, &st.gpr.rdi, &st.gpr.rsp, &st.gpr.rbp,
                   &st.gpr.r8,  &st.gpr.r9,  &st.gpr.r10, &st.gpr.r11,
                   &st.gpr.r12, &st.gpr.r13, &st.gpr.r14, &st.gpr.r15};
  for (int i = 0; i < 16; ++i) regs[i]->qword = in->gpr[i];
  st.gpr.rip.qword = pc;

  uint8_t *flags[7] = {&st.aflag.cf, &st.aflag.pf, &st.aflag.af, &st.aflag.zf,
                       &st.aflag.sf, &st.aflag.df, &st.aflag.of};
  for (int i = 0; i < 7; ++i) *flags[i] = in->flags[i];
  for (int i = 0; i < 16; ++i) memcpy(&st.vec[i], in->xmm[i], 16);

  g_fault = g_error = g_external = g_undef = 0;

  struct sigaction sa, old;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = OnAlarm;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGALRM, &sa, &old);
  itimerval tv;
  memset(&tv, 0, sizeof(tv));
  tv.it_value.tv_sec = timeout_ms / 1000;
  tv.it_value.tv_usec = (timeout_ms % 1000) * 1000;

  int rc = 0;
  static char memory_token;
  typedef void *(*LiftedFn)(void *, uint64_t, void *);
  setitimer(ITIMER_REAL, &tv, nullptr);
  if (sigsetjmp(g_jump, 1) == 0) {
    reinterpret_cast<LiftedFn>(fn)(&st, pc, &memory_token);
  } else {
    rc = 3;
  }
  itimerval off;
  memset(&off, 0, sizeof(off));
  setitimer(ITIMER_REAL, &off, nullptr);
  sigaction(SIGALRM, &old, nullptr);

  for (int i = 0; i < 16; ++i) out->gpr[i] = regs[i]->qword;
  for (int i = 0; i < 7; ++i) out->flags[i] = *flags[i];
  out->flags[7] = 0;
  for (int i = 0; i < 16; ++i) memcpy(out->xmm[i], &st.vec[i], 16);

  info[0] = static_cast<uint32_t>(g_undef);
  info[1] = static_cast<uint32_t>(g_external);
  info[2] = static_cast<uint32_t>(g_error);
  info[3] = static_cast<uint32_t>(g_fault);
  if (rc == 0) rc = g_fault ? 1 : (g_error ? 2 : 0);
  return rc;
}

}  // extern "C"
