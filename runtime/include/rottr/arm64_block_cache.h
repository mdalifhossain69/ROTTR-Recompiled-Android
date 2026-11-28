#pragma once

#include "rottr/arm64_translator_runtime.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace rottr {
namespace arm64 {

class BlockCache {
public:
    struct Stats {
        size_t hits = 0;
        size_t misses = 0;
        size_t invalidations = 0;
    };

    BlockCache();
    ~BlockCache();

    BlockCache(const BlockCache&) = delete;
    BlockCache& operator=(const BlockCache&) = delete;

    void invalidateAll();
    void invalidateRange(uint64_t address, size_t size);
    const Stats& stats() const { return stats_; }

private:
    friend X64RunResult runX64Translated(rottr_cpu& cpu, const X64GuestMemory& memory,
                                         uint64_t instructionBudget, BlockCache& cache);

    struct Entry;
    Entry* lookup(const X64GuestMemory& memory, uint64_t rip, size_t maxInstructions);

    std::unordered_map<uint64_t, std::unique_ptr<Entry>> entries_;
    Stats stats_;
};

X64RunResult runX64Translated(rottr_cpu& cpu, const X64GuestMemory& memory,
                              uint64_t instructionBudget, BlockCache& cache);

}  // namespace arm64
}  // namespace rottr