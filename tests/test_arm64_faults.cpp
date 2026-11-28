#include "rottr/arm64_block_cache.h"
#include "rottr/x64_runtime.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using Bytes = std::vector<uint8_t>;

struct Snapshot {
    std::array<uint64_t, 16> gpr{};
    uint64_t rip = 0;
    uint64_t rflags = 0;
};

Snapshot snapshot(const rottr_cpu& cpu) {
    Snapshot s;
    std::memcpy(s.gpr.data(), cpu.gpr, sizeof(cpu.gpr));
    s.rip = cpu.rip;
    s.rflags = cpu.rflags;
    return s;
}

bool same(const Snapshot& a, const Snapshot& b) {
    return a.gpr == b.gpr && a.rip == b.rip && a.rflags == b.rflags;
}

void movRax(Bytes& out, uint64_t value) {
    out.push_back(0x48);
    out.push_back(0xb8);
    for (unsigned i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

bool runFaultCase(const char* name, const Bytes& program, bool writable,
                  rottr::X64StopReason expectedReason, uint64_t expectedFault) {
    const uint64_t base = 0x1000;
    Bytes initial(0x200, 0);
    std::copy(program.begin(), program.end(), initial.begin());

    rottr_cpu interpreterCpu{};
    interpreterCpu.rip = base;
    Bytes interpreterBytes = initial;
    rottr::X64GuestMemory interpreterMemory{base, interpreterBytes.data(),
                                            interpreterBytes.size(), writable, true};
    rottr::X64RunResult interpreterResult =
        rottr::runX64Interpreter(interpreterCpu, interpreterMemory, 16);

    rottr_cpu translatedCpu{};
    translatedCpu.rip = base;
    Bytes translatedBytes = initial;
    rottr::X64GuestMemory translatedMemory{base, translatedBytes.data(),
                                           translatedBytes.size(), writable, true};
    rottr::arm64::BlockCache cache;
    rottr::X64RunResult translatedResult =
        rottr::arm64::runX64Translated(translatedCpu, translatedMemory, 16, cache);

    if (interpreterResult.reason != expectedReason ||
        translatedResult.reason != interpreterResult.reason ||
        translatedResult.faultAddress != expectedFault ||
        translatedResult.faultAddress != interpreterResult.faultAddress ||
        translatedResult.instructionsExecuted != interpreterResult.instructionsExecuted ||
        !same(snapshot(interpreterCpu), snapshot(translatedCpu))) {
        std::printf("FAIL: %s\n", name);
        std::printf("  interpreter reason=%d fault=0x%llx rip=0x%llx\n",
                    static_cast<int>(interpreterResult.reason),
                    static_cast<unsigned long long>(interpreterResult.faultAddress),
                    static_cast<unsigned long long>(interpreterCpu.rip));
        std::printf("  translated  reason=%d fault=0x%llx rip=0x%llx\n",
                    static_cast<int>(translatedResult.reason),
                    static_cast<unsigned long long>(translatedResult.faultAddress),
                    static_cast<unsigned long long>(translatedCpu.rip));
        return false;
    }
    return true;
}

}  // namespace

int main() {
    Bytes invalidRead;
    movRax(invalidRead, 0x2a);
    // mov rbx, [0x2000] -- outside the guest memory window.
    invalidRead.insert(invalidRead.end(), {0x48, 0x8b, 0x1d, 0xef, 0x0f, 0, 0});

    Bytes readOnlyWrite;
    readOnlyWrite.insert(readOnlyWrite.end(),
                         {0x48, 0xbb, 0x00, 0x11, 0, 0, 0, 0, 0, 0});
    movRax(readOnlyWrite, 0x2a);
    // mov [rbx], rax -- address 0x1100 in a read-only memory window.
    readOnlyWrite.insert(readOnlyWrite.end(), {0x48, 0x89, 0x03});

    bool ok = true;
    ok &= runFaultCase("invalid translated load reports guest address", invalidRead, true,
                       rottr::X64StopReason::InvalidMemory, 0x100a);
    ok &= runFaultCase("translated store to read-only memory reports guest address",
                       readOnlyWrite, false, rottr::X64StopReason::InvalidMemory, 0x1014);

    if (!rottr::arm64::hasArm64TranslationBackend()) {
        std::puts("SKIP: host is not AArch64; native translation was not executed");
        return 77;
    }
    if (!ok) return 1;
    std::puts("OK (translated faults report exact guest address and registers)");
    return 0;
}