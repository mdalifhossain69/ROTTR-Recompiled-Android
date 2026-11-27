// Roadmap step 14: Load guest DLLs with dependencies and reference counts.
// Pass: your EXE calls your DLL; a dependency cycle does not hang.
#include "rottr/pe_loader.h"

#include "pe_fixture.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {
[[maybe_unused]] bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}
}  // namespace

using namespace pe_fixture;

// .text at RVA 0x1000/rawOffset 0x200, .idata at RVA 0x2000/rawOffset 0x400
// File offset for .idata RVA r: 0x400 + (r - 0x2000)

// Build "base.dll" exporting "BaseFunc" at RVA 0x3000.
static std::vector<uint8_t> buildBaseDll() {
    Builder builder(0x140000000ULL, 0x5000, 0x4000);
    builder.dll(true);
    builder.entry(0x1000);
    builder.section(".text", 0x1000, 0x400, 0x200, 0x400, 0x60000020U);
    builder.section(".edata", 0x2000, 0x800, 0x600, 0x400, 0x40000040U);
    // Export directory at RVA 0x2000 (file offset 0x600)
    put<uint32_t>(builder.file, 0x600 + 16U, 1U);   // ordinal base
    put<uint32_t>(builder.file, 0x600 + 20U, 1U);   // function count
    put<uint32_t>(builder.file, 0x600 + 24U, 1U);   // name count
    put<uint32_t>(builder.file, 0x600 + 28U, 0x2100U); // address table RVA
    put<uint32_t>(builder.file, 0x600 + 32U, 0x2120U); // name ptr table RVA
    put<uint32_t>(builder.file, 0x600 + 36U, 0x2140U); // ordinal table RVA
    put<uint32_t>(builder.file, 0x700, 0x3000U);   // BaseFunc RVA (address table)
    put<uint16_t>(builder.file, 0x720, 0U);        // ordinal index 0
    text(builder.file, 0x740, "BaseFunc");
    builder.directory(0, 0x2000, 0x400);
    return builder.build();
}

// Build "dep.dll" importing "BaseFunc" from base.dll.
static std::vector<uint8_t> buildDepDll() {
    Builder builder(0x140000000ULL, 0x5000, 0x4000);
    builder.dll(true);
    builder.entry(0x1000);
    builder.section(".text", 0x1000, 0x400, 0x200, 0x400, 0x60000020U);
    builder.section(".idata", 0x2000, 0x800, 0x400, 0x800, 0xc0000040U);
    // Import descriptor at RVA 0x2000 (file offset 0x400)
    put<uint32_t>(builder.file, 0x400, 0x2100U);   // lookup table RVA
    put<uint32_t>(builder.file, 0x40c, 0x2200U);   // DLL name RVA
    put<uint32_t>(builder.file, 0x410, 0x2300U);   // IAT RVA
    text(builder.file, 0x600, "base.dll");          // DLL name at RVA 0x2200
    put<uint64_t>(builder.file, 0x500, 0x2400U);   // by-name thunk at RVA 0x2100
    put<uint64_t>(builder.file, 0x508, 0ULL);      // end of lookup table
    put<uint16_t>(builder.file, 0x800, 0U);        // hint at RVA 0x2400
    text(builder.file, 0x802, "BaseFunc");         // name at RVA 0x2402
    put<uint64_t>(builder.file, 0x700, 0ULL);      // IAT slot at RVA 0x2300
    builder.directory(1, 0x2000, 20);
    return builder.build();
}

// Build "cyc_a.dll" importing "FuncB" from cyc_b.dll.
static std::vector<uint8_t> buildCycADll() {
    Builder builder(0x140000000ULL, 0x5000, 0x4000);
    builder.dll(true);
    builder.entry(0x1000);
    builder.section(".text", 0x1000, 0x400, 0x200, 0x400, 0x60000020U);
    builder.section(".idata", 0x2000, 0x800, 0x400, 0x800, 0xc0000040U);
    put<uint32_t>(builder.file, 0x400, 0x2100U);
    put<uint32_t>(builder.file, 0x40c, 0x2200U);
    put<uint32_t>(builder.file, 0x410, 0x2300U);
    text(builder.file, 0x600, "cyc_b.dll");
    put<uint64_t>(builder.file, 0x500, 0x2400U);
    put<uint64_t>(builder.file, 0x508, 0ULL);
    put<uint16_t>(builder.file, 0x800, 0U);
    text(builder.file, 0x802, "FuncB");
    put<uint64_t>(builder.file, 0x700, 0ULL);
    builder.directory(1, 0x2000, 20);
    return builder.build();
}

// Build "cyc_b.dll" importing "FuncA" from cyc_a.dll.
static std::vector<uint8_t> buildCycBDll() {
    Builder builder(0x140000000ULL, 0x5000, 0x4000);
    builder.dll(true);
    builder.entry(0x1000);
    builder.section(".text", 0x1000, 0x400, 0x200, 0x400, 0x60000020U);
    builder.section(".idata", 0x2000, 0x800, 0x400, 0x800, 0xc0000040U);
    put<uint32_t>(builder.file, 0x400, 0x2100U);
    put<uint32_t>(builder.file, 0x40c, 0x2200U);
    put<uint32_t>(builder.file, 0x410, 0x2300U);
    text(builder.file, 0x600, "cyc_a.dll");
    put<uint64_t>(builder.file, 0x500, 0x2400U);
    put<uint64_t>(builder.file, 0x508, 0ULL);
    put<uint16_t>(builder.file, 0x800, 0U);
    text(builder.file, 0x802, "FuncA");
    put<uint64_t>(builder.file, 0x700, 0ULL);
    builder.directory(1, 0x2000, 20);
    return builder.build();
}

int main() {
    rottr_guest_mem* memory = rottr_guest_reserve(32ULL << 20U);
    if (!expect(memory != nullptr, "guest reserve")) return 1;

    rottr::PeModuleRegistry registry(memory);

    std::vector<uint8_t> baseDll = buildBaseDll();
    std::vector<uint8_t> depDll = buildDepDll();
    std::vector<uint8_t> cycADll = buildCycADll();
    std::vector<uint8_t> cycBDll = buildCycBDll();
    registry.setFileOpener([&](const std::string& name,
                               std::vector<uint8_t>& bytes) -> bool {
        if (name == "base.dll") { bytes = baseDll; return true; }
        if (name == "dep.dll") { bytes = depDll; return true; }
        if (name == "cyc_a.dll") { bytes = cycADll; return true; }
        if (name == "cyc_b.dll") { bytes = cycBDll; return true; }
        return false;
    });

    // ---- Step 14a: Load base.dll ----
    std::string error;
    rottr::PeImage* base = registry.loadModule("base.dll", &error);
    if (!expect(base != nullptr, "base.dll must load")) {
        std::fprintf(stderr, "  %s\n", error.c_str());
        return 1;
    }
    if (!expect(registry.refCount("base.dll") == 1, "base.dll ref count must be 1"))
        return 1;

    // ---- Step 14b: Load dep.dll which imports base.dll ----
    rottr::PeImage* dep = registry.loadModule("dep.dll", &error);
    if (!expect(dep != nullptr, "dep.dll must load")) {
        std::fprintf(stderr, "  %s\n", error.c_str());
        return 1;
    }
    if (!expect(registry.refCount("base.dll") == 2,
                "base.dll ref count must be 2 after dep.dll imports it"))
        return 1;
    if (!expect(registry.findModule("base.dll") != nullptr,
                "base.dll must be findable"))
        return 1;

    // ---- Step 14c: Dependency cycle does not hang ----
    rottr::PeImage* cycA = registry.loadModule("cyc_a.dll", &error);
    if (!expect(cycA != nullptr, "cyc_a.dll must load despite cycle")) {
        std::fprintf(stderr, "  %s\n", error.c_str());
        return 1;
    }
    rottr::PeImage* cycB = registry.loadModule("cyc_b.dll", &error);
    if (!expect(cycB != nullptr, "cyc_b.dll must load despite cycle")) {
        std::fprintf(stderr, "  %s\n", error.c_str());
        return 1;
    }

    // ---- Step 14d: Initialization order ----
    std::vector<rottr::PeImage*> order = registry.initializationOrder();
    if (!expect(order.size() >= 3, "initialization order must include all modules"))
        return 1;
    size_t baseIdx = 0, depIdx = 0;
    for (size_t i = 0; i < order.size(); ++i) {
        if (order[i] == base) baseIdx = i;
        if (order[i] == dep) depIdx = i;
    }
    if (!expect(baseIdx < depIdx, "base.dll must initialize before dep.dll"))
        return 1;

    std::printf("OK (modules=%zu base_refs=%d dep_refs=%d)\n",
                registry.moduleCount(), registry.refCount("base.dll"),
                registry.refCount("dep.dll"));
    rottr_guest_release(memory);
    return 0;
}
