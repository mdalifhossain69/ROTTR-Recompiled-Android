// Roadmap step 47: executable memory with explicit instruction-cache flushing.
//
// The buffer is writable while code is being emitted, then sealed read+execute.
// Patching requires unseal(), which makes it writable again; seal() always
// flushes the instruction cache before the code can run.
#pragma once

#include <cstddef>
#include <cstdint>

namespace rottr {
namespace arm64 {

class ExecutableMemory {
public:
    ExecutableMemory() = default;
    explicit ExecutableMemory(std::size_t bytes);
    ~ExecutableMemory();

    ExecutableMemory(const ExecutableMemory&) = delete;
    ExecutableMemory& operator=(const ExecutableMemory&) = delete;
    ExecutableMemory(ExecutableMemory&& other) noexcept;
    ExecutableMemory& operator=(ExecutableMemory&& other) noexcept;

    bool allocate(std::size_t bytes);
    void release();

    bool valid() const { return memory_ != nullptr; }
    bool executable() const { return sealed_; }
    std::size_t capacity() const { return capacity_; }

    bool write(std::size_t offset, const void* source, std::size_t bytes);
    bool writeWords(std::size_t offset, const uint32_t* words, std::size_t count);

    // Make the whole buffer read+execute and flush the instruction cache.
    bool seal();
    // Make the whole buffer writable again so it can be patched.
    bool unseal();

    void flushInstructionCache();
    void* entry(std::size_t offset = 0) const;

private:
    void* memory_ = nullptr;
    std::size_t capacity_ = 0;
    std::size_t mapped_ = 0;
    bool writable_ = false;
    bool sealed_ = false;
};

}  // namespace arm64
}  // namespace rottr
