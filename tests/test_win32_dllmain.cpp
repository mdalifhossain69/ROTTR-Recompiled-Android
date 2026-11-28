// Roadmap step 17: Call DllMain and entry points in dependency order.
// Pass: DllMain is called for each loaded DLL in dependency order
// (dependencies first), and each returns TRUE.
#include "rottr/pe_loader.h"
#include "rottr/x64_bridge.h"

#include "pe_fixture.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}
}  // namespace

using namespace pe_fixture;

// Build "b.dll" exporting "FuncB" at RVA 0x2000.
// DllMain at RVA 0x1000: mov eax, 1 ; ret   (returns TRUE)
// FuncB at RVA 0x2000: ret
static std::vector<uint8_t> buildB() {
    Builder builder(0x140010000ULL, 0x5000, 0x4000);
    builder.dll(true);
    builder.entry(0x1000);
    builder.section(".text", 0x1000, 0x400, 0x200, 0x400, 0x60000020U);
    builder.section(".edata", 0x2000, 0x800, 0x600, 0x400, 0x40000040U);
    // DllMain at RVA 0x1000 (file off 0x200): mov eax, 1 ; ret
    builder.raw(0x200, {0xb8, 0x01, 0x00, 0x00, 0x00, 0xc3});
    // FuncB at RVA 0x2000 (file off 0x300): ret
    builder.raw(0x300, {0xc3});
    // Export directory at RVA 0x2000 (file off 0x600)
    put<uint32_t>(builder.file, 0x600 + 16, 1U);   // ordinal base
    put<uint32_t>(builder.file, 0x600 + 20, 1U);   // function count
    put<uint32_t>(builder.file, 0x600 + 24, 1U);   // name count
    put<uint32_t>(builder.file, 0x600 + 28, 0x3100U); // address table RVA
    put<uint32_t>(builder.file, 0x600 + 32, 0x3120U); // name ptr table RVA
    put<uint32_t>(builder.file, 0x600 + 36, 0x3140U); // ordinal table RVA
    put<uint64_t>(builder.file, 0x700, 0x2000ULL);  // FuncB RVA (address table)
    put<uint16_t>(builder.file, 0x720, 0U);         // ordinal index 0
    text(builder.file, 0x740, "FuncB");             // name at RVA 0x3140
    builder.directory(0, 0x2000, 0x400);
    return builder.build();
}

// Build "a.dll" importing "FuncB" from b.dll.
// DllMain at RVA 0x1000: mov eax, 2 ; ret   (returns TRUE)
static std::vector<uint8_t> buildA() {
    Builder builder(0x140020000ULL, 0x5000, 0x4000);
    builder.dll(true);
    builder.entry(0x1000);
    builder.section(".text", 0x1000, 0x400, 0x200, 0x400, 0x60000020U);
    builder.section(".idata", 0x2000, 0x800, 0x400, 0x800, 0xc0000040U);
    // DllMain at RVA 0x1000 (file off 0x200): mov eax, 2 ; ret
    builder.raw(0x200, {0xb8, 0x02, 0x00, 0x00, 0x00, 0xc3});
    // Import descriptor at RVA 0x2000 (file off 0x400)
    put<uint32_t>(builder.file, 0x400, 0x2100U);   // lookup table RVA
    put<uint32_t>(builder.file, 0x40c, 0x2200U);   // DLL name RVA
    put<uint32_t>(builder.file, 0x410, 0x2300U);   // IAT RVA
    text(builder.file, 0x600, "b.dll");             // DLL name at RVA 0x2200
    put<uint64_t>(builder.file, 0x500, 0x2400U);   // by-name thunk at RVA 0x2100
    put<uint64_t>(builder.file, 0x508, 0ULL);      // end of lookup table
    put<uint16_t>(builder.file, 0x800, 0U);        // hint at RVA 0x2400
    text(builder.file, 0x802, "FuncB");            // name at RVA 0x2402
    put<uint64_t>(builder.file, 0x700, 0ULL);      // IAT slot at RVA 0x2300
    builder.directory(1, 0x2000, 20);
    return builder.build();
}

int main() {
    rottr_guest_mem* memory = rottr_guest_reserve(64ULL << 20U);
    if (!expect(memory != nullptr, "guest reserve")) return 1;
    uintptr_t base = (uintptr_t)rottr_guest_base(memory);

    std::vector<uint8_t> bDll = buildB();
    std::vector<uint8_t> aDll = buildA();
    rottr::PeModuleRegistry registry(memory);
    registry.setFileOpener([&](const std::string& name,
                               std::vector<uint8_t>& bytes) -> bool {
        if (name == "b.dll") { bytes = bDll; return true; }
        if (name == "a.dll") { bytes = aDll; return true; }
        return false;
    });

    // ---- Step 17a: Load a.dll (pulls in b.dll as dependency) ----
    std::string error;
    rottr::PeLoadOptions loadOpts;
    loadOpts.loadBase = base + 0x10000;
    rottr::PeImage* a = registry.loadModule("a.dll", &error, loadOpts);
    if (!expect(a != nullptr, "a.dll must load")) {
        std::fprintf(stderr, "  %s\n", error.c_str());
        return 1;
    }
    rottr::PeImage* b = registry.findModule("b.dll");
    if (!expect(b != nullptr, "b.dll must be loaded as dependency of a.dll"))
        return 1;

    // ---- Step 17b: Verify dependency order ----
    std::vector<rottr::PeImage*> order = registry.initializationOrder();
    if (!expect(order.size() == 2,
                "initialization order must have exactly 2 modules"))
        return 1;
    if (!expect(order[0] == b, "b.dll must initialize first (dependency)"))
        return 1;
    if (!expect(order[1] == a, "a.dll must initialize second (dependent)"))
        return 1;

    // ---- Step 17c: Call DllMain for each in dependency order ----
    const uint32_t kProcessAttach = 1u;
    uint64_t result = 0;
    rottr_cpu cpu{};
    rottr_cpu_reset(&cpu, 0, base + 0x100000);
    // Commit stack page so callGuestProc can push the return sentinel.
    rottr_guest_commit_pages(memory, base + 0x100000 - 4096, 4096);
    rottr::X64GuestMemory guest{
        base, reinterpret_cast<uint8_t*>(rottr_guest_base(memory)),
        64ULL << 20U, true, true};

    if (!expect(b->callDllMain(cpu, guest, kProcessAttach, result),
                "b.dll DllMain must be callable"))
        return 1;
    if (!expect(result == 1, "b.dll DllMain must return TRUE (1)")) return 1;

    if (!expect(a->callDllMain(cpu, guest, kProcessAttach, result),
                "a.dll DllMain must be callable"))
        return 1;
    if (!expect(result == 2, "a.dll DllMain must return TRUE (2)")) return 1;

    std::printf("OK (order=b->a, b_result=%llu, a_result=%llu)\n",
                (unsigned long long)1, (unsigned long long)2);
    rottr_guest_release(memory);
    return 0;
}
