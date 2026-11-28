// Roadmap step 38: Implement timers, clocks and sleep.
// Pass: time never goes backward.
#include <chrono>
#include <cstdio>
#include <cstdint>

extern "C" {
namespace rottr {
uint64_t host_GetTickCount(uint64_t, uint64_t, uint64_t, uint64_t,
                           uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t host_GetTickCount64(uint64_t, uint64_t, uint64_t, uint64_t,
                             uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t host_QueryPerformanceCounter(uint64_t counterPtr, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t);
uint64_t host_QueryPerformanceFrequency(uint64_t freqPtr, uint64_t, uint64_t,
                                        uint64_t, uint64_t, uint64_t, uint64_t,
                                        uint64_t);
uint64_t host_Sleep(uint64_t ms, uint64_t, uint64_t, uint64_t,
                    uint64_t, uint64_t, uint64_t, uint64_t);
}  // namespace rottr
}

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}
}  // namespace

int main() {
    // ---- QueryPerformanceFrequency ----
    uint64_t freq = 0;
    if (!expect(rottr::host_QueryPerformanceFrequency(
                    reinterpret_cast<uint64_t>(&freq), 0, 0, 0, 0, 0, 0, 0) != 0,
                "QueryPerformanceFrequency must succeed"))
        return 1;
    if (!expect(freq > 0, "QueryPerformanceFrequency must be nonzero")) return 1;

    // ---- QueryPerformanceCounter is monotonic and tracks elapsed time ----
    uint64_t c1 = 0, c2 = 0;
    rottr::host_QueryPerformanceCounter(reinterpret_cast<uint64_t>(&c1),
                                        0, 0, 0, 0, 0, 0, 0);
    rottr::host_Sleep(30, 0, 0, 0, 0, 0, 0, 0);
    rottr::host_QueryPerformanceCounter(reinterpret_cast<uint64_t>(&c2),
                                        0, 0, 0, 0, 0, 0, 0);
    if (!expect(c2 >= c1, "QueryPerformanceCounter must never go backward"))
        return 1;
    const uint64_t elapsedNs = (c2 - c1) * 1000000000ULL / freq;
    if (!expect(elapsedNs >= 20000000ULL,
                "QueryPerformanceCounter must advance across Sleep(30)"))
        return 1;

    // ---- GetTickCount64 monotonic and advances ----
    uint64_t t1 = rottr::host_GetTickCount64(0, 0, 0, 0, 0, 0, 0, 0);
    rottr::host_Sleep(30, 0, 0, 0, 0, 0, 0, 0);
    uint64_t t2 = rottr::host_GetTickCount64(0, 0, 0, 0, 0, 0, 0, 0);
    if (!expect(t2 >= t1, "GetTickCount64 must never go backward")) return 1;
    if (!expect(t2 - t1 >= 20, "GetTickCount64 must advance across Sleep(30)"))
        return 1;

    // ---- GetTickCount non-decreasing across several reads ----
    uint32_t last = static_cast<uint32_t>(
        rottr::host_GetTickCount(0, 0, 0, 0, 0, 0, 0, 0));
    for (int i = 0; i < 16; ++i) {
        uint32_t now = static_cast<uint32_t>(
            rottr::host_GetTickCount(0, 0, 0, 0, 0, 0, 0, 0));
        if (!expect(now >= last, "GetTickCount must never go backward")) return 1;
        last = now;
    }

    // ---- Sleep actually blocks for roughly the requested time ----
    auto start = std::chrono::steady_clock::now();
    rottr::host_Sleep(60, 0, 0, 0, 0, 0, 0, 0);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();
    if (!expect(elapsed >= 40, "Sleep(60) must block for at least 40 ms")) {
        std::fprintf(stderr, "  slept %lld ms\n", (long long)elapsed);
        return 1;
    }

    std::puts("OK (qpc monotonic, tick counts monotonic, sleep blocks)");
    return 0;
}
