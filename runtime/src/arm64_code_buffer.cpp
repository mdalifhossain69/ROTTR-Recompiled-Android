#include "rottr/arm64_code_buffer.h"

#include <cstring>
#include <utility>
#include <sys/mman.h>
#include <unistd.h>

namespace rottr {
namespace arm64 {
namespace {

std::size_t pageSize() {
    const long size = sysconf(_SC_PAGESIZE);
    return size > 0 ? static_cast<std::size_t>(size) : 4096;
}

std::size_t roundUp(std::size_t value, std::size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

}  // namespace

ExecutableMemory::ExecutableMemory(std::size_t bytes) { allocate(bytes); }

ExecutableMemory::~ExecutableMemory() { release(); }

ExecutableMemory::ExecutableMemory(ExecutableMemory&& other) noexcept {
    *this = std::move(other);
}

ExecutableMemory& ExecutableMemory::operator=(ExecutableMemory&& other) noexcept {
    if (this != &other) {
        release();
        memory_ = other.memory_;
        capacity_ = other.capacity_;
        mapped_ = other.mapped_;
        writable_ = other.writable_;
        sealed_ = other.sealed_;
        other.memory_ = nullptr;
        other.capacity_ = 0;
        other.mapped_ = 0;
        other.writable_ = false;
        other.sealed_ = false;
    }
    return *this;
}

bool ExecutableMemory::allocate(std::size_t bytes) {
    release();
    if (bytes == 0) return false;
    const std::size_t mapped = roundUp(bytes, pageSize());
    void* memory = mmap(nullptr, mapped, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) return false;
    memory_ = memory;
    capacity_ = bytes;
    mapped_ = mapped;
    writable_ = true;
    sealed_ = false;
    return true;
}

void ExecutableMemory::release() {
    if (memory_ != nullptr) {
        munmap(memory_, mapped_);
    }
    memory_ = nullptr;
    capacity_ = 0;
    mapped_ = 0;
    writable_ = false;
    sealed_ = false;
}

bool ExecutableMemory::write(std::size_t offset, const void* source,
                             std::size_t bytes) {
    if (!writable_ || memory_ == nullptr || source == nullptr) return false;
    if (offset > capacity_ || bytes > capacity_ - offset) return false;
    std::memcpy(static_cast<uint8_t*>(memory_) + offset, source, bytes);
    return true;
}

bool ExecutableMemory::writeWords(std::size_t offset, const uint32_t* words,
                                  std::size_t count) {
    if (words == nullptr) return false;
    if (count > static_cast<std::size_t>(-1) / sizeof(uint32_t)) return false;
    return write(offset, words, count * sizeof(uint32_t));
}

bool ExecutableMemory::seal() {
    if (memory_ == nullptr) return false;
    if (sealed_) return true;
    if (mprotect(memory_, mapped_, PROT_READ | PROT_EXEC) != 0) return false;
    writable_ = false;
    sealed_ = true;
    flushInstructionCache();
    return true;
}

bool ExecutableMemory::unseal() {
    if (memory_ == nullptr) return false;
    if (!sealed_) {
        writable_ = true;
        return true;
    }
    if (mprotect(memory_, mapped_, PROT_READ | PROT_WRITE) != 0) return false;
    writable_ = true;
    sealed_ = false;
    return true;
}

void ExecutableMemory::flushInstructionCache() {
    if (memory_ == nullptr) return;
#if defined(__GNUC__) || defined(__clang__)
    __builtin___clear_cache(static_cast<char*>(memory_),
                            static_cast<char*>(memory_) + mapped_);
#else
#error "no instruction-cache flush primitive for this compiler"
#endif
}

void* ExecutableMemory::entry(std::size_t offset) const {
    if (memory_ == nullptr || offset >= capacity_) return nullptr;
    return static_cast<uint8_t*>(memory_) + offset;
}

}  // namespace arm64
}  // namespace rottr
