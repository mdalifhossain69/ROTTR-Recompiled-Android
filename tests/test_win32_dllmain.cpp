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

// DllMain records that its dependency ran first and returns TRUE.
static std::vector<uint8_t> buildB(uint64_t markerAddress) {
    Builder builder(0x140010000ULL, 0x5000, 0x4000);
    builder.dll(true);
    builder.entry(0x1000);
    builder.section(".text", 0x1000, 0x400, 0x200, 0x400, 0x60000020U);
    builder.section(".edata", 0x2000, 0x800, 0x600, 0x400, 0x40000040U);
    builder.raw(0x200, {0x48, 0xb8});
    put<uint64_t>(builder.file, 0x202, markerAddress);
    builder.raw(0x20a, {0xc6, 0x00, 0x01, 0xb8, 0x01, 0x00, 0x00, 0x00, 0xc3});
    builder.raw(0x220, {0xc3});  // FuncB at RVA 0x1020
    addSingleExport(builder, 0x2000U, 0x600U, 0x1020U, "FuncB");
    return builder.build();
}

// DllMain succeeds only if b.dll's DllMain has already run.
static std::vector<uint8_t> buildA(uint64_t markerAddress) {
    Builder builder(0x140020000ULL, 0x5000, 0x4000);
    builder.dll(true);
    builder.entry(0x1000);
    builder.section(".text", 0x1000, 0x400, 0x200, 0x400, 0x60000020U);
    builder.section(".idata", 0x2000, 0x800, 0x400, 0x800, 0xc0000040U);
    builder.raw(0x200, {0x48, 0xb8});
    put<uint64_t>(builder.file, 0x202, markerAddress);
    builder.raw(0x20a, {0x0f, 0xb6, 0x08, 0x83, 0xf9, 0x01, 0x75, 0x09,
                        0xc6, 0x00, 0x02, 0xb8, 0x01, 0x00, 0x00, 0x00,
                        0xc3, 0x31, 0xc0, 0xc3});
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
    const uint64_t markerAddress = base + 0x500000U;

    std::vector<uint8_t> bDll = buildB(markerAddress);
    std::vector<uint8_t> aDll = buildA(markerAddress);
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
    if (!expect(a->missingImports().empty(),
                "a.dll must resolve FuncB instead of installing an import trap"))
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

    // ---- Step 17c: The registry calls DllMain in dependency order ----
    if (!expect(rottr_guest_commit_pages(memory, markerAddress,
                                         ROTTR_GUEST_PAGE_SIZE) == 0,
                "order marker page must commit"))
        return 1;
    const uint8_t initialMarker = 0;
    if (!expect(rottr_guest_write(memory, markerAddress, &initialMarker, 1) == 0,
                "order marker must initialize"))
        return 1;
    rottr_cpu cpu{};
    rottr_cpu_reset(&cpu, 0, base + 0x100000);
    // Commit stack page so callGuestProc can push the return sentinel.
    rottr_guest_commit_pages(memory, base + 0x100000 - 4096, 4096);
    rottr::X64GuestMemory guest{
        base, reinterpret_cast<uint8_t*>(rottr_guest_base(memory)),
        64ULL << 20U, true, true};

    const bool entryPointsOk = registry.callEntryPoints(cpu, guest);
    uint8_t marker = 0;
    rottr_guest_read(memory, markerAddress, &marker, 1);
    if (!expect(entryPointsOk,
                "registry must call DLL entry points in dependency order")) {
        std::fprintf(stderr, "  marker after failure: %u\n", marker);
        return 1;
    }
    if (!expect(rottr_guest_read(memory, markerAddress, &marker, 1) == 0 &&
                marker == 2,
                "dependent DllMain must observe and advance its dependency's marker"))
        return 1;

    std::printf("OK (order=b->a, marker=%u)\n", marker);
    rottr_guest_release(memory);
    return 0;
}
