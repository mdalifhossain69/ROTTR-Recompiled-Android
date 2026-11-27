// Roadmap step 18: Load an import-free EXE whose entry returns 42.
// Pass: interpreter ends with RAX equal to 42.
#include "rottr/pe_loader.h"
#include "rottr/x64_runtime.h"
#include "rottr/guest_mem.h"

#include "pe_fixture.h"

#include <cstdio>
#include <vector>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}
}  // namespace

int main() {
    rottr_guest_mem* memory = rottr_guest_reserve(8ULL << 20U);
    if (!expect(memory != nullptr, "guest reserve")) return 1;
    uintptr_t base = (uintptr_t)rottr_guest_base(memory);

    // Build an import-free PE32+ EXE:
    //   .text at RVA 0x1000 contains: mov eax, 42 ; ret
    // Entry point = 0x1000
    pe_fixture::Builder builder(0x140000000ULL, 0x5000, 0x4000);
    builder.dll(false);  // EXE, not DLL
    builder.entry(0x1000);
    builder.section(".text", 0x1000, 0x200, 0x400, 0x200, 0x60000020U);
    // mov eax, 42 (b8 2a 00 00 00) ; hlt (f4)
    builder.raw(0x400, {0xb8, 0x2a, 0x00, 0x00, 0x00, 0xf4});
    std::vector<uint8_t> file = builder.build();

    rottr::PeImage image;
    rottr::PeLoadOptions options;
    options.loadBase = base + 0x10000;
    const rottr::PeLoadError status = image.load(memory, file.data(), file.size(), options);
    if (!expect(status == rottr::PeLoadError::None,
                "PE load must succeed")) {
        std::fprintf(stderr, "load error: %s\n",
                     rottr::describePeLoadError(status));
        return 1;
    }
    if (!expect(image.loaded() && !image.isDll(), "image must be loaded EXE"))
        return 1;

    // Set up guest CPU state at the entry point.
    rottr_cpu cpu{};
    rottr_cpu_reset(&cpu, image.entryPoint(), base + 0x100000);

    rottr::X64GuestMemory guest{
        base, reinterpret_cast<uint8_t*>(rottr_guest_base(memory)), 8ULL << 20U,
        true, true};
    rottr_mem_result r;
    rottr_guest_translate(memory, base, 8ULL << 20U, &r);
    guest.data = static_cast<uint8_t*>(r.host);

    auto result = rottr::runX64Interpreter(cpu, guest, 16);
    std::fprintf(stderr, "DEBUG: reason=%d rax=0x%llx rip=0x%llx executed=%llu\n",
            (int)result.reason, (unsigned long long)cpu.gpr[0],
            (unsigned long long)cpu.rip,
            (unsigned long long)result.instructionsExecuted);

    if (!expect(result.reason == rottr::X64StopReason::Halted,
                "interpreter must reach RET then HLT") ||
        !expect(cpu.gpr[0] == 42, "entry point must return 42 in RAX")) {
        return 1;
    }

    rottr_guest_release(memory);
    std::puts("OK");
    return 0;
}
