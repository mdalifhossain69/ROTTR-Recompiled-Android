// Read-only header check of a user-supplied Windows executable. Nothing is executed,
// modified or unpacked. Mirrors scripts/recon.py.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rottr {

struct PeSection {
    std::string name;
    uint32_t rva = 0, virtualSize = 0, fileSize = 0, fileOffset = 0;
    bool executable = false;
    double entropy = 0.0;
};

struct PeImport {
    std::string dll;
    uint32_t functions = 0;
};

struct PeReport {
    std::string error;  // empty when the headers were read successfully
    uint16_t machine = 0;
    bool pe32Plus = false;
    uint32_t entryRva = 0;
    uint64_t imageBase = 0;
    uint32_t imageSize = 0;
    std::vector<PeSection> sections;
    std::vector<PeImport> imports;
    std::vector<std::string> protectionIndicators;

    bool isX64() const { return error.empty() && pe32Plus && machine == 0x8664; }
    // True only for an x64 program with no sign of protection.
    bool passed() const { return isX64() && protectionIndicators.empty(); }
};

PeReport inspectPe(const uint8_t* data, size_t size);
std::string describePe(const PeReport& report);

}  // namespace rottr
