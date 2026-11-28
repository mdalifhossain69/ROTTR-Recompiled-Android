// Roadmap step 45: report all imports as tested, implemented or missing.
// Pass: the report runs and unknown calls never return fake success.
#include "rottr/import_report.h"
#include "rottr/x64_bridge.h"

#include "pe_fixture.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace pe_fixture;

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

std::vector<uint8_t> importFixture() {
    Builder builder(0x140000000ULL, 0x5000, 0x4000);
    builder.entry(0x1000);
    builder.section(".text", 0x1000, 0x400, 0x400, 0x200, 0x60000020U);
    builder.section(".idata", 0x2000, 0x1000, 0x1000, 0x800, 0xc0000040U);
    // Import descriptor at RVA 0x2000 -> raw 0x1000.
    put<uint32_t>(builder.file, 0x1000, 0x2100U);  // OriginalFirstThunk
    put<uint32_t>(builder.file, 0x100c, 0x2200U);  // module name RVA
    put<uint32_t>(builder.file, 0x1010, 0x2300U);  // FirstThunk (IAT)
    text(builder.file, 0x1200, "KERNEL32.dll");
    // Thunks at RVA 0x2100 -> raw 0x1100.
    put<uint64_t>(builder.file, 0x1100, 0x2400U);
    put<uint64_t>(builder.file, 0x1108, 0x2410U);
    put<uint64_t>(builder.file, 0x1110, 0x2420U);
    put<uint64_t>(builder.file, 0x1118, 0x8000000000000017ULL);  // ordinal 23
    put<uint64_t>(builder.file, 0x1120, 0);
    // Hint/name entries at RVA 0x2400 -> raw 0x1400.
    put<uint16_t>(builder.file, 0x1400, 0);
    text(builder.file, 0x1402, "Sleep");
    put<uint16_t>(builder.file, 0x1410, 0);
    text(builder.file, 0x1412, "GetSystemTime");
    put<uint16_t>(builder.file, 0x1420, 0);
    text(builder.file, 0x1422, "DefinitelyMissing");
    builder.directory(1, 0x2000, 20);
    return builder.build();
}

}  // namespace

int main() {
    rottr::registerHostModules();
    const std::vector<uint8_t> file = importFixture();
    const rottr::ImportReport report = rottr::buildImportReport(file.data(), file.size());
    if (!expect(report.ok(), "report must parse the import table")) return 1;
    if (!expect(report.entries.size() == 4, "report must contain all four imports"))
        return 1;

    const rottr::ImportEntry& sleep = report.entries[0];
    if (!expect(sleep.function.module == "KERNEL32.dll" &&
                    sleep.function.name == "Sleep" &&
                    sleep.status == rottr::ImportStatus::Tested,
                "Sleep must be classified tested"))
        return 1;
    const rottr::ImportEntry& systemTime = report.entries[1];
    if (!expect(systemTime.function.name == "GetSystemTime" &&
                    systemTime.status == rottr::ImportStatus::Tested,
                "GetSystemTime must be classified tested"))
        return 1;
    const rottr::ImportEntry& missing = report.entries[2];
    if (!expect(missing.function.name == "DefinitelyMissing" &&
                    missing.status == rottr::ImportStatus::Missing,
                "unknown named import must be classified missing"))
        return 1;
    const rottr::ImportEntry& ordinal = report.entries[3];
    if (!expect(ordinal.function.byOrdinal && ordinal.function.ordinal == 23 &&
                    ordinal.status == rottr::ImportStatus::Missing,
                "unknown ordinal import must be classified missing"))
        return 1;
    if (!expect(report.tested == 2 && report.implemented == 0 &&
                    report.missing == 2,
                "report counts must match the classified imports"))
        return 1;

    const std::string text = rottr::formatImportReport(report);
    if (!expect(text.find("2 missing") != std::string::npos &&
                    text.find("KERNEL32.dll!DefinitelyMissing") != std::string::npos &&
                    text.find("KERNEL32.dll!#23") != std::string::npos,
                "formatted report must list the missing imports"))
        return 1;

    // Unknown calls resolve to no host address; the PE loader turns that into
    // a UD2 trap rather than a fake success.
    if (!expect(rottr::isHostExportImplemented("KERNEL32.dll", "Sleep", 0, false),
                "Sleep must be reported as implemented"))
        return 1;
    if (!expect(!rottr::isHostExportImplemented("kernel32.dll", "DefinitelyMissing",
                                                0, false),
                "unknown export must not be reported as implemented"))
        return 1;
    if (!expect(rottr::resolveHostExport("kernel32.dll", "DefinitelyMissing", 0,
                                         false) == 0,
                "unknown export must resolve to no address"))
        return 1;

    // Truncated input is rejected, not silently reported as zero imports.
    const rottr::ImportReport bad = rottr::buildImportReport(file.data(), 0x40);
    if (!expect(!bad.ok(), "truncated file must produce an error")) return 1;

    std::puts("OK (tested/implemented/missing report, unknown exports have no address)");
    return 0;
}
