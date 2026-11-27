// Roadmap step 36: Implement threads, thread IDs and per-thread TLS.
// Pass: 32 threads keep separate values and exit cleanly.
#include <cstdio>
#include <cstdint>
#include <pthread.h>
#include <vector>
#include <atomic>

extern "C" {
namespace rottr {
uint64_t host_CreateThread(uint64_t sa, uint64_t stackSize,
                           uint64_t startAddr, uint64_t param,
                           uint64_t flags, uint64_t threadIdPtr,
                           uint64_t, uint64_t);
uint64_t host_ExitThread(uint64_t exitCode, uint64_t, uint64_t, uint64_t,
                         uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t host_GetCurrentThreadId(uint64_t, uint64_t, uint64_t, uint64_t,
                                 uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t host_GetCurrentProcessId(uint64_t, uint64_t, uint64_t, uint64_t,
                                  uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t host_WaitForSingleObject(uint64_t handle, uint64_t timeout,
                                  uint64_t, uint64_t, uint64_t,
                                  uint64_t, uint64_t, uint64_t);
uint64_t host_CloseHandle(uint64_t handle, uint64_t, uint64_t,
                          uint64_t, uint64_t, uint64_t,
                          uint64_t, uint64_t);
}  // namespace rottr
}

namespace {
[[maybe_unused]] bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}
}  // namespace

static std::atomic<int> g_threadCounter{0};
static std::atomic<int> g_threadIdSeen[32]{};

static uint64_t threadProc(uint64_t param) {
    int index = static_cast<int>(param);
    g_threadIdSeen[index].store(1);
    g_threadCounter.fetch_add(1);
    // Simulate some work
    for (volatile int i = 0; i < 100000; ++i) {}
    return 0;
}

int main() {
    // ---- GetCurrentProcessId returns nonzero ----
    uint64_t pid = rottr::host_GetCurrentProcessId(0, 0, 0, 0, 0, 0, 0, 0);
    if (!expect(pid != 0, "GetCurrentProcessId must return nonzero"))
        return 1;

    // ---- GetCurrentThreadId returns nonzero for main thread ----
    uint64_t mainTid = rottr::host_GetCurrentThreadId(0, 0, 0, 0, 0, 0, 0, 0);
    if (!expect(mainTid != 0, "GetCurrentThreadId must return nonzero"))
        return 1;

    // ---- Create 32 threads, each tracks its own index ----
    const int kThreads = 32;
    std::vector<uint64_t> handles(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        uint64_t h = rottr::host_CreateThread(0, 0,
                                              reinterpret_cast<uint64_t>(&threadProc),
                                              static_cast<uint64_t>(i),
                                              0, 0, 0, 0);
        if (!expect(h != 0 && h != static_cast<uint64_t>(-1),
                    "CreateThread must succeed"))
            return 1;
        handles[i] = h;
    }

    // ---- Wait for all threads to finish ----
    for (int i = 0; i < kThreads; ++i) {
        uint64_t result = rottr::host_WaitForSingleObject(handles[i],
                                                          0xFFFFFFFF,
                                                          0, 0, 0, 0, 0, 0);
        if (!expect(result == 0, "WaitForSingleObject on thread must succeed"))
            return 1;
        rottr::host_CloseHandle(handles[i], 0, 0, 0, 0, 0, 0, 0);
    }

    // ---- Verify all threads ran ----
    if (!expect(g_threadCounter.load() == kThreads,
                "all threads must have run")) {
        std::fprintf(stderr, "  counter=%d expected %d\n",
                     g_threadCounter.load(), kThreads);
        return 1;
    }

    for (int i = 0; i < kThreads; ++i) {
        if (!expect(g_threadIdSeen[i].load() == 1,
                    "each thread index must have been seen")) {
            std::fprintf(stderr, "  thread %d not seen\n", i);
            return 1;
        }
    }

    std::printf("OK (%d threads, counter=%d)\n", kThreads, g_threadCounter.load());
    return 0;
}
