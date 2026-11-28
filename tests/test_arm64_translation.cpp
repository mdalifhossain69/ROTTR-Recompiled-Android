#include "rottr/arm64_translator_runtime.h"
#include "rottr/x64_runtime.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<uint8_t>;

struct Snapshot {
    std::array<uint64_t, 16> gpr{};
    uint64_t rip = 0;
    uint64_t rflags = 0;
    Bytes data;
};

Snapshot snapshot(const rottr_cpu& cpu, const Bytes& bytes) {
    Snapshot s;
    std::memcpy(s.gpr.data(), cpu.gpr, sizeof(cpu.gpr));
    s.rip = cpu.rip;
    s.rflags = cpu.rflags;
    s.data = bytes;
    return s;
}

bool same(const Snapshot& a, const Snapshot& b) {
    return a.gpr == b.gpr && a.rip == b.rip && a.rflags == b.rflags && a.data == b.data;
}

rottr_cpu freshCpu(uint64_t rip) {
    rottr_cpu cpu{};
    cpu.rip = rip;
    return cpu;
}

bool runCase(const char* name, const Bytes& program, uint64_t budget,
             size_t memorySize = 0x200) {
    Bytes initial(memorySize, 0);
    std::copy(program.begin(), program.end(), initial.begin());
    Bytes bytes = initial;

    const uint64_t base = 0x1000;
    rottr_cpu interpreterCpu = freshCpu(base);
    rottr::X64GuestMemory memory{base, bytes.data(), bytes.size(), true, true};
    rottr::X64RunResult interpreterResult =
        rottr::runX64Interpreter(interpreterCpu, memory, budget);
    Snapshot expected = snapshot(interpreterCpu, bytes);

    rottr_cpu translatedCpu = freshCpu(base);
    Bytes translatedBytes = initial;
    rottr::X64GuestMemory translatedMemory{base, translatedBytes.data(),
                                           translatedBytes.size(), true, true};
    rottr::X64RunResult translatedResult =
        rottr::arm64::runX64Translated(translatedCpu, translatedMemory, budget);
    Snapshot actual = snapshot(translatedCpu, translatedBytes);

    if (!same(expected, actual) ||
        interpreterResult.instructionsExecuted != translatedResult.instructionsExecuted) {
        std::printf("FAIL: %s\n", name);
        std::printf("  interpreter rip=0x%llx flags=0x%llx executed=%llu\n",
                    static_cast<unsigned long long>(expected.rip),
                    static_cast<unsigned long long>(expected.rflags),
                    static_cast<unsigned long long>(interpreterResult.instructionsExecuted));
        std::printf("  translated  rip=0x%llx flags=0x%llx executed=%llu\n",
                    static_cast<unsigned long long>(actual.rip),
                    static_cast<unsigned long long>(actual.rflags),
                    static_cast<unsigned long long>(translatedResult.instructionsExecuted));
        for (size_t i = 0; i < expected.gpr.size(); ++i) {
            if (expected.gpr[i] != actual.gpr[i]) {
                std::printf("  r%zu: interpreter=0x%llx translated=0x%llx\n", i,
                            static_cast<unsigned long long>(expected.gpr[i]),
                            static_cast<unsigned long long>(actual.gpr[i]));
            }
        }
        for (size_t i = 0; i < expected.data.size(); ++i) {
            if (expected.data[i] != actual.data[i]) {
                std::printf("  mem[0x%zx]: interpreter=0x%02x translated=0x%02x\n", i,
                            expected.data[i], actual.data[i]);
            }
        }
        return false;
    }
    return true;
}

void movImm(Bytes& out, uint8_t reg, uint64_t value) {
    out.push_back(0x48);
    out.push_back(static_cast<uint8_t>(0xb8 + reg));
    for (unsigned i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

}  // namespace

int main() {
    if (!rottr::hasX64InstructionInterpreter()) {
        std::puts("FAIL: x64 interpreter unavailable");
        return 1;
    }

    Bytes branch;
    movImm(branch, 0, 5);
    movImm(branch, 3, 7);
    branch.insert(branch.end(), {0x48, 0x39, 0xd8});
    branch.insert(branch.end(), {0x72, 0x0c});
    movImm(branch, 1, 1);
    branch.insert(branch.end(), {0xeb, 0x0a});
    movImm(branch, 1, 2);

    Bytes logical;
    movImm(logical, 0, 0xf0);
    movImm(logical, 3, 0x0f);
    logical.insert(logical.end(), {0x48, 0x21, 0xd8});

    Bytes increment;
    movImm(increment, 0, ~0ULL);
    increment.insert(increment.end(), {0x48, 0x83, 0xc0, 0x01, 0x48, 0xff, 0xc0});

    Bytes atomic;
    movImm(atomic, 3, 0x1100);
    atomic.insert(atomic.end(), {0xf0, 0x48, 0x83, 0x03, 0x01, 0x48, 0x8b, 0x03});

    bool ok = true;
    ok &= runCase("branch and arithmetic flags", branch, 5);
    ok &= runCase("logical flags", logical, 3);
    ok &= runCase("inc preserves carry", increment, 3);
    ok &= runCase("locked RMW falls back to ordered interpreter", atomic, 3);

    if (!rottr::arm64::hasArm64TranslationBackend()) {
        std::puts("SKIP: host is not AArch64; native translation was not executed");
        return 77;
    }
    if (!ok) return 1;
    std::puts("OK (translated flags, branches, and ordered fallbacks match interpreter)");
    return 0;
}