#include "rottr/arm64_block_cache.h"
#include "rottr/x64_runtime.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using Bytes = std::vector<uint8_t>;

void movRax(Bytes& out, uint64_t value) {
    out.push_back(0x48);
    out.push_back(0xb8);
    for (unsigned i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

}  // namespace

int main() {
    Bytes bytes;
    movRax(bytes, 1);
    movRax(bytes, 2);
    bytes.resize(0x200, 0);
    // mov dword [rip-18], 3 -> patches the low immediate byte of the second MOV.
    const uint8_t patch[] = {0xc7, 0x05, 0xee, 0xff, 0xff, 0xff, 0x03, 0, 0, 0};
    std::memcpy(bytes.data() + 0x14, patch, sizeof(patch));

    const uint64_t base = 0x1000;
    rottr_cpu cpu{};
    rottr::X64GuestMemory memory{base, bytes.data(), bytes.size(), true, true};
    rottr::arm64::BlockCache cache;

    cpu.rip = base;
    rottr::arm64::runX64Translated(cpu, memory, 1, cache);
    if (cpu.gpr[0] != 1) {
        std::printf("FAIL: first cached block set RAX=%llu rip=0x%llx\n",
                    static_cast<unsigned long long>(cpu.gpr[0]),
                    static_cast<unsigned long long>(cpu.rip));
        return 1;
    }

    cpu.rip = base;
    rottr::arm64::runX64Translated(cpu, memory, 1, cache);
    if (cpu.gpr[0] != 1 || cache.stats().hits == 0) {
        std::printf("FAIL: cached block did not replay RAX=1\n");
        return 1;
    }

    cpu.rip = base;
    rottr::arm64::runX64Translated(cpu, memory, 2, cache);
    if (cpu.gpr[0] != 2) {
        std::printf("FAIL: second block set RAX=%llu\n",
                    static_cast<unsigned long long>(cpu.gpr[0]));
        return 1;
    }

    cpu.rip = base + 0x14;
    rottr::arm64::runX64Translated(cpu, memory, 1, cache);
    if (bytes[0x0c] != 3) {
        std::printf("FAIL: guest store did not patch code byte (0x%02x)\n", bytes[0x0c]);
        return 1;
    }

    cpu.rip = base + 0x0a;
    rottr::arm64::runX64Translated(cpu, memory, 1, cache);
    if (cpu.gpr[0] != 3) {
        std::printf("FAIL: stale cached block ran after self-modification (RAX=%llu hits=%zu misses=%zu invalidations=%zu)\n",
                    static_cast<unsigned long long>(cpu.gpr[0]), cache.stats().hits,
                    cache.stats().misses, cache.stats().invalidations);
        return 1;
    }

    if (!rottr::arm64::hasArm64TranslationBackend()) {
        std::puts("SKIP: host is not AArch64; native translation was not executed");
        return 77;
    }
    if (cache.stats().invalidations == 0) {
        std::puts("FAIL: cache did not detect modified code");
        return 1;
    }
    std::puts("OK (cached blocks detect self-modification and rerun translated code)");
    return 0;
}