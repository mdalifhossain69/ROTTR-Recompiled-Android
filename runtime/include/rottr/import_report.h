// Roadmap step 45: report every import of a user-supplied EXE as tested,
// implemented or missing. Missing imports are never turned into fake success:
// the PE loader maps them to UD2 trap thunks and the report marks them missing.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rottr {

struct ImportedFunction {
    std::string module;
    std::string name;
    uint16_t ordinal = 0;
    bool byOrdinal = false;
};

enum class ImportStatus : uint8_t { Tested, Implemented, Missing };

struct ImportEntry {
    ImportedFunction function;
    ImportStatus status = ImportStatus::Missing;
};

struct ImportReport {
    std::string error;  // empty when the import table was read successfully
    std::vector<ImportEntry> entries;
    size_t tested = 0;
    size_t implemented = 0;
    size_t missing = 0;
    bool ok() const { return error.empty(); }
};

// Parses the import directory of a PE32/PE32+ file and classifies every
// imported function against the registered host modules. Call
// registerHostModules() before this to populate the implementation list.
ImportReport buildImportReport(const uint8_t* file, size_t fileSize);

std::string formatImportReport(const ImportReport& report);

// True when a host module implements the export. Unknown exports return false
// (the loader then creates a trap; it never returns a fake success address).
bool isHostExportImplemented(const std::string& module, const std::string& name,
                             uint16_t ordinal, bool byOrdinal);

}  // namespace rottr
