// Roadmap step 24: Add atomics and fences with x86 memory ordering.
// Pass: multithreaded counter and litmus tests. This host test runs:
//   1. exact single-thread XCHG / XADD / CMPXCHG semantics,
//   2. a multithreaded LOCK ADD counter that must lose no increments,
//   3. a store-buffer (SB) litmus test guarded by MFENCE that must never
//      observe both loads as zero.
// The phone run is still required by the roadmap before the step is closed.
#include "rottr/x64_runtime.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <vector>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

constexpr uint64_t kBase = 0x1000;
constexpr size_t kMemSize = 0x2000;

// ---------------------------------------------------------------------------
// 1. Single-thread semantics for the atomic read-modify-write instructions.
// ---------------------------------------------------------------------------
bool singleThreadSemantics() {
    // mov rbx,0x10d0; mov qword [rbx],10; mov rax,5; xchg [rbx],rax;
    // mov rcx,1; xadd [rbx],rcx; cmpxchg [rbx],rcx; mov rax,6; mov rdx,99;
    // cmpxchg [rbx],rdx; mov rsi,[rbx]; hlt
    constexpr uint8_t code[] = {
        0x48,0xbb,0xd0,0x10,0,0,0,0,0,0,
        0x48,0xc7,0x03,0x0a,0,0,0,
        0x48,0xb8,5,0,0,0,0,0,0,0,
        0x48,0x87,0x03,
        0x48,0xb9,1,0,0,0,0,0,0,0,
        0x48,0x0f,0xc1,0x0b,
        0x48,0x0f,0xb1,0x0b,
        0x48,0xb8,6,0,0,0,0,0,0,0,
        0x48,0xba,99,0,0,0,0,0,0,0,
        0x48,0x0f,0xb1,0x13,
        0x48,0x8b,0x33,
        0xf4,
    };
    std::vector<uint8_t> memory(kMemSize, 0);
    std::memcpy(memory.data(), code, sizeof(code));
    rottr_cpu cpu{};
    rottr_cpu_reset(&cpu, kBase, kBase + 0x1000);
    const rottr::X64GuestMemory guest{kBase, memory.data(), memory.size(), true, true};
    auto result = rottr::runX64Interpreter(cpu, guest, 64);

    uint64_t memValue = 0;
    std::memcpy(&memValue, memory.data() + 0x10d0 - kBase, sizeof(memValue));

    if (!expect(result.reason == rottr::X64StopReason::Halted,
                "atomic semantics program must halt"))
        return false;
    if (!expect(cpu.gpr[0] == 6, "CMPXCHG failure must load RAX with memory"))
        return false;
    if (!expect(cpu.gpr[1] == 5, "XADD must leave the old memory value in RCX"))
        return false;
    if (!expect(cpu.gpr[6] == 99, "CMPXCHG success must store the source"))
        return false;
    if (!expect(memValue == 99, "atomic stores must land in guest memory"))
        return false;
    if (!expect((cpu.rflags & (1ULL << 6)) != 0,
                "successful CMPXCHG must set ZF"))
        return false;
    return true;
}

// ---------------------------------------------------------------------------
// 2. Multithreaded LOCK ADD counter.
// ---------------------------------------------------------------------------
struct CounterJob {
    rottr::X64GuestMemory guest;
    uint64_t iterations;
};

constexpr uint64_t kCounterAddress = 0x1100;

void* counterWorker(void* arg) {
    CounterJob* job = static_cast<CounterJob*>(arg);
    rottr_cpu cpu{};
    rottr_cpu_reset(&cpu, kBase, kBase + 0x1000);
    cpu.gpr[1] = job->iterations;  // rcx = loop counter
    // mov rbx,0x1100; loop: lock add qword [rbx],1; dec rcx; jnz loop; hlt
    rottr::runX64Interpreter(cpu, job->guest, job->iterations * 3 + 16);
    return nullptr;
}

bool multithreadedCounter() {
    constexpr uint8_t code[] = {
        0x48,0xbb,0x00,0x11,0,0,0,0,0,0,
        0xf0,0x48,0x83,0x03,0x01,
        0x48,0xff,0xc9,
        0x75,0xf6,
        0xf4,
    };
    std::vector<uint8_t> memory(kMemSize, 0);
    std::memcpy(memory.data(), code, sizeof(code));
    const rottr::X64GuestMemory guest{kBase, memory.data(), memory.size(), true, true};

    constexpr uint64_t kIterations = 200000;
    constexpr int kThreads = 4;
    CounterJob job{guest, kIterations};
    std::vector<pthread_t> threads(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        if (pthread_create(&threads[i], nullptr, counterWorker, &job) != 0) {
            std::fprintf(stderr, "FAIL: pthread_create\n");
            return false;
        }
    }
    for (int i = 0; i < kThreads; ++i) pthread_join(threads[i], nullptr);

    uint64_t counter = 0;
    std::memcpy(&counter, memory.data() + kCounterAddress - kBase, sizeof(counter));
    const uint64_t expected = kIterations * kThreads;
    if (!expect(counter == expected, "LOCK ADD counter must lose no increments")) {
        std::fprintf(stderr, "  got %llu expected %llu\n",
                     (unsigned long long)counter, (unsigned long long)expected);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 3. Store-buffer litmus test: with MFENCE, (r1==0 && r2==0) is forbidden.
// ---------------------------------------------------------------------------
struct LitmusJob {
    rottr::X64GuestMemory guest;
    uint64_t first;   // address written first
    uint64_t second;  // address loaded afterwards
    uint64_t observed = 0;
    pthread_barrier_t* barrier = nullptr;
};

// mov qword [r8],1; mfence; mov rax,[r9]; hlt
constexpr uint8_t kLitmusCode[] = {
    0x49,0xc7,0x00,0x01,0,0,0,
    0x0f,0xae,0xf0,
    0x49,0x8b,0x01,
    0xf4,
};

void* litmusWorker(void* arg) {
    LitmusJob* job = static_cast<LitmusJob*>(arg);
    rottr_cpu cpu{};
    rottr_cpu_reset(&cpu, kBase, kBase + 0x1000);
    cpu.gpr[8] = job->first;   // r8
    cpu.gpr[9] = job->second;  // r9
    pthread_barrier_wait(job->barrier);
    rottr::runX64Interpreter(cpu, job->guest, 8);
    job->observed = cpu.gpr[0];
    return nullptr;
}

bool storeBufferLitmus() {
    constexpr uint64_t kX = 0x1100;
    constexpr uint64_t kY = 0x1108;
    constexpr int kRounds = 5000;
    std::vector<uint8_t> memory(kMemSize, 0);
    std::memcpy(memory.data(), kLitmusCode, sizeof(kLitmusCode));
    const rottr::X64GuestMemory guest{kBase, memory.data(), memory.size(), true, true};

    pthread_barrier_t barrier;
    pthread_barrier_init(&barrier, nullptr, 3);

    for (int round = 0; round < kRounds; ++round) {
        uint64_t zero = 0;
        std::memcpy(memory.data() + kX - kBase, &zero, 8);
        std::memcpy(memory.data() + kY - kBase, &zero, 8);

        LitmusJob a{guest, kX, kY, 0, &barrier};
        LitmusJob b{guest, kY, kX, 0, &barrier};
        pthread_t ta, tb;
        pthread_create(&ta, nullptr, litmusWorker, &a);
        pthread_create(&tb, nullptr, litmusWorker, &b);
        pthread_barrier_wait(&barrier);
        pthread_join(ta, nullptr);
        pthread_join(tb, nullptr);

        if (a.observed == 0 && b.observed == 0) {
            std::fprintf(stderr,
                         "FAIL: SB litmus observed (0,0) at round %d; "
                         "MFENCE did not order the stores\n", round);
            pthread_barrier_destroy(&barrier);
            return false;
        }
    }
    pthread_barrier_destroy(&barrier);
    return true;
}
}  // namespace

int main() {
    if (!singleThreadSemantics()) return 1;
    if (!multithreadedCounter()) return 1;
    if (!storeBufferLitmus()) return 1;
    std::puts("OK (xchg/xadd/cmpxchg semantics, lock counter, mfence litmus)");
    return 0;
}
