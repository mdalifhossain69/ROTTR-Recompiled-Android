// Roadmap steps 9-13: PE mapping, relocations, protections, imports and exports.
#include "rottr/pe_loader.h"
#include "rottr/x64_runtime.h"

#include "pe_fixture.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#define CHECK(x)                                                                  \
    do {                                                                          \
        if (!(x)) {                                                               \
            std::printf("FAILED line %d: %s\n", __LINE__, #x);                    \
            return 1;                                                             \
        }                                                                         \
    } while (0)

using namespace pe_fixture;

static uint8_t readByte(const rottr::PeImage& image, uint64_t guestAddress) {
    uint8_t value = 0;
    if (!image.read(guestAddress, &value, sizeof(value))) return 0xff;
    return value;
}

static uint64_t readQword(const rottr::PeImage& image, uint64_t guestAddress) {
    uint64_t value = 0;
    if (!image.read(guestAddress, &value, sizeof(value))) return 0;
    return value;
}

static int testMappingAndProtection(rottr_guest_mem* memory, uint64_t loadBase) {
    Builder builder = simpleImage();
    std::vector<uint8_t> file = builder.build();

    rottr::PeImage image;
    rottr::PeLoadOptions options;
    options.loadBase = loadBase;
    const rottr::PeLoadError loadStatus = image.load(memory, file.data(), file.size(), options);
    if (loadStatus != rottr::PeLoadError::None)
        std::printf("load error: %s\n", rottr::describePeLoadError(loadStatus));
    CHECK(loadStatus == rottr::PeLoadError::None);
    CHECK(image.loaded() && image.base() == loadBase && !image.isDll());
    CHECK(image.entryPoint() == loadBase + 0x1000);
    CHECK(image.sections().size() == 2);

    uint16_t mz = 0;
    CHECK(image.read(image.base(), &mz, sizeof(mz)) && mz == 0x5a4d);
    uint32_t signature = 0;
    CHECK(image.read(image.base() + 0x80, &signature, sizeof(signature)) &&
          signature == 0x00004550U);
    for (uint32_t offset : {0u, 0x17fu, 0x3ffu}) {
        CHECK(readByte(image, image.base() + 0x1000 + offset) == file[0x400 + offset]);
        CHECK(readByte(image, image.base() + 0x2000 + offset) == file[0x800 + offset]);
    }
    // VirtualSize > SizeOfRawData: the uninitialized tail must be zero.
    CHECK(readByte(image, image.base() + 0x2200) == 0);
    CHECK(readByte(image, image.base() + 0x2fff) == 0);

    rottr_mem_result result{};
    CHECK(rottr_guest_check(memory, static_cast<uintptr_t>(image.base() + 0x1000), 4,
                            ROTTR_MEM_EXEC, &result) == 1);
    CHECK(rottr_guest_write(memory, static_cast<uintptr_t>(image.base() + 0x1000),
                            "\x90", 1) == -1);
    CHECK(rottr_guest_check(memory, static_cast<uintptr_t>(image.base() + 0x2000), 4,
                            ROTTR_MEM_EXEC, &result) == 0);
    CHECK(rottr_guest_write(memory, static_cast<uintptr_t>(image.base() + 0x2000),
                            "\x7f", 1) == 0);
    CHECK(readByte(image, image.base() + 0x2000) == 0x7f);

    // Truncation is rejected rather than silently zero-filled.
    Builder truncated = simpleImage();
    truncated.file.resize(0x900);
    std::vector<uint8_t> shortFile = truncated.build();
    rottr::PeImage bad;
    CHECK(bad.load(memory, shortFile.data(), shortFile.size(), options) ==
          rottr::PeLoadError::TruncatedSectionData);
    return 0;
}

static int testRelocations(rottr_guest_mem* memory, uint64_t first, uint64_t second) {
    Builder builder(0x140000000ULL, 0x5000, 0x4000);
    builder.entry(0x1000);
    builder.section(".text", 0x1000, 0x400, 0x400, 0x200, 0x60000020U);
    builder.section(".rdata", 0x2000, 0x1000, 0x1000, 0x400, 0x40000040U);
    builder.section(".reloc", 0x3000, 0x400, 0x1800, 0x200, 0x42000040U);
    put<uint64_t>(builder.file, 0x1000, 0x140001234ULL);
    put<uint32_t>(builder.file, 0x1800, 0x2000U);  // page RVA
    put<uint32_t>(builder.file, 0x1804, 12U);      // block size
    put<uint16_t>(builder.file, 0x1808, 0xa000U);  // DIR64 at offset 0
    put<uint16_t>(builder.file, 0x180a, 0x0000U);  // ABSOLUTE padding
    builder.directory(5, 0x3000, 12);
    std::vector<uint8_t> file = builder.build();

    for (uint64_t base : {first, second}) {
        rottr::PeImage image;
        rottr::PeLoadOptions options;
        options.loadBase = base;
        CHECK(image.load(memory, file.data(), file.size(), options) ==
              rottr::PeLoadError::None);
        const uint64_t expected = 0x140001234ULL + (base - 0x140000000ULL);
        CHECK(readQword(image, image.base() + 0x2000) == expected);
    }

    Builder invalid = builder;
    put<uint16_t>(invalid.file, 0x1808, 0x3000U);  // IMAGE_REL_BASED_HIGHLOW
    std::vector<uint8_t> badFile = invalid.build();
    rottr::PeImage bad;
    rottr::PeLoadOptions options;
    options.loadBase = second + 0x200000;
    const rottr::PeLoadError badStatus = bad.load(memory, badFile.data(), badFile.size(), options);
    if (badStatus != rottr::PeLoadError::UnsupportedRelocation)
        std::printf("bad reloc status: %s\n", rottr::describePeLoadError(badStatus));
    CHECK(badStatus == rottr::PeLoadError::UnsupportedRelocation);
    return 0;
}

static int testImports(rottr_guest_mem* memory, uint64_t loadBase) {
    Builder builder(0x140000000ULL, 0x5000, 0x4000);
    builder.entry(0x1000);
    builder.section(".text", 0x1000, 0x400, 0x400, 0x200, 0x60000020U);
    builder.section(".idata", 0x2000, 0x1000, 0x1000, 0x800, 0xc0000040U);
    put<uint32_t>(builder.file, 0x1000, 0x2100U);  // lookup table RVA
    put<uint32_t>(builder.file, 0x100c, 0x2200U);  // DLL name RVA
    put<uint32_t>(builder.file, 0x1010, 0x2300U);  // IAT RVA
    text(builder.file, 0x1200, "KERNEL32.dll");
    put<uint64_t>(builder.file, 0x1100, 0x2400U);       // by name
    put<uint64_t>(builder.file, 0x1108, 0x8000000000000012ULL);  // by ordinal 18
    put<uint64_t>(builder.file, 0x1110, 0x8000000000000017ULL);  // by ordinal 23 (missing)
    put<uint64_t>(builder.file, 0x1118, 0);
    put<uint16_t>(builder.file, 0x1400, 0);  // hint
    text(builder.file, 0x1402, "Sleep");
    for (uint32_t i = 0; i < 3; ++i)
        put<uint64_t>(builder.file, 0x1300 + i * 8ULL, 0x140000000ULL + i);
    builder.directory(1, 0x2000, 20);
    std::vector<uint8_t> file = builder.build();

    rottr::PeImage image;
    rottr::PeLoadOptions options;
    options.loadBase = loadBase;
    options.importResolver = [](const rottr::PeImportRequest& request, uint64_t& address) {
        if (request.module == "KERNEL32.dll" && request.function == "Sleep") {
            address = 0x7ff800001000ULL;
            return true;
        }
        if (request.byOrdinal && request.ordinal == 18) {
            address = 0x7ff800002000ULL;
            return true;
        }
        return false;
    };
    const rottr::PeLoadError importStatus = image.load(memory, file.data(), file.size(), options);
    if (importStatus != rottr::PeLoadError::None)
        std::printf("import load: %s\n", rottr::describePeLoadError(importStatus));
    CHECK(importStatus == rottr::PeLoadError::None);
    CHECK(image.imports().size() == 3);
    CHECK(readQword(image, image.base() + 0x2300) == 0x7ff800001000ULL);
    CHECK(readQword(image, image.base() + 0x2308) == 0x7ff800002000ULL);
    CHECK(image.missingImports().size() == 1);
    CHECK(image.missingImports()[0].description == "KERNEL32.dll!#23");

    const uint64_t trap = image.missingImports()[0].trapAddress;
    CHECK(readQword(image, image.base() + 0x2310) == trap);
    CHECK(readByte(image, trap) == 0x0f && readByte(image, trap + 1) == 0x0b);
    CHECK(image.missingImportAt(trap) == "KERNEL32.dll!#23");
    CHECK(image.missingImportAt(image.base() + 0x1000).empty());

    return 0;
}

static int testExports(rottr_guest_mem* memory, uint64_t loadBase) {
    Builder builder(0x140000000ULL, 0x5000, 0x4000);
    builder.entry(0x1000);
    builder.section(".text", 0x1000, 0x400, 0x400, 0x200, 0x60000020U);
    builder.section(".edata", 0x2000, 0x800, 0x1000, 0x600, 0x40000040U);
    put<uint32_t>(builder.file, 0x1010, 1U);     // ordinal base
    put<uint32_t>(builder.file, 0x1014, 3U);     // function count
    put<uint32_t>(builder.file, 0x1018, 2U);     // name count
    put<uint32_t>(builder.file, 0x101c, 0x2100U);
    put<uint32_t>(builder.file, 0x1020, 0x2120U);
    put<uint32_t>(builder.file, 0x1024, 0x2140U);
    text(builder.file, 0x1060, "OTHER.Beta");    // forwarder inside the directory
    put<uint32_t>(builder.file, 0x1100, 0x3000U);
    put<uint32_t>(builder.file, 0x1104, 0x2060U);
    put<uint32_t>(builder.file, 0x1108, 0x3004U);
    put<uint32_t>(builder.file, 0x1120, 0x2200U);
    put<uint32_t>(builder.file, 0x1124, 0x2210U);
    put<uint16_t>(builder.file, 0x1140, 0U);
    put<uint16_t>(builder.file, 0x1142, 1U);
    text(builder.file, 0x1200, "Alpha");
    text(builder.file, 0x1210, "Beta");
    builder.directory(0, 0x2000, 0x400);
    std::vector<uint8_t> file = builder.build();

    rottr::PeImage image;
    rottr::PeLoadOptions options;
    options.loadBase = loadBase;
    const rottr::PeLoadError exportStatus = image.load(memory, file.data(), file.size(), options);
    if (exportStatus != rottr::PeLoadError::None)
        std::printf("export load: %s\n", rottr::describePeLoadError(exportStatus));
    CHECK(exportStatus == rottr::PeLoadError::None);
    CHECK(image.exports().size() == 3);
    rottr::PeExport exported;
    CHECK(image.findExport("Alpha", exported) && exported.address == loadBase + 0x3000);
    CHECK(image.findExportByOrdinal(3, exported) && exported.address == loadBase + 0x3004);
    CHECK(image.findExport("Beta", exported) && exported.forwarder == "OTHER.Beta");
    CHECK(image.findExportByOrdinal(2, exported) && exported.forwarder == "OTHER.Beta");
    CHECK(!image.findExport("Missing", exported));
    return 0;
}

// Opt-in validation against the user's own copy of the game. No game files are
// committed, copied or modified: this test only reads the executable.
static int testRealGame(const char* directory) {
    const std::string path = std::string(directory) + "/ROTTR.exe";
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::printf("ROTTR_GAME_DIR set but %s could not be opened\n", path.c_str());
        return 1;
    }
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
    if (file.size() < 0x1000) return 1;
    rottr_guest_mem* memory = rottr_guest_reserve(512ULL << 20U);
    if (memory == nullptr) return 1;
    rottr::PeImage image;
    const rottr::PeLoadError status = image.load(memory, file.data(), file.size());
    const bool ok = status == rottr::PeLoadError::None && image.loaded() &&
                    image.base() >= reinterpret_cast<uintptr_t>(rottr_guest_base(memory));
    if (!ok) std::printf("real ROTTR.exe load: %s\n", rottr::describePeLoadError(status));
    else std::printf("%s", image.describe().c_str());
    rottr_guest_release(memory);
    return ok ? 0 : 1;
}

int main() {
    rottr_guest_mem* memory = rottr_guest_reserve(8ULL << 20U);
    if (memory == nullptr) {
        std::puts("FAILED: guest reservation");
        return 1;
    }
    const uint64_t reservation = reinterpret_cast<uintptr_t>(rottr_guest_base(memory));
    int result = testMappingAndProtection(memory, reservation + 0x10000);
    if (result == 0) result = testRelocations(memory, reservation + 0x20000,
                                              reservation + 0x400000);
    if (result == 0) result = testImports(memory, reservation + 0x30000);
    if (result == 0) result = testExports(memory, reservation + 0x180000);
    rottr_guest_release(memory);
    if (result != 0) return result;

    if (const char* directory = std::getenv("ROTTR_GAME_DIR"))
        return testRealGame(directory);
    std::puts("pe_loader: OK (set ROTTR_GAME_DIR to validate ROTTR.exe)");
    return 0;
}
