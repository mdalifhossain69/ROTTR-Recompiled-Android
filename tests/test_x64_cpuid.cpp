// Roadmap step 27: Add CPUID, RDTSC, XGETBV.
// Pass: only implemented features are advertised.
#include "rottr/x64_runtime.h"
#include "x64_test_runner.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

constexpr uint64_t kBase = 0x1000;
constexpr size_t kMemSize = 0x1000;

struct Run {
    rottr_cpu cpu{};
    rottr::X64RunResult result{};
};

// Run `code` to completion with the given initial EAX/ECX.
Run runWith(uint64_t eax, uint64_t ecx, const std::vector<uint8_t>& code) {
    std::vector<uint8_t> memory(kMemSize, 0);
    std::memcpy(memory.data(), code.data(), code.size());
    rottr_cpu cpu{};
    rottr_cpu_reset(&cpu, kBase, kBase + 0x800);
    cpu.gpr[0] = eax;  // rax
    cpu.gpr[1] = ecx;  // rcx
    const rottr::X64GuestMemory guest{kBase, memory.data(), memory.size(), true, true};
    auto result = runX64ForTest(cpu, guest, 16);
    return Run{cpu, result};
}

std::string vendorString(const rottr_cpu& cpu) {
    char vendor[13] = {};
    uint32_t ebx = static_cast<uint32_t>(cpu.gpr[3]);
    uint32_t edx = static_cast<uint32_t>(cpu.gpr[2]);
    uint32_t ecx = static_cast<uint32_t>(cpu.gpr[1]);
    std::memcpy(vendor + 0, &ebx, 4);
    std::memcpy(vendor + 4, &edx, 4);
    std::memcpy(vendor + 8, &ecx, 4);
    return std::string(vendor, 12);
}

bool cpuidLeaf0() {
    const std::vector<uint8_t> code = {0x0f, 0xa2, 0xf4};  // cpuid; hlt
    Run run = runWith(0, 0, code);
    if (!expect(run.result.reason == rottr::X64StopReason::Halted,
                "CPUID leaf 0 must execute"))
        return false;
    uint32_t maxLeaf = static_cast<uint32_t>(run.cpu.gpr[0]);
    if (!expect(maxLeaf >= 1, "CPUID leaf 0 must report a usable max leaf"))
        return false;
    if (!expect(vendorString(run.cpu) == "ROTTR-RECOMP",
                "CPUID must report the runtime's own vendor string"))
        return false;
    return true;
}

bool cpuidLeaf1AdvertisesNothingUnimplemented() {
    const std::vector<uint8_t> code = {0x0f, 0xa2, 0xf4};
    Run run = runWith(1, 0, code);
    if (!expect(run.result.reason == rottr::X64StopReason::Halted,
                "CPUID leaf 1 must execute"))
        return false;
    uint32_t ecx = static_cast<uint32_t>(run.cpu.gpr[1]);
    uint32_t edx = static_cast<uint32_t>(run.cpu.gpr[2]);
    // No SSE/SSE2/SSE3/SSSE3/SSE4.x/AVX/FMA/XSAVE/POPCNT are implemented yet.
    if (!expect(ecx == 0, "CPUID leaf 1 ECX must not advertise unimplemented features"))
        return false;
    if (!expect(edx == 0, "CPUID leaf 1 EDX must not advertise unimplemented features"))
        return false;
    uint32_t ebx = static_cast<uint32_t>(run.cpu.gpr[3]);
    if (!expect(((ebx >> 16) & 0xffU) == 1U,
                "CPUID leaf 1 must report one logical processor"))
        return false;
    return true;
}

bool cpuidExtended() {
    const std::vector<uint8_t> code = {0x0f, 0xa2, 0xf4};
    Run run = runWith(0x80000000ULL, 0, code);
    if (!expect(run.result.reason == rottr::X64StopReason::Halted,
                "CPUID extended leaf must execute"))
        return false;
    if (!expect(static_cast<uint32_t>(run.cpu.gpr[0]) == 0x80000000U,
                "CPUID 0x80000000 must report no extended leaves"))
        return false;
    return true;
}

bool cpuidUnknownLeafIsZero() {
    const std::vector<uint8_t> code = {0x0f, 0xa2, 0xf4};
    Run run = runWith(0x12345678ULL, 0, code);
    if (!expect(run.result.reason == rottr::X64StopReason::Halted,
                "unknown CPUID leaf must execute"))
        return false;
    if (!expect(run.cpu.gpr[0] == 0 && run.cpu.gpr[1] == 0 &&
                    run.cpu.gpr[2] == 0 && run.cpu.gpr[3] == 0,
                "unknown CPUID leaf must return zeros"))
        return false;
    return true;
}

bool rdtscMonotonic() {
    // rdtsc; mov rbx,rax; mov rsi,rdx; rdtsc; hlt
    const std::vector<uint8_t> code = {
        0x0f, 0x31,
        0x48, 0x89, 0xc3,
        0x48, 0x89, 0xd6,
        0x0f, 0x31,
        0xf4,
    };
    Run run = runWith(0, 0, code);
    if (!expect(run.result.reason == rottr::X64StopReason::Halted,
                "RDTSC program must halt"))
        return false;
    uint64_t t1 = (run.cpu.gpr[6] << 32) | (run.cpu.gpr[3] & 0xffffffffULL);
    uint64_t t2 = (run.cpu.gpr[2] << 32) | (run.cpu.gpr[0] & 0xffffffffULL);
    if (!expect(t2 >= t1, "RDTSC must be monotonic"))
        return false;
    if (!expect(t1 != 0, "RDTSC must return a nonzero timestamp"))
        return false;
    // EDX:EAX must be zero-extended writes, not leave stale high bits.
    if (!expect((run.cpu.gpr[0] >> 32) == 0 && (run.cpu.gpr[2] >> 32) == 0,
                "RDTSC must write only EDX:EAX"))
        return false;
    return true;
}

bool xgetbvSupported() {
    // mov ecx,0; xgetbv; hlt
    const std::vector<uint8_t> code = {
        0xb9, 0x00, 0x00, 0x00, 0x00,
        0x0f, 0x01, 0xd0,
        0xf4,
    };
    Run run = runWith(0, 0, code);
    if (!expect(run.result.reason == rottr::X64StopReason::Halted,
                "XGETBV must execute"))
        return false;
    uint64_t xcr0 = (run.cpu.gpr[2] << 32) | (run.cpu.gpr[0] & 0xffffffffULL);
    if (!expect(xcr0 == 0,
                "XGETBV must not enable x87/SSE/AVX state that is unimplemented"))
        return false;
    return true;
}
}  // namespace

int main() {
    if (!cpuidLeaf0()) return 1;
    if (!cpuidLeaf1AdvertisesNothingUnimplemented()) return 1;
    if (!cpuidExtended()) return 1;
    if (!cpuidUnknownLeafIsZero()) return 1;
    if (!rdtscMonotonic()) return 1;
    if (!xgetbvSupported()) return 1;
    std::puts("OK (cpuid leaves, rdtsc monotonic, xgetbv)");
    return 0;
}
