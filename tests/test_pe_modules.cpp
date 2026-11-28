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

static void addSingleExport(Builder& builder, uint32_t directoryRva,
                            size_t rawOffset, uint32_t functionRva,
                            const char* functionName) {
    put<uint32_t>(builder.file, rawOffset + 16U, 1U);
    put<uint32_t>(builder.file, rawOffset + 20U, 1U);
    put<uint32_t>(builder.file, rawOffset + 24U, 1U);
    put<uint32_t>(builder.file, rawOffset + 28U, directoryRva + 0x100U);
    put<uint32_t>(builder.file, rawOffset + 32U, directoryRva + 0x120U);
    put<uint32_t>(builder.file, rawOffset + 36U, directoryRva + 0x140U);
    put<uint32_t>(builder.file, rawOffset + 0x100U, functionRva);
    put<uint32_t>(builder.file, rawOffset + 0x120U, directoryRva + 0x160U);
    put<uint16_t>(builder.file, rawOffset + 0x140U, 0U);
    text(builder.file, rawOffset + 0x160U, functionName);
    builder.directory(0, directoryRva, 0x400U);
}

// Build "base.dll" exporting "BaseFunc" at RVA 0x1000.
static std::vector<uint8_t> buildBaseDll() {
    Builder builder(0x140000000ULL, 0x5000, 0x4000);
    builder.dll(true);
    builder.entry(0x1000);
    builder.section(".text", 0x1000, 0x400, 0x200, 0x400, 0x60000020U);
    builder.section(".edata", 0x2000, 0x800, 0x600, 0x400, 0x40000040U);
    builder.raw(0x200, {0xb8, 0x2a, 0x00, 0x00, 0x00, 0xc3});
    addSingleExport(builder, 0x2000U, 0x600U, 0x1000U, "BaseFunc");
    return builder.build();
}

// Build "dep.dll" importing "BaseFunc" from base.dll.
static std::vector<uint8_t> buildDepDll() {
    Builder builder(0x140000000ULL, 0x5000, 0x4000);
    builder.dll(true);
    builder.entry(0x1000);
    builder.section(".text", 0x1000, 0x400, 0x200, 0x400, 0x60000020U);
    builder.section(".idata", 0x2000, 0x800, 0x400, 0x800, 0xc0000040U);
    builder.section(".edata", 0x3000, 0x400, 0xc00, 0x400, 0x40000040U);
    // Call BaseFunc through the IAT, then return its result to the caller.
    builder.raw(0x200, {0xff, 0x15, 0xfa, 0x12, 0x00, 0x00, 0xc3});
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
    addSingleExport(builder, 0x3000U, 0xc00U, 0x1000U, "DepFunc");
    return builder.build();
}

static std::vector<uint8_t> buildCallingExe() {
    Builder builder(0x140000000ULL, 0x5000, 0x4000);
    builder.entry(0x1000);
    builder.section(".text", 0x1000, 0x400, 0x200, 0x400, 0x60000020U);
    builder.section(".idata", 0x2000, 0x800, 0x400, 0x800, 0xc0000040U);
    // call qword ptr [rip + 0x12fa] (the DepFunc IAT slot), then stop.
    builder.raw(0x200, {0xff, 0x15, 0xfa, 0x12, 0x00, 0x00, 0xf4});
    put<uint32_t>(builder.file, 0x400, 0x2100U);
    put<uint32_t>(builder.file, 0x40c, 0x2200U);
    put<uint32_t>(builder.file, 0x410, 0x2300U);
    text(builder.file, 0x600, "dep.dll");
    put<uint64_t>(builder.file, 0x500, 0x2400U);
    put<uint64_t>(builder.file, 0x508, 0ULL);
    put<uint16_t>(builder.file, 0x800, 0U);
    text(builder.file, 0x802, "DepFunc");
    put<uint64_t>(builder.file, 0x700, 0ULL);
    builder.directory(1, 0x2000U, 20U);
    return builder.build();
}

// Build "cyc_a.dll" importing "FuncB" from cyc_b.dll.
static std::vector<uint8_t> buildCycADll() {
    Builder builder(0x140000000ULL, 0x5000, 0x4000);
    builder.dll(true);
    builder.entry(0x1000);
    builder.section(".text", 0x1000, 0x400, 0x200, 0x400, 0x60000020U);
    builder.section(".idata", 0x2000, 0x800, 0x400, 0x800, 0xc0000040U);
    builder.section(".edata", 0x3000, 0x400, 0xc00, 0x400, 0x40000040U);
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
    addSingleExport(builder, 0x3000U, 0xc00U, 0x1000U, "FuncA");
    return builder.build();
}

// Build "cyc_b.dll" importing "FuncA" from cyc_a.dll.
static std::vector<uint8_t> buildCycBDll() {
    Builder builder(0x140000000ULL, 0x5000, 0x4000);
    builder.dll(true);
    builder.entry(0x1000);
    builder.section(".text", 0x1000, 0x400, 0x200, 0x400, 0x60000020U);
    builder.section(".idata", 0x2000, 0x800, 0x400, 0x800, 0xc0000040U);
    builder.section(".edata", 0x3000, 0x400, 0xc00, 0x400, 0x40000040U);
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
    addSingleExport(builder, 0x3000U, 0xc00U, 0x1000U, "FuncB");
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
    if (!expect(dep->missingImports().empty(),
                "dep.dll must resolve BaseFunc instead of installing an import trap"))
        return 1;

    // ---- Step 14c: Dependency cycle does not hang ----
    rottr::PeImage* cycA = registry.loadModule("cyc_a.dll", &error);
    if (!expect(cycA != nullptr, "cyc_a.dll must load despite cycle")) {
        std::fprintf(stderr, "  %s\n", error.c_str());
        return 1;
    }
    if (!expect(cycA->missingImports().empty(),
                "cyc_a.dll must resolve FuncB across the cycle"))
        return 1;
    rottr::PeImage* cycB = registry.loadModule("cyc_b.dll", &error);
    if (!expect(cycB != nullptr, "cyc_b.dll must load despite cycle")) {
        std::fprintf(stderr, "  %s\n", error.c_str());
        return 1;
    }
    if (!expect(cycB->missingImports().empty(),
                "cyc_b.dll must resolve FuncA across the cycle"))
        return 1;

    // ---- Step 14d: Execute an EXE call through its DLL import ----
    std::vector<uint8_t> callingExe = buildCallingExe();
    auto exe = std::make_unique<rottr::PeImage>();
    rottr::PeLoadOptions exeOptions;
    exeOptions.importResolver = [&registry](const rottr::PeImportRequest& request,
                                             uint64_t& address) {
        return registry.resolveImport(request, address);
    };
    const rottr::PeLoadError exeStatus = exe->load(
            memory, callingExe.data(), callingExe.size(), exeOptions);
    if (!expect(exeStatus == rottr::PeLoadError::None,
                "calling EXE must load")) {
        std::fprintf(stderr, "  %s\n", rottr::describePeLoadError(exeStatus));
        return 1;
    }
    if (!expect(exe->missingImports().empty(),
                "calling EXE must resolve DepFunc"))
        return 1;
    rottr::PeImage* mainImage = registry.addImage(std::move(exe), "main.exe");
    if (!expect(mainImage != nullptr, "main EXE must register"))
        return 1;

    const uintptr_t guestBase = reinterpret_cast<uintptr_t>(rottr_guest_base(memory));
    constexpr size_t guestSize = 32ULL << 20U;
    rottr_mem_result guestRegion{};
    if (!expect(rottr_guest_translate(memory, guestBase, guestSize, &guestRegion) == 1,
                "guest region must translate"))
        return 1;
    if (!expect(rottr_guest_commit_pages(memory, guestBase + 0xff000U,
                                          ROTTR_GUEST_PAGE_SIZE) == 0,
                "guest stack page must commit"))
        return 1;
    rottr::X64GuestMemory guest{
            guestBase, static_cast<uint8_t*>(guestRegion.host), guestSize, true, true};
    rottr_cpu cpu{};
    rottr_cpu_reset(&cpu, mainImage->entryPoint(), guestBase + 0x100000U);
    const rottr::X64RunResult run = rottr::runX64Interpreter(cpu, guest, 32);
    if (!expect(run.reason == rottr::X64StopReason::Halted,
                "calling EXE must return from its DLL call and halt") ||
        !expect(cpu.gpr[0] == 42,
                "calling EXE must receive BaseFunc's result through DepFunc"))
        return 1;

    // ---- Step 14e: Initialization order ----
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
