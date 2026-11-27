// Roadmap step 15: Create PEB, TEB, stack and the GS base.
// Pass: gs:[0x30] returns TEB, gs:[0x60] returns PEB.
#include <cstdio>
#include <cstdint>
#include <cstring>

#include "rottr/guest.h"
#include "rottr/x64_runtime.h"
#include "rottr/guest_mem.h"

namespace {
[[maybe_unused]] bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}
}  // namespace

int main() {
    rottr_guest_mem* memory = rottr_guest_reserve(8ULL << 20U);
    if (!expect(memory != nullptr, "guest reserve")) return 1;
    uintptr_t base = (uintptr_t)rottr_guest_base(memory);
    rottr_guest_commit_pages(memory, base, 8ULL << 20U);

    // TEB at 0x10000, PEB at 0x20000 (guest addresses)
    const uint64_t tebAddr = base + 0x10000ULL;
    const uint64_t pebAddr = base + 0x20000ULL;

    // TEB+0x30 = Self (TEB address itself), TEB+0x60 = ProcessEnvironmentBlock (PEB address)
    uint64_t self = tebAddr;
    rottr_guest_write(memory, static_cast<uintptr_t>(tebAddr + 0x30ULL), &self, sizeof(self));
    uint64_t peb = pebAddr;
    rottr_guest_write(memory, static_cast<uintptr_t>(tebAddr + 0x60ULL), &peb, sizeof(peb));

    rottr::X64GuestMemory guest{
        base, reinterpret_cast<uint8_t*>(rottr_guest_base(memory)), 8ULL << 20U,
        true, true};
    rottr_mem_result r;
    rottr_guest_translate(memory, base, 8ULL << 20U, &r);
    guest.data = static_cast<uint8_t*>(r.host);

    // ---- Test 1: gs:[0x30] returns TEB address ----
    {
        // mov rax, gs:[0x30] ; hlt
        const uint8_t prog[] = {0x65, 0x48, 0x8B, 0x04, 0x25, 0x30, 0x00, 0x00, 0x00, 0xF4};
        rottr_cpu cpu{};
        rottr_cpu_reset(&cpu, base, base + 0x100000);
        cpu.gsBase = tebAddr;
        for (size_t i = 0; i < sizeof(prog); ++i)
            rottr_guest_write(memory, static_cast<uintptr_t>(base + i), &prog[i], 1);

        auto result = rottr::runX64Interpreter(cpu, guest, 16);
        if (!expect(result.reason == rottr::X64StopReason::Halted,
                    "gs:[0x30] test must reach HLT") ||
            !expect(cpu.gpr[0] == tebAddr,
                    "gs:[0x30] must return TEB address")) {
            std::fprintf(stderr, "  rax=0x%llx expected 0x%llx\n",
                    (unsigned long long)cpu.gpr[0], (unsigned long long)tebAddr);
            return 1;
        }
    }

    // ---- Test 2: gs:[0x60] returns PEB address ----
    {
        // mov rax, gs:[0x60] ; hlt
        const uint8_t prog[] = {0x65, 0x48, 0x8B, 0x04, 0x25, 0x60, 0x00, 0x00, 0x00, 0xF4};
        rottr_cpu cpu{};
        rottr_cpu_reset(&cpu, base, base + 0x100000);
        cpu.gsBase = tebAddr;
        for (size_t i = 0; i < sizeof(prog); ++i)
            rottr_guest_write(memory, static_cast<uintptr_t>(base + i), &prog[i], 1);

        auto result = rottr::runX64Interpreter(cpu, guest, 16);
        if (!expect(result.reason == rottr::X64StopReason::Halted,
                    "gs:[0x60] test must reach HLT") ||
            !expect(cpu.gpr[0] == pebAddr,
                    "gs:[0x60] must return PEB address")) {
            std::fprintf(stderr, "  rax=0x%llx expected 0x%llx\n",
                    (unsigned long long)cpu.gpr[0], (unsigned long long)pebAddr);
            return 1;
        }
    }

    // ---- Test 3: gs:[0x30] with gsBase=base reads from base+0x30 ----
    {
        uint8_t zero = 0;
        rottr_guest_write(memory, static_cast<uintptr_t>(base + 0x30), &zero, 1);
        const uint8_t prog[] = {0x65, 0x48, 0x8B, 0x04, 0x25, 0x30, 0x00, 0x00, 0x00, 0xF4};
        rottr_cpu cpu{};
        rottr_cpu_reset(&cpu, base, base + 0x100000);
        cpu.gsBase = base; // GS base = guest base
        for (size_t i = 0; i < sizeof(prog); ++i)
            rottr_guest_write(memory, static_cast<uintptr_t>(base + i), &prog[i], 1);

        auto result = rottr::runX64Interpreter(cpu, guest, 16);
        if (!expect(result.reason == rottr::X64StopReason::Halted,
                    "gs:[0x30] with gsBase=base must reach HLT") ||
            !expect(cpu.gpr[0] == 0,
                    "gs:[0x30] with gsBase=base must return 0")) {
            std::fprintf(stderr, "  rax=0x%llx expected 0\n",
                    (unsigned long long)cpu.gpr[0]);
            return 1;
        }
    }

    std::puts("OK");
    rottr_guest_release(memory);
    return 0;
}
