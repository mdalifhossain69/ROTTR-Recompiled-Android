#include "rottr/pe_inspect.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace rottr {
namespace {

constexpr uint32_t kMaxSections = 96;  // the Windows loader's own limit
constexpr uint32_t kMaxDlls = 1024;
constexpr uint32_t kMaxFunctionsPerDll = 65536;
constexpr size_t kMaxNameLength = 256;

// Every read is bounds-checked: the file is untrusted input.
struct View {
    const uint8_t* data;
    size_t size;

    template <typename T>
    bool read(uint64_t offset, T& out) const {
        if (offset > size || size - offset < sizeof(T)) return false;
        std::memcpy(&out, data + offset, sizeof(T));  // PE and supported hosts are little-endian
        return true;
    }
};

double entropyOf(const uint8_t* bytes, size_t length) {
    if (length == 0) return 0.0;
    size_t counts[256] = {};
    for (size_t i = 0; i < length; ++i) ++counts[bytes[i]];
    double result = 0.0;
    for (size_t count : counts) {
        if (count == 0) continue;
        const double p = static_cast<double>(count) / static_cast<double>(length);
        result -= p * std::log2(p);
    }
    return result;
}

bool fileOffset(const PeReport& report, uint32_t rva, uint64_t& out) {
    for (const PeSection& s : report.sections) {
        if (rva >= s.rva && rva - s.rva < s.fileSize) {
            out = static_cast<uint64_t>(s.fileOffset) + (rva - s.rva);
            return true;
        }
    }
    return false;
}

bool readName(const View& view, uint64_t offset, std::string& out) {
    out.clear();
    for (size_t i = 0; i <= kMaxNameLength; ++i) {
        uint8_t c = 0;
        if (!view.read(offset + i, c)) return false;
        if (c == 0) return true;
        out.push_back(c >= 0x20 && c < 0x7F ? static_cast<char>(c) : '?');
    }
    return false;
}

bool readImports(const View& view, PeReport& report, uint32_t directoryRva) {
    uint64_t table = 0;
    if (!fileOffset(report, directoryRva, table)) return false;
    for (uint32_t i = 0; i < kMaxDlls; ++i) {
        const uint64_t entry = table + 20ULL * i;
        uint32_t lookupRva = 0, nameRva = 0, addressRva = 0;
        if (!view.read(entry, lookupRva) || !view.read(entry + 12, nameRva) ||
            !view.read(entry + 16, addressRva))
            return false;
        if (nameRva == 0) return true;
        PeImport import;
        uint64_t offset = 0;
        if (!fileOffset(report, nameRva, offset) || !readName(view, offset, import.dll)) return false;
        const uint32_t thunkRva = lookupRva != 0 ? lookupRva : addressRva;
        uint64_t thunks = 0;
        if (thunkRva != 0 && fileOffset(report, thunkRva, thunks)) {
            const uint64_t width = report.pe32Plus ? 8 : 4;
            while (import.functions < kMaxFunctionsPerDll) {
                uint64_t value = 0;
                if (report.pe32Plus) {
                    if (!view.read(thunks + width * import.functions, value)) break;
                } else {
                    uint32_t narrow = 0;
                    if (!view.read(thunks + width * import.functions, narrow)) break;
                    value = narrow;
                }
                if (value == 0) break;
                ++import.functions;
            }
        }
        report.imports.push_back(import);
    }
    return false;  // no terminator within the limit
}

PeReport failure(const char* message) {
    PeReport report;
    report.error = message;
    return report;
}

}  // namespace

PeReport inspectPe(const uint8_t* data, size_t size) {
    const View view{data, size};
    uint16_t mz = 0;
    if (!view.read(0, mz) || mz != 0x5A4D) return failure("not a Windows executable (no MZ header)");
    uint32_t pe = 0, signature = 0;
    if (!view.read(0x3C, pe) || !view.read(pe, signature) || signature != 0x00004550)
        return failure("PE signature not found");

    PeReport report;
    uint16_t sectionCount = 0, optionalSize = 0, magic = 0;
    const uint64_t optional = static_cast<uint64_t>(pe) + 24;
    if (!view.read(pe + 4ULL, report.machine) || !view.read(pe + 6ULL, sectionCount) ||
        !view.read(pe + 20ULL, optionalSize) || !view.read(optional, magic))
        return failure("truncated PE header");
    if (magic != 0x10B && magic != 0x20B) return failure("unknown optional header");
    report.pe32Plus = magic == 0x20B;
    if (sectionCount > kMaxSections) return failure("too many sections");

    uint32_t directoryCount = 0;
    if (!view.read(optional + 16, report.entryRva) || !view.read(optional + 56, report.imageSize) ||
        !view.read(optional + (report.pe32Plus ? 108 : 92), directoryCount))
        return failure("truncated optional header");
    if (report.pe32Plus) {
        if (!view.read(optional + 24, report.imageBase)) return failure("truncated optional header");
    } else {
        uint32_t base = 0;
        if (!view.read(optional + 28, base)) return failure("truncated optional header");
        report.imageBase = base;
    }

    for (uint32_t i = 0; i < sectionCount; ++i) {
        const uint64_t header = optional + optionalSize + 40ULL * i;
        char name[8] = {};
        uint32_t flags = 0;
        PeSection s;
        if (!view.read(header, name) || !view.read(header + 8, s.virtualSize) ||
            !view.read(header + 12, s.rva) || !view.read(header + 16, s.fileSize) ||
            !view.read(header + 20, s.fileOffset) || !view.read(header + 36, flags))
            return failure("truncated section table");
        for (char c : name) {
            if (c == 0) break;
            s.name.push_back(c >= 0x20 && c < 0x7F ? c : '?');
        }
        s.executable = (flags & 0x20000000U) != 0;
        if (s.fileOffset < size)
            s.entropy = entropyOf(data + s.fileOffset,
                                  std::min<size_t>(s.fileSize, size - s.fileOffset));
        report.sections.push_back(s);
    }

    if (directoryCount > 1) {
        uint32_t importRva = 0;
        if (!view.read(optional + (report.pe32Plus ? 112 : 96) + 8, importRva))
            return failure("truncated data directories");
        if (importRva != 0 && !readImports(view, report, importRva))
            report.protectionIndicators.emplace_back("import table unreadable");
    }
    for (const PeSection& s : report.sections) {
        if (s.name == ".bind")
            report.protectionIndicators.emplace_back(".bind section (typical of the SteamStub wrapper)");
        if (s.executable && s.entropy > 7.2)
            report.protectionIndicators.push_back("executable section '" + s.name +
                                                  "' looks packed or encrypted");
    }
    return report;
}

std::string describePe(const PeReport& report) {
    if (!report.error.empty()) return "Header check failed: " + report.error + "\n";
    char line[256];
    std::snprintf(line, sizeof(line),
                  "%s, machine 0x%x, entry RVA 0x%x, image base 0x%llx, image size %.1f MiB\n",
                  report.pe32Plus ? "PE32+" : "PE32", static_cast<unsigned>(report.machine),
                  report.entryRva, static_cast<unsigned long long>(report.imageBase),
                  report.imageSize / 1048576.0);
    std::string text = line;
    text += "Sections: " + std::to_string(report.sections.size()) + "\n";
    uint64_t functions = 0;
    for (const PeImport& i : report.imports) functions += i.functions;
    text += "Imports the app would have to provide: " + std::to_string(functions) +
            " functions from " + std::to_string(report.imports.size()) + " DLLs\n";
    for (const PeImport& i : report.imports)
        text += "  " + i.dll + " (" + std::to_string(i.functions) + ")\n";
    if (!report.isX64()) text += "Result: not a 64-bit x86 program.\n";
    if (!report.protectionIndicators.empty()) {
        text += "Protection indicators found. This project does not remove protection:\n";
        for (const std::string& f : report.protectionIndicators) text += "  " + f + "\n";
    } else if (report.isX64()) {
        text += "Result: headers read; no protection indicators found "
                "(this does not show whether a copy is genuine).\n";
    }
    return text;
}

}  // namespace rottr
