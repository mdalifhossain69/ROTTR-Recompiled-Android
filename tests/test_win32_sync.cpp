// Roadmap step 37: Implement events, mutexes, semaphores, critical sections,
// SRW locks, condition variables and waits.
// Pass: stress test runs with no deadlock and correct synchronization.
#include <cstdio>
#include <cstdint>
#include <pthread.h>
#include <vector>
#include <atomic>
#include <chrono>
#include <thread>

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
uint64_t host_GetLastError(uint64_t, uint64_t, uint64_t, uint64_t,
                           uint64_t, uint64_t, uint64_t, uint64_t);
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

struct DelayedEventSignal {
    uint64_t event;
};

static void* signalEventLater(void* arg) {
    const auto* signal = static_cast<DelayedEventSignal*>(arg);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    rottr::host_SetEvent(signal->event, 0, 0, 0, 0, 0, 0, 0);
    return nullptr;
}

struct DelayedSemaphoreRelease {
    uint64_t semaphore;
};

static void* releaseSemaphoreLater(void* arg) {
    const auto* release = static_cast<DelayedSemaphoreRelease*>(arg);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    rottr::host_ReleaseSemaphore(release->semaphore, 1, 0, 0, 0, 0, 0, 0);
    return nullptr;
}

struct MutexReleaseAttempt {
    uint64_t mutex;
    uint64_t result = 0;
};

static void* releaseMutexFromOtherThread(void* arg) {
    auto* attempt = static_cast<MutexReleaseAttempt*>(arg);
    attempt->result =
            rottr::host_ReleaseMutex(attempt->mutex, 0, 0, 0, 0, 0, 0, 0);
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

    constexpr uint64_t kWaitObject0 = 0;
    constexpr uint64_t kWaitTimeout = 0x102;
    constexpr uint64_t kWaitFailed = 0xffffffffULL;

    // ---- Auto-reset event: timeout, signal, and one successful wait ----
    const uint64_t event = rottr::host_CreateEventA(0, 0, 0, 0, 0, 0, 0, 0);
    if (!expect(event != 0, "CreateEventA must succeed") ||
        !expect(rottr::host_WaitForSingleObject(event, 0, 0, 0, 0, 0, 0, 0) ==
                        kWaitTimeout,
                "unsignaled event must time out"))
        return 1;
    DelayedEventSignal delayedSignal{event};
    pthread_t signalThread;
    if (pthread_create(&signalThread, nullptr, signalEventLater,
                       &delayedSignal) != 0 ||
        !expect(rottr::host_WaitForSingleObject(event, 1000, 0, 0, 0, 0, 0, 0) ==
                        kWaitObject0,
                "finite event wait must wake after SetEvent"))
        return 1;
    pthread_join(signalThread, nullptr);
    if (!expect(rottr::host_WaitForSingleObject(event, 0, 0, 0, 0, 0, 0, 0) ==
                        kWaitTimeout,
                "auto-reset event must reset after a successful wait"))
        return 1;
    DelayedEventSignal infiniteSignal{event};
    if (pthread_create(&signalThread, nullptr, signalEventLater,
                       &infiniteSignal) != 0 ||
        !expect(rottr::host_WaitForSingleObject(event, 0xffffffffULL, 0, 0, 0,
                                                0, 0, 0) == kWaitObject0,
                "infinite event wait must wake after SetEvent"))
        return 1;
    pthread_join(signalThread, nullptr);

    // ---- Manual-reset event remains signaled until closed ----
    const uint64_t manualEvent =
            rottr::host_CreateEventA(0, 1, 0, 0, 0, 0, 0, 0);
    if (!expect(manualEvent != 0, "manual-reset event creation must succeed") ||
        !expect(rottr::host_SetEvent(manualEvent, 0, 0, 0, 0, 0, 0, 0) != 0,
                "SetEvent must succeed") ||
        !expect(rottr::host_WaitForSingleObject(manualEvent, 0, 0, 0, 0, 0, 0,
                                                0) == kWaitObject0 &&
                        rottr::host_WaitForSingleObject(manualEvent, 0, 0, 0, 0,
                                                        0, 0, 0) == kWaitObject0,
                "manual-reset event must remain signaled"))
        return 1;

    // ---- Mutex: ownership, recursion, and owner-only release ----
    const uint64_t mutex = rottr::host_CreateMutexA(0, 0, 0, 0, 0, 0, 0, 0);
    if (!expect(mutex != 0, "CreateMutexA must succeed") ||
        !expect(rottr::host_ReleaseMutex(mutex, 0, 0, 0, 0, 0, 0, 0) == 0,
                "ReleaseMutex must fail when the caller does not own it") ||
        !expect(rottr::host_GetLastError(0, 0, 0, 0, 0, 0, 0, 0) == 288,
                "unowned ReleaseMutex must set ERROR_NOT_OWNER") ||
        !expect(rottr::host_WaitForSingleObject(mutex, 0, 0, 0, 0, 0, 0, 0) ==
                        kWaitObject0,
                "unowned mutex must be acquirable") ||
        !expect(rottr::host_WaitForSingleObject(mutex, 0, 0, 0, 0, 0, 0, 0) ==
                        kWaitObject0,
                "mutex must be recursive for its owner") ||
        !expect(rottr::host_ReleaseMutex(mutex, 0, 0, 0, 0, 0, 0, 0) != 0 &&
                        rottr::host_ReleaseMutex(mutex, 0, 0, 0, 0, 0, 0, 0) !=
                                0,
                "owner must release each recursive acquisition"))
        return 1;

    const uint64_t initiallyOwnedMutex =
            rottr::host_CreateMutexA(0, 1, 0, 0, 0, 0, 0, 0);
    if (!expect(initiallyOwnedMutex != 0,
                "initially-owned mutex creation must succeed") ||
        !expect(rottr::host_ReleaseMutex(initiallyOwnedMutex, 0, 0, 0, 0, 0, 0,
                                         0) != 0,
                "initial owner must be able to release mutex"))
        return 1;

    if (!expect(rottr::host_WaitForSingleObject(mutex, 0, 0, 0, 0, 0, 0, 0) ==
                        kWaitObject0,
                "mutex must be acquirable before cross-thread ownership test"))
        return 1;
    MutexReleaseAttempt releaseAttempt{mutex};
    pthread_t releaseThread;
    if (pthread_create(&releaseThread, nullptr, releaseMutexFromOtherThread,
                       &releaseAttempt) != 0)
        return 1;
    pthread_join(releaseThread, nullptr);
    if (!expect(releaseAttempt.result == 0,
                "a non-owner thread must not release a mutex") ||
        !expect(rottr::host_ReleaseMutex(mutex, 0, 0, 0, 0, 0, 0, 0) != 0,
                "mutex owner must retain the ability to release it"))
        return 1;

    // ---- Semaphore: waits, counts, maximum, and finite timeout ----
    const uint64_t sem = rottr::host_CreateSemaphoreA(0, 0, 2, 0, 0, 0, 0, 0);
    if (!expect(sem != 0, "CreateSemaphoreA must succeed") ||
        !expect(rottr::host_CreateSemaphoreA(0, 3, 2, 0, 0, 0, 0, 0) == 0,
                "semaphore creation must reject initial count above maximum") ||
        !expect(rottr::host_WaitForSingleObject(sem, 0, 0, 0, 0, 0, 0, 0) ==
                        kWaitTimeout,
                "empty semaphore must time out"))
        return 1;
    if (!expect(rottr::host_WaitForSingleObject(sem, 20, 0, 0, 0, 0, 0, 0) ==
                        kWaitTimeout,
                "finite wait on an empty semaphore must time out"))
        return 1;
    uint32_t previousCount = 99;
    if (!expect(rottr::host_ReleaseSemaphore(
                        sem, 2, reinterpret_cast<uint64_t>(&previousCount),
                        0, 0, 0, 0, 0) != 0 &&
                        previousCount == 0,
                "ReleaseSemaphore must report the prior count") ||
        !expect(rottr::host_ReleaseSemaphore(sem, 1, 0, 0, 0, 0, 0, 0) == 0,
                "ReleaseSemaphore must reject a release above current capacity") ||
        !expect(rottr::host_WaitForSingleObject(sem, 0, 0, 0, 0, 0, 0, 0) ==
                        kWaitObject0 &&
                        rottr::host_WaitForSingleObject(sem, 0, 0, 0, 0, 0, 0,
                                                        0) == kWaitObject0,
                "semaphore count must permit the matching number of waits") ||
        !expect(rottr::host_WaitForSingleObject(sem, 0, 0, 0, 0, 0, 0, 0) ==
                        kWaitTimeout,
                "semaphore must be empty after consuming its count") ||
        !expect(rottr::host_ReleaseSemaphore(sem, 3, 0, 0, 0, 0, 0, 0) == 0,
                "ReleaseSemaphore must reject a count above its maximum"))
        return 1;
    DelayedSemaphoreRelease delayedRelease{sem};
    pthread_t semaphoreThread;
    if (pthread_create(&semaphoreThread, nullptr, releaseSemaphoreLater,
                       &delayedRelease) != 0 ||
        !expect(rottr::host_WaitForSingleObject(sem, 1000, 0, 0, 0, 0, 0, 0) ==
                        kWaitObject0,
                "finite semaphore wait must wake after a release"))
        return 1;
    pthread_join(semaphoreThread, nullptr);

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
    rottr::host_CloseHandle(manualEvent, 0, 0, 0, 0, 0, 0, 0);
    rottr::host_CloseHandle(mutex, 0, 0, 0, 0, 0, 0, 0);
    rottr::host_CloseHandle(initiallyOwnedMutex, 0, 0, 0, 0, 0, 0, 0);
    rottr::host_CloseHandle(sem, 0, 0, 0, 0, 0, 0, 0);

    if (!expect(rottr::host_WaitForSingleObject(event, 0, 0, 0, 0, 0, 0, 0) ==
                        kWaitFailed,
                "closed synchronization handle must be invalid"))
        return 1;

    std::printf("OK (counter=%llu)\n", (unsigned long long)g_counter);
    return 0;
}
