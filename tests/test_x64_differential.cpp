#include "rottr/x64_runtime.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace {

struct NativeResult {
    uint64_t value = 0;
    uint64_t flags = 0;
};

struct Operation {
    uint8_t opcode;
    const char* name;
    uint64_t flagsMask;
};

bool runNative(const std::vector<uint8_t>& code, NativeResult& result) {
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0 || code.size() > static_cast<size_t>(pageSize))
        return false;
    void* page = mmap(nullptr, static_cast<size_t>(pageSize),
                      PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1,
                      0);
    if (page == MAP_FAILED) return false;
    std::memcpy(page, code.data(), code.size());
    if (mprotect(page, static_cast<size_t>(pageSize),
                 PROT_READ | PROT_EXEC) != 0) {
        munmap(page, static_cast<size_t>(pageSize));
        return false;
    }

    using NativeFunction = void (*)(NativeResult*);
    auto function = reinterpret_cast<NativeFunction>(page);
    function(&result);
    munmap(page, static_cast<size_t>(pageSize));
    return true;
}

void appendImmediate(std::vector<uint8_t>& code, uint8_t regOpcode,
                     uint64_t value) {
    code.push_back(0x48);
    code.push_back(regOpcode);
    for (unsigned byte = 0; byte < 8; ++byte)
        code.push_back(static_cast<uint8_t>(value >> (byte * 8U)));
}

bool compareCase(const Operation& operation, uint64_t left, uint64_t right) {
    std::vector<uint8_t> instruction;
    appendImmediate(instruction, 0xb8, left);  // MOV RAX, imm64
    appendImmediate(instruction, 0xb9, right); // MOV RCX, imm64
    instruction.push_back(0x48);
    instruction.push_back(operation.opcode);
    instruction.push_back(0xc8); // RAX, RCX

    std::vector<uint8_t> nativeCode = instruction;
    nativeCode.push_back(0x9c); // PUSHFQ
    nativeCode.push_back(0x5a); // POP RDX
    nativeCode.insert(nativeCode.end(), {0x48, 0x89, 0x07}); // [RDI] = RAX
    nativeCode.insert(nativeCode.end(), {0x48, 0x89, 0x57, 0x08});
    nativeCode.push_back(0xc3);

    NativeResult native{};
    if (!runNative(nativeCode, native)) {
        std::fprintf(stderr, "FAIL: native code setup for %s\n", operation.name);
        return false;
    }

    std::array<uint8_t, 4096> guestBytes{};
    std::memcpy(guestBytes.data(), instruction.data(), instruction.size());
    guestBytes[instruction.size()] = 0xf4; // HLT
    rottr::X64GuestMemory guest{0x1000, guestBytes.data(), guestBytes.size(),
                                true, true};
    rottr_cpu cpu{};
    rottr_cpu_reset(&cpu, guest.base, guest.base + guest.size);
    const rottr::X64RunResult interpreted =
            rottr::runX64Interpreter(cpu, guest, 4);

    const uint64_t interpreterFlags = cpu.rflags & operation.flagsMask;
    const uint64_t nativeFlags = native.flags & operation.flagsMask;
    if (interpreted.reason != rottr::X64StopReason::Halted ||
        cpu.gpr[0] != native.value || interpreterFlags != nativeFlags) {
        std::fprintf(stderr,
                     "FAIL: %s (%016llx, %016llx): "
                     "interpreter=(%016llx, %03llx) native=(%016llx, %03llx)\n",
                     operation.name, static_cast<unsigned long long>(left),
                     static_cast<unsigned long long>(right),
                     static_cast<unsigned long long>(cpu.gpr[0]),
                     static_cast<unsigned long long>(interpreterFlags),
                     static_cast<unsigned long long>(native.value),
                     static_cast<unsigned long long>(nativeFlags));
        return false;
    }
    return true;
}

} // namespace

int main() {
    constexpr uint64_t arithmeticFlags =
            (1ULL << 0) | (1ULL << 2) | (1ULL << 4) | (1ULL << 6) |
            (1ULL << 7) | (1ULL << 11);
    constexpr uint64_t logicalFlags =
            (1ULL << 0) | (1ULL << 2) | (1ULL << 6) |
            (1ULL << 7) | (1ULL << 11);
    constexpr std::array<Operation, 5> operations{{
            {0x01, "ADD", arithmeticFlags},
            {0x31, "XOR", logicalFlags},
            {0x39, "CMP", arithmeticFlags},
            {0x09, "OR", logicalFlags},
            {0x21, "AND", logicalFlags},
    }};
    constexpr std::array<std::array<uint64_t, 2>, 8> operands{{
            {{0, 0}},
            {{0, 1}},
            {{1, 0}},
            {{1, 1}},
            {{UINT64_MAX, 1}},
            {{0x7fffffffffffffffULL, 1}},
            {{0x8000000000000000ULL, UINT64_MAX}},
            {{0x0123456789abcdefULL, 0xfedcba9876543210ULL}},
    }};

    for (const Operation& operation : operations) {
        for (const auto& pair : operands) {
            if (!compareCase(operation, pair[0], pair[1])) return 1;
        }
    }
    std::puts("OK (40 native/interpreter arithmetic comparisons)");
    return 0;
}
