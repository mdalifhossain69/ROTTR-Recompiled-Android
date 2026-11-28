#include "rottr/arm64_block_cache.h"
#include "rottr/arm64_block_translator.h"
#include "rottr/arm64_code_buffer.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>

namespace rottr {
namespace arm64 {

struct BlockCache::Entry {
    TranslationResult translation;
    ExecutableMemory native;
    std::vector<uint8_t> source;
};

namespace {

bool readSource(const X64GuestMemory& memory, uint64_t address, size_t size,
                std::vector<uint8_t>& out) {
    if (size == 0 || address < memory.base) return false;
    const size_t offset = static_cast<size_t>(address - memory.base);
    if (offset + size > memory.size) return false;
    out.assign(memory.data + offset, memory.data + offset + size);
    return true;
}

}  // namespace

BlockCache::BlockCache() = default;
BlockCache::~BlockCache() = default;

void BlockCache::invalidateAll() {
    stats_.invalidations += entries_.size();
    entries_.clear();
}

void BlockCache::invalidateRange(uint64_t address, size_t size) {
    if (size == 0) return;
    const uint64_t end = address + size;
    for (auto it = entries_.begin(); it != entries_.end();) {
        const TranslationResult& block = it->second->translation;
        const uint64_t blockEnd = block.endRip;
        const bool overlaps = block.startRip < end && address < blockEnd;
        if (overlaps) {
            it = entries_.erase(it);
            ++stats_.invalidations;
        } else {
            ++it;
        }
    }
}

BlockCache::Entry* BlockCache::lookup(const X64GuestMemory& memory, uint64_t rip,
                                      size_t maxInstructions) {
    auto it = entries_.find(rip);
    if (it != entries_.end()) {
        std::vector<uint8_t> current;
        const TranslationResult& previous = it->second->translation;
        const size_t sourceSize = static_cast<size_t>(previous.endRip - previous.startRip);
        if (readSource(memory, previous.startRip, sourceSize, current) &&
            current == it->second->source) {
            ++stats_.hits;
            return it->second.get();
        }
        entries_.erase(it);
        ++stats_.invalidations;
    }

    ++stats_.misses;
    TranslationResult block = translateBasicBlock(memory, rip, maxInstructions);
    if (block.status != TranslationResult::Status::Translated || block.code.empty()) {
        return nullptr;
    }

    auto entry = std::make_unique<Entry>();
    entry->translation = std::move(block);
    const size_t sourceSize = static_cast<size_t>(entry->translation.endRip -
                                                  entry->translation.startRip);
    if (!readSource(memory, entry->translation.startRip, sourceSize, entry->source) ||
        !entry->native.allocate(entry->translation.code.size() * sizeof(uint32_t)) ||
        !entry->native.writeWords(0, entry->translation.code.data(),
                                  entry->translation.code.size()) ||
        !entry->native.seal()) {
        return nullptr;
    }
    Entry* result = entry.get();
    entries_.emplace(rip, std::move(entry));
    return result;
}

X64RunResult runX64Translated(rottr_cpu& cpu, const X64GuestMemory& memory,
                              uint64_t instructionBudget, BlockCache& cache) {
    X64RunResult result;
    result.reason = X64StopReason::InstructionBudgetExceeded;
    uint64_t executed = 0;
#if defined(__aarch64__)
    while (executed < instructionBudget) {
        const size_t maxInstructions = static_cast<size_t>(
            std::min<uint64_t>(32, instructionBudget - executed));
        BlockCache::Entry* block = cache.lookup(memory, cpu.rip, maxInstructions);
        if (block != nullptr) {
            using BlockFn = void (*)(rottr_cpu*);
            BlockFn fn = reinterpret_cast<BlockFn>(block->native.entry());
            fn(&cpu);
            executed += block->translation.instructionCount;
            continue;
        }

        X64RunResult step = runX64Interpreter(cpu, memory, 1);
        executed += step.instructionsExecuted;
        const bool interpreterStopped =
            step.instructionsExecuted == 0 ||
            step.reason == X64StopReason::Halted ||
            step.reason == X64StopReason::Returned ||
            step.reason == X64StopReason::InvalidMemory ||
            step.reason == X64StopReason::ReadOnlyMemory ||
            step.reason == X64StopReason::UnsupportedInstruction ||
            (step.reason == X64StopReason::InvalidCpuState && step.instructionsExecuted == 0);
        if (interpreterStopped) {
            result.reason = step.reason;
            result.detail = std::move(step.detail);
            result.faultAddress = step.faultAddress;
            result.instructionsExecuted = executed;
            return result;
        }
    }
#else
    result = runX64Interpreter(cpu, memory, instructionBudget);
    result.detail = "host is not AArch64; translated blocks were not executed";
#endif
    result.instructionsExecuted = executed;
    return result;
}

}  // namespace arm64
}  // namespace rottr