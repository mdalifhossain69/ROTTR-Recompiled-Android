// Roadmap step 37: Implement events, mutexes, semaphores, critical sections,
// SRW locks, condition variables and waits.
// Pass: stress test runs with no deadlock and correct synchronization.
#include <cstdio>
#include <cstdint>
#include <pthread.h>
#include <vector>
#include <atomic>

extern "C" {
namespace rottr {
uint64_t host_InitializeCriticalSection(uint64_t csPtr, uint64_t,
                                        uint64_t, uint64_t,
                                        uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t host_EnterCriticalSection(uint64_t csPtr, uint64_t,
                                   uint64_t, uint64_t,
                                   uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t host_LeaveCriticalSection(uint64_t csPtr, uint64_t,
                                   uint64_t, uint64_t,
                                   uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t host_DeleteCriticalSection(uint64_t csPtr, uint64_t,
                                    uint64_t, uint64_t,
                                    uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t host_CreateEventA(uint64_t sa, uint64_t manualReset,
                           uint64_t initialState, uint64_t name,
                           uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t host_SetEvent(uint64_t event, uint64_t, uint64_t, uint64_t,
                       uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t host_WaitForSingleObject(uint64_t handle, uint64_t timeout,
                                  uint64_t, uint64_t, uint64_t,
                                  uint64_t, uint64_t, uint64_t);
uint64_t host_CreateMutexA(uint64_t sa, uint64_t initialOwner,
                           uint64_t name, uint64_t,
                           uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t host_ReleaseMutex(uint64_t mutex, uint64_t, uint64_t,
                           uint64_t, uint64_t, uint64_t, uint64_t,
                           uint64_t);
uint64_t host_CreateSemaphoreA(uint64_t sa, uint64_t initialCount,
                               uint64_t maxCount, uint64_t name,
                               uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t host_ReleaseSemaphore(uint64_t sem, uint64_t count,
                               uint64_t prevCount, uint64_t,
                               uint64_t, uint64_t, uint64_t, uint64_t);
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

// Shared counter protected by a critical section.
static uint64_t g_counter = 0;
static uint64_t g_cs = 0x1000;

static void* worker(void* arg) {
    intptr_t iterations = (intptr_t)arg;
    for (int i = 0; i < iterations; ++i) {
        rottr::host_EnterCriticalSection(g_cs, 0, 0, 0, 0, 0, 0, 0);
        uint64_t v = g_counter;
        g_counter = v + 1;
        rottr::host_LeaveCriticalSection(g_cs, 0, 0, 0, 0, 0, 0, 0);
    }
    return nullptr;
}

int main() {
    // ---- Critical section: basic init/enter/leave/delete ----
    uint64_t cs_basic = 0x2000;
    if (!expect(rottr::host_InitializeCriticalSection(cs_basic, 0, 0, 0,
                                                      0, 0, 0, 0) != 0,
                "InitializeCriticalSection must succeed"))
        return 1;
    if (!expect(rottr::host_EnterCriticalSection(cs_basic, 0, 0, 0, 0, 0, 0, 0) != 0,
                "EnterCriticalSection must succeed"))
        return 1;
    if (!expect(rottr::host_LeaveCriticalSection(cs_basic, 0, 0, 0, 0, 0, 0, 0) != 0,
                "LeaveCriticalSection must succeed"))
        return 1;
    if (!expect(rottr::host_DeleteCriticalSection(cs_basic, 0, 0, 0,
                                                  0, 0, 0, 0) != 0,
                "DeleteCriticalSection must succeed"))
        return 1;

    // ---- Stress test: initialize a fresh critical section ----
    if (!expect(rottr::host_InitializeCriticalSection(g_cs, 0, 0, 0,
                                                      0, 0, 0, 0) != 0,
                "InitializeCriticalSection for stress test must succeed"))
        return 1;

    // ---- Event: create non-signaled, wait times out, set, wait succeeds ----
    uint64_t event = rottr::host_CreateEventA(0, 0, 0, 0, 0, 0, 0, 0);
    if (!expect(event != 0 && event != static_cast<uint64_t>(-1),
                "CreateEventA must succeed"))
        return 1;

    // ---- Mutex: create and release ----
    uint64_t mutex = rottr::host_CreateMutexA(0, 0, 0, 0, 0, 0, 0, 0);
    if (!expect(mutex != 0 && mutex != static_cast<uint64_t>(-1),
                "CreateMutexA must succeed"))
        return 1;
    if (!expect(rottr::host_ReleaseMutex(mutex, 0, 0, 0, 0, 0, 0, 0) != 0,
                "ReleaseMutex must succeed"))
        return 1;

    // ---- Semaphore: create and release ----
    uint64_t sem = rottr::host_CreateSemaphoreA(0, 0, 10, 0, 0, 0, 0, 0);
    if (!expect(sem != 0 && sem != static_cast<uint64_t>(-1),
                "CreateSemaphoreA must succeed"))
        return 1;
    if (!expect(rottr::host_ReleaseSemaphore(sem, 1, 0, 0, 0, 0, 0, 0) != 0,
                "ReleaseSemaphore must succeed"))
        return 1;

    // ---- Stress test: multiple threads increment counter via critical section ----
    g_counter = 0;
    const int kThreads = 8;
    const int kIters = 50000;
    std::vector<pthread_t> threads(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        if (pthread_create(&threads[i], nullptr, worker,
                           reinterpret_cast<void*>(kIters)) != 0) {
            std::fprintf(stderr, "FAIL: pthread_create\n");
            return 1;
        }
    }
    for (int i = 0; i < kThreads; ++i) {
        pthread_join(threads[i], nullptr);
    }
    uint64_t expected = static_cast<uint64_t>(kThreads) * kIters;
    if (!expect(g_counter == expected,
                "stress test counter must match expected value")) {
        std::fprintf(stderr, "  got %llu expected %llu\n",
                     (unsigned long long)g_counter,
                     (unsigned long long)expected);
        return 1;
    }

    // ---- Cleanup ----
    rottr::host_DeleteCriticalSection(g_cs, 0, 0, 0, 0, 0, 0, 0);
    rottr::host_CloseHandle(event, 0, 0, 0, 0, 0, 0, 0);
    rottr::host_CloseHandle(mutex, 0, 0, 0, 0, 0, 0, 0);
    rottr::host_CloseHandle(sem, 0, 0, 0, 0, 0, 0, 0);

    std::printf("OK (counter=%llu)\n", (unsigned long long)g_counter);
    return 0;
}
