#include "rottr/arm64_block_cache.h"
#include "rottr/x64_runtime.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using Bytes = std::vector<uint8_t>;
using Clock = std::chrono::steady_clock;

void appendMovAdd(Bytes& out) {
    out.push_back(0x48);
    out.push_back(0xb8);
    out.push_back(5);
    for (int i = 1; i < 8; ++i) out.push_back(0);
    out.insert(out.end(), {0x48, 0x83, 0xc0, 0x07});
}

uint64_t execute(bool cached, rottr::X64GuestMemory& memory,
                 rottr::arm64::BlockCache* cache) {
    rottr_cpu cpu{};
    cpu.rip = memory.base;
    if (cached) {
        rottr::arm64::runX64Translated(cpu, memory, 2, *cache);
    } else {
        rottr::arm64::runX64Translated(cpu, memory, 2);
    }
    return cpu.gpr[0];
}

}  // namespace

int main() {
    if (!rottr::arm64::hasArm64TranslationBackend()) {
        std::puts("SKIP: host is not AArch64; native benchmark did not run");
        return 77;
    }

    Bytes bytes;
    appendMovAdd(bytes);
    bytes.resize(0x100, 0);
    rottr::X64GuestMemory memory{0x1000, bytes.data(), bytes.size(), true, true};

    constexpr int iterations = 2000;
    const uint64_t uncached = execute(false, memory, nullptr);

    rottr::arm64::BlockCache cache;
    const uint64_t cached = execute(true, memory, &cache);
    if (uncached != cached || cached != 12) {
        std::printf("FAIL: cached and uncached results differ (%llu vs %llu)\n",
                    static_cast<unsigned long long>(uncached),
                    static_cast<unsigned long long>(cached));
        return 1;
    }

    const auto uncachedStart = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        if (execute(false, memory, nullptr) != cached) return 1;
    }
    const auto uncachedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                Clock::now() - uncachedStart).count();

    const auto cachedStart = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        if (execute(true, memory, &cache) != cached) return 1;
    }
    const auto cachedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                              Clock::now() - cachedStart).count();

    if (cache.stats().hits == 0) {
        std::puts("FAIL: benchmark did not hit cached blocks");
        return 1;
    }
    std::printf("uncached=%lldms cached=%lldms hits=%zu\n",
                static_cast<long long>(uncachedNs / 1000000),
                static_cast<long long>(cachedNs / 1000000), cache.stats().hits);

    std::printf("OK (cached translation matched results with %zu block hits)\n",
                cache.stats().hits);
    return 0;
}