#include "rottr/import_report.h"

#include "rottr/x64_bridge.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <set>

namespace rottr {
namespace {

constexpr uint32_t kMaxSections = 96;
constexpr uint32_t kMaxModules = 1024;
constexpr uint32_t kMaxFunctions = 65536;
constexpr size_t kMaxName = 512;

struct Section {
    uint32_t rva = 0;
    uint32_t fileSize = 0;
    uint32_t fileOffset = 0;
};

struct View {
    const uint8_t* data = nullptr;
    size_t size = 0;

    bool range(uint64_t offset, uint64_t length) const {
        return offset <= size && length <= size - offset;
    }

    template <typename T>
    bool read(uint64_t offset, T& out) const {
        if (!range(offset, sizeof(T))) return false;
        std::memcpy(&out, data + offset, sizeof(T));
        return true;
    }
};

std::string lower(std::string value) {
    for (char& c : value) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return value;
}

bool rvaToOffset(const std::vector<Section>& sections, uint32_t rva, uint32_t length,
                 uint64_t& offset) {
    for (const Section& section : sections) {
        if (rva < section.rva) continue;
        const uint64_t delta = static_cast<uint64_t>(rva) - section.rva;
        if (delta + length > section.fileSize) continue;
        offset = static_cast<uint64_t>(section.fileOffset) + delta;
        return true;
    }
    return false;
}

bool readName(const View& view, uint64_t offset, std::string& out) {
    out.clear();
    for (size_t i = 0; i <= kMaxName; ++i) {
        uint8_t c = 0;
        if (!view.read(offset + i, c)) return false;
        if (c == 0) return true;
        out.push_back(c >= 0x20 && c < 0x7f ? static_cast<char>(c) : '?');
    }
    return false;
}

bool isTested(const std::string& module, const std::string& name) {
    static const std::set<std::string> tested = {
        "kernel32.dll!sleep",
        "kernel32.dll!getsystemtime",
        "kernel32.dll!getversionexa",
        "kernel32.dll!getsysteminfo",
        "kernel32.dll!getcomputernamea",
        "kernel32.dll!getdiskfreespaceexa",
        "kernel32.dll!globalmemorystatusex",
        "kernel32.dll!getsystemdirectorya",
        "kernel32.dll!getsystempowerstatus",
        "kernel32.dll!getenvironmentvariablea",
        "kernel32.dll!getlasterror",
        "kernel32.dll!setlasterror",
        "kernel32.dll!exitprocess",
        "kernel32.dll!gettickcount",
        "kernel32.dll!gettickcount64",
        "kernel32.dll!queryperformancecounter",
        "kernel32.dll!queryperformancefrequency",
        "kernel32.dll!createthread",
        "kernel32.dll!exitthread",
        "kernel32.dll!createfilea",
        "kernel32.dll!readfile",
        "kernel32.dll!writefile",
        "kernel32.dll!closehandle",
        "kernel32.dll!setfilepointer",
        "kernel32.dll!findfirstfilea",
        "kernel32.dll!findnextfilea",
        "kernel32.dll!findclose",
        "kernel32.dll!createfilemappinga",
        "kernel32.dll!mapviewoffile",
        "kernel32.dll!unmapviewoffile",
        "kernel32.dll!heapalloc",
        "kernel32.dll!heapfree",
        "kernel32.dll!heaprealloc",
        "kernel32.dll!getprocessheap",
        "kernel32.dll!initializecriticalsection",
        "kernel32.dll!entercriticalsection",
        "kernel32.dll!leavecriticalsection",
        "kernel32.dll!deletecriticalsection",
        "kernel32.dll!createeventa",
        "kernel32.dll!setevent",
        "kernel32.dll!waitforsingleobject",
        "kernel32.dll!createmutexa",
        "kernel32.dll!releasemutex",
        "kernel32.dll!createsemaphorea",
        "kernel32.dll!releasesemaphore",
        "kernel32.dll!virtualalloc",
        "kernel32.dll!virtualfree",
        "kernel32.dll!virtualprotect",
        "kernel32.dll!virtualquery",
        "kernel32.dll!getcurrentprocessid",
        "kernel32.dll!getcurrentthreadid",
        "kernel32.dll!getcurrentprocess",
        "kernel32.dll!getcurrentthread",
        "kernel32.dll!getcommandlinea",
        "kernel32.dll!getmodulehandlea",
        "kernel32.dll!getprocaddress",
        "kernel32.dll!loadlibrarya",
        "advapi32.dll!regopenkeyexa",
        "advapi32.dll!regcreatekeyexa",
        "advapi32.dll!regsetvalueexa",
        "advapi32.dll!regqueryvalueexa",
        "advapi32.dll!regclosekey",
        "advapi32.dll!regdeletevaluea",
        "advapi32.dll!regenumkeyexa",
        "advapi32.dll!regenumvaluea",
        "advapi32.dll!regflushkey",
        "shell32.dll!shgetfolderpatha",
        "user32.dll!registerclassa",
        "user32.dll!registerclassexa",
        "user32.dll!createwindowexa",
        "user32.dll!destroywindow",
        "user32.dll!showwindow",
        "user32.dll!movewindow",
        "user32.dll!setwindowpos",
        "user32.dll!setfocus",
        "user32.dll!getfocus",
        "user32.dll!setwindowtexta",
        "user32.dll!postmessagea",
        "user32.dll!postquitmessage",
        "user32.dll!getmessagea",
        "user32.dll!peekmessagea",
        "user32.dll!translatemessage",
        "user32.dll!dispatchmessagea",
        "user32.dll!defwindowproca",
        "ole32.dll!coinitializeex",
        "ole32.dll!coinitialize",
        "ole32.dll!couninitialize",
        "ole32.dll!cocreateinstance",
        "ole32.dll!cotaskmemalloc",
        "ole32.dll!cotaskmemfree",
        "ws2_32.dll!wsastartup",
        "ws2_32.dll!wsacleanup",
        "ws2_32.dll!wsagetlasterror",
        "ws2_32.dll!socket",
        "ws2_32.dll!closesocket",
        "ws2_32.dll!connect",
        "ws2_32.dll!send",
        "ws2_32.dll!recv",
        "ws2_32.dll!shutdown",
        "ws2_32.dll!getaddrinfo",
        "ws2_32.dll!freeaddrinfo",
        "ws2_32.dll!htons",
        "ws2_32.dll!ntohs",
        "ws2_32.dll!htonl",
        "ws2_32.dll!ntohl",
        "ws2_32.dll!inet_addr",
        "ws2_32.dll!inet_ntoa",
    };
    return tested.find(lower(module) + "!" + lower(name)) != tested.end();
}

}  // namespace

bool isHostExportImplemented(const std::string& module, const std::string& name,
                             uint16_t ordinal, bool byOrdinal) {
    const std::string wanted = lower(module);
    std::lock_guard<std::mutex> lock(hostModulesMutex());
    for (const HostModule& host : hostModules()) {
        if (host.name != wanted) continue;
        if (byOrdinal) {
            if (host.exportsByOrdinal.find(ordinal) != host.exportsByOrdinal.end())
                return true;
        } else if (!name.empty()) {
            if (host.exportsByName.find(name) != host.exportsByName.end())
                return true;
        }
    }
    return false;
}

ImportReport buildImportReport(const uint8_t* file, size_t fileSize) {
    ImportReport report;
    if (file == nullptr || fileSize < 0x40) {
        report.error = "file too small for PE headers";
        return report;
    }
    const View view{file, fileSize};
    uint16_t mz = 0;
    if (!view.read(0, mz) || mz != 0x5a4d) {
        report.error = "not a Windows executable (no MZ header)";
        return report;
    }
    uint32_t peOffset = 0;
    uint32_t signature = 0;
    if (!view.read(0x3c, peOffset) || !view.read(peOffset, signature) ||
        signature != 0x00004550) {
        report.error = "PE signature not found";
        return report;
    }
    uint16_t sectionCount = 0;
    uint16_t optionalSize = 0;
    uint16_t magic = 0;
    const uint64_t optional = static_cast<uint64_t>(peOffset) + 24;
    if (!view.read(peOffset + 6ULL, sectionCount) ||
        !view.read(peOffset + 20ULL, optionalSize) || !view.read(optional, magic)) {
        report.error = "truncated PE header";
        return report;
    }
    if (magic != 0x10b && magic != 0x20b) {
        report.error = "unknown optional header";
        return report;
    }
    const bool pe32Plus = magic == 0x20b;
    if (sectionCount == 0 || sectionCount > kMaxSections) {
        report.error = "invalid section count";
        return report;
    }
    std::vector<Section> sections;
    for (uint32_t i = 0; i < sectionCount; ++i) {
        const uint64_t header = optional + optionalSize + 40ULL * i;
        Section section;
        if (!view.read(header + 12, section.rva) ||
            !view.read(header + 16, section.fileSize) ||
            !view.read(header + 20, section.fileOffset)) {
            report.error = "truncated section table";
            return report;
        }
        sections.push_back(section);
    }
    uint32_t directoryCount = 0;
    if (!view.read(optional + (pe32Plus ? 108 : 92), directoryCount)) {
        report.error = "truncated optional header";
        return report;
    }
    if (directoryCount <= 1) {
        report.error.clear();
        return report;  // no imports
    }
    uint32_t importRva = 0;
    if (!view.read(optional + (pe32Plus ? 112 : 96) + 8, importRva)) {
        report.error = "truncated data directories";
        return report;
    }
    if (importRva == 0) return report;
    uint64_t descriptorOffset = 0;
    if (!rvaToOffset(sections, importRva, 20, descriptorOffset)) {
        report.error = "import directory is outside file-backed data";
        return report;
    }
    for (uint32_t moduleIndex = 0; moduleIndex < kMaxModules; ++moduleIndex) {
        const uint64_t entry = descriptorOffset + 20ULL * moduleIndex;
        uint32_t lookupRva = 0;
        uint32_t nameRva = 0;
        uint32_t addressRva = 0;
        if (!view.read(entry, lookupRva) || !view.read(entry + 12, nameRva) ||
            !view.read(entry + 16, addressRva)) {
            report.error = "truncated import descriptor";
            return report;
        }
        if (nameRva == 0) break;
        uint64_t nameOffset = 0;
        std::string module;
        if (!rvaToOffset(sections, nameRva, 1, nameOffset) ||
            !readName(view, nameOffset, module)) {
            report.error = "unreadable import module name";
            return report;
        }
        const uint32_t thunkRva = lookupRva != 0 ? lookupRva : addressRva;
        uint64_t thunkOffset = 0;
        if (thunkRva == 0 || !rvaToOffset(sections, thunkRva, pe32Plus ? 8 : 4,
                                          thunkOffset)) {
            report.error = "unreadable import thunk table";
            return report;
        }
        for (uint32_t index = 0; index < kMaxFunctions; ++index) {
            uint64_t value = 0;
            if (pe32Plus) {
                if (!view.read(thunkOffset + 8ULL * index, value)) break;
            } else {
                uint32_t narrow = 0;
                if (!view.read(thunkOffset + 4ULL * index, narrow)) break;
                value = narrow;
            }
            if (value == 0) break;
            ImportedFunction function;
            function.module = module;
            if ((value & 0x8000000000000000ULL) != 0) {
                function.byOrdinal = true;
                function.ordinal = static_cast<uint16_t>(value & 0xffff);
            } else {
                const uint32_t byNameRva = static_cast<uint32_t>(value & 0x7fffffffU);
                uint64_t byNameOffset = 0;
                if (!rvaToOffset(sections, byNameRva, 2, byNameOffset) ||
                    !readName(view, byNameOffset + 2, function.name)) {
                    report.error = "unreadable import function name";
                    return report;
                }
            }
            ImportEntry item;
            item.function = function;
            const bool implemented =
                isHostExportImplemented(module, function.name, function.ordinal,
                                        function.byOrdinal);
            if (!implemented) {
                item.status = ImportStatus::Missing;
                report.missing += 1;
            } else if (!function.byOrdinal && isTested(module, function.name)) {
                item.status = ImportStatus::Tested;
                report.tested += 1;
            } else {
                item.status = ImportStatus::Implemented;
                report.implemented += 1;
            }
            report.entries.push_back(std::move(item));
        }
    }
    return report;
}

std::string formatImportReport(const ImportReport& report) {
    if (!report.ok()) return "Import report failed: " + report.error + "\n";
    std::string text = "Import coverage: " +
                       std::to_string(report.entries.size()) + " imports (" +
                       std::to_string(report.tested) + " tested, " +
                       std::to_string(report.implemented) + " implemented, " +
                       std::to_string(report.missing) + " missing)\n";
    text += "Missing imports are mapped to UD2 traps; they never return fake success.\n";
    std::set<std::string> modules;
    for (const ImportEntry& entry : report.entries) modules.insert(entry.function.module);
    for (const std::string& module : modules) {
        size_t total = 0;
        size_t missing = 0;
        for (const ImportEntry& entry : report.entries) {
            if (entry.function.module != module) continue;
            total += 1;
            if (entry.status == ImportStatus::Missing) missing += 1;
        }
        text += "  " + module + ": " + std::to_string(total - missing) + "/" +
                std::to_string(total) + " provided\n";
    }
    if (report.missing != 0) {
        text += "Missing:\n";
        for (const ImportEntry& entry : report.entries) {
            if (entry.status != ImportStatus::Missing) continue;
            text += "  " + entry.function.module + "!";
            if (entry.function.byOrdinal) {
                text += "#" + std::to_string(entry.function.ordinal);
            } else {
                text += entry.function.name;
            }
            text += "\n";
        }
    }
    return text;
}

}  // namespace rottr
