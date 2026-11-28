#include "rottr/pe_loader.h"
#include "rottr/x64_bridge.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>

namespace rottr {
namespace {

constexpr uint32_t kMaxSections = 96;
constexpr uint32_t kMaxThunksPerModule = 65536;
constexpr uint32_t kMaxImportModules = 1024;
constexpr uint32_t kMaxNameLength = 512;
constexpr uint32_t kDirectoryCount = 16;
constexpr uint32_t kRelocDirectory = 5;
constexpr uint32_t kImportDirectory = 1;
constexpr uint32_t kExportDirectory = 0;
constexpr uint32_t kTlsDirectory = 9;
constexpr uint16_t kRelocDir64 = 10;
constexpr size_t kTrapRegionSize = 1U << 20;
constexpr uint32_t kScnMemExecute = 0x20000000U;
constexpr uint32_t kScnMemWrite = 0x80000000U;

struct View {
    const uint8_t* data = nullptr;
    size_t size = 0;

    bool range(uint64_t offset, uint64_t length) const {
        return offset <= size && length <= static_cast<uint64_t>(size) - offset;
    }

    template <typename T>
    bool read(uint64_t offset, T& out) const {
        if (!range(offset, sizeof(T))) return false;
        std::memcpy(&out, data + offset, sizeof(T));
        return true;
    }
};

uint32_t alignUp(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1U) & ~(alignment - 1U);
}

uint32_t protectionFor(uint32_t characteristics) {
    uint32_t protection = ROTTR_MEM_READ;
    if ((characteristics & kScnMemExecute) != 0) protection |= ROTTR_MEM_EXEC;
    if ((characteristics & kScnMemWrite) != 0) protection |= ROTTR_MEM_WRITE;
    return protection;
}

std::string sanitize(const char* bytes, size_t length) {
    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        const unsigned char c = static_cast<unsigned char>(bytes[i]);
        if (c == 0) break;
        result.push_back(c >= 0x20 && c < 0x7f ? static_cast<char>(c) : '?');
    }
    return result;
}

bool readAscii(const PeImage& image, uint64_t address, size_t limit, std::string& out) {
    out.clear();
    for (size_t i = 0; i < limit; ++i) {
        uint8_t c = 0;
        if (!image.read(address + i, &c, sizeof(c))) return false;
        if (c == 0) return true;
        out.push_back(static_cast<char>(c));
    }
    return false;
}

}  // namespace

const char* describePeLoadError(PeLoadError error) {
    switch (error) {
        case PeLoadError::None: return "ok";
        case PeLoadError::InvalidDosHeader: return "no MZ signature";
        case PeLoadError::InvalidPeSignature: return "no PE signature";
        case PeLoadError::TruncatedHeaders: return "truncated PE headers";
        case PeLoadError::UnsupportedOptionalHeader: return "not a PE32+ image";
        case PeLoadError::TooManySections: return "too many sections";
        case PeLoadError::InvalidSectionTable: return "invalid section table";
        case PeLoadError::SectionOutOfRange: return "section outside SizeOfImage";
        case PeLoadError::TruncatedSectionData: return "truncated section data";
        case PeLoadError::ImageOutOfRange: return "image outside guest reservation";
        case PeLoadError::AllocationFailed: return "guest allocation failed";
        case PeLoadError::InvalidRelocationTable: return "invalid relocation table";
        case PeLoadError::UnsupportedRelocation: return "unsupported relocation type";
        case PeLoadError::InvalidImportTable: return "invalid import table";
        case PeLoadError::InvalidExportTable: return "invalid export table";
        case PeLoadError::NotLoaded: return "image not loaded";
    }
    return "unknown loader error";
}

PeImage::~PeImage() { reset(); }

PeImage::PeImage(PeImage&& other) noexcept { *this = std::move(other); }

PeImage& PeImage::operator=(PeImage&& other) noexcept {
    if (this == &other) return *this;
    reset();
    memory_ = other.memory_;
    base_ = other.base_;
    preferredBase_ = other.preferredBase_;
    trapBase_ = other.trapBase_;
    trapBytes_ = other.trapBytes_;
    imageSize_ = other.imageSize_;
    headersSize_ = other.headersSize_;
    entryRva_ = other.entryRva_;
    isDll_ = other.isDll_;
    loaded_ = other.loaded_;
    exportDirRva_ = other.exportDirRva_;
    exportDirSize_ = other.exportDirSize_;
    tlsStart_ = other.tlsStart_;
    tlsEnd_ = other.tlsEnd_;
    tlsIndex_ = other.tlsIndex_;
    tlsCallbacks_ = other.tlsCallbacks_;
    tlsDirectorySize_ = other.tlsDirectorySize_;
    tlsZeroFill_ = other.tlsZeroFill_;
    sections_ = std::move(other.sections_);
    imports_ = std::move(other.imports_);
    missingImports_ = std::move(other.missingImports_);
    exports_ = std::move(other.exports_);
    other.loaded_ = false;
    other.memory_ = nullptr;
    return *this;
}

void PeImage::reset() {
    // Guest memory belongs to the reservation owner, not to the image: pages
    // are released when the whole reservation is released.
    memory_ = nullptr;
    base_ = 0;
    preferredBase_ = 0;
    trapBase_ = 0;
    trapBytes_ = 0;
    imageSize_ = 0;
    headersSize_ = 0;
    entryRva_ = 0;
    isDll_ = false;
    loaded_ = false;
    exportDirRva_ = 0;
    exportDirSize_ = 0;
    tlsStart_ = 0;
    tlsEnd_ = 0;
    tlsIndex_ = 0;
    tlsCallbacks_ = 0;
    tlsDirectorySize_ = 0;
    tlsZeroFill_ = 0;
    sections_.clear();
    imports_.clear();
    missingImports_.clear();
    exports_.clear();
}

bool PeImage::read(uint64_t address, void* dst, size_t size) const {
    if (!loaded_ || memory_ == nullptr) return false;
    return rottr_guest_read(memory_, static_cast<uintptr_t>(address), dst, size) == 0;
}

bool PeImage::write(uint64_t address, const void* src, size_t size) const {
    if (!loaded_ || memory_ == nullptr) return false;
    return rottr_guest_write(memory_, static_cast<uintptr_t>(address), src, size) == 0;
}

bool PeImage::vaToRva(uint64_t va, uint32_t& rva) const {
    if (!loaded_ || va < preferredBase_) return false;
    const uint64_t offset = va - preferredBase_;
    if (offset >= imageSize_) return false;
    rva = static_cast<uint32_t>(offset);
    return true;
}

bool PeImage::findExport(const std::string& name, PeExport& result) const {
    for (const PeExport& candidate : exports_) {
        if (candidate.name == name) {
            result = candidate;
            return true;
        }
    }
    return false;
}

bool PeImage::findExportByOrdinal(uint16_t ordinal, PeExport& result) const {
    for (const PeExport& candidate : exports_) {
        if (candidate.ordinal == ordinal) {
            result = candidate;
            return true;
        }
    }
    return false;
}

std::string PeImage::missingImportAt(uint64_t address) const {
    for (const PeMissingImport& missing : missingImports_) {
        if (missing.trapAddress != 0 && missing.trapAddress == address)
            return missing.description;
    }
    return std::string();
}

std::string PeImage::describe() const {
    if (!loaded_) return "PE image: not loaded";
    char line[256];
    std::snprintf(line, sizeof(line),
                  "PE image at 0x%llx (preferred 0x%llx), size 0x%x, entry 0x%x, %s\n",
                  static_cast<unsigned long long>(base_),
                  static_cast<unsigned long long>(preferredBase_), imageSize_, entryRva_,
                  isDll_ ? "DLL" : "EXE");
    std::string text = line;
    for (const PeSectionInfo& section : sections_) {
        std::snprintf(line, sizeof(line), "  %-8s rva 0x%x size 0x%x prot 0x%x\n",
                      section.name.c_str(), section.rva, section.virtualSize,
                      section.protection);
        text += line;
    }
    std::snprintf(line, sizeof(line), "  imports %zu, missing %zu, exports %zu\n",
                  imports_.size(), missingImports_.size(), exports_.size());
    text += line;
    return text;
}

PeLoadError PeImage::load(rottr_guest_mem* memory, const uint8_t* file, size_t fileSize,
                          const PeLoadOptions& options) {
    reset();
    if (memory == nullptr) return PeLoadError::AllocationFailed;

    const View view{file, fileSize};
    uint16_t dosSignature = 0;
    if (!view.read(0, dosSignature) || dosSignature != 0x5A4D)
        return PeLoadError::InvalidDosHeader;
    uint32_t peOffset = 0;
    uint32_t peSignature = 0;
    if (!view.read(0x3C, peOffset) || !view.read(peOffset, peSignature) ||
        peSignature != 0x00004550U)
        return PeLoadError::InvalidPeSignature;

    uint16_t machine = 0, sectionCount = 0, optionalSize = 0, magic = 0;
    if (!view.read(static_cast<uint64_t>(peOffset) + 4U, machine) ||
        !view.read(static_cast<uint64_t>(peOffset) + 6U, sectionCount) ||
        !view.read(static_cast<uint64_t>(peOffset) + 20U, optionalSize) ||
        !view.read(static_cast<uint64_t>(peOffset) + 24U, magic))
        return PeLoadError::TruncatedHeaders;
    if (magic != 0x20BU || machine != 0x8664U) return PeLoadError::UnsupportedOptionalHeader;
    if (sectionCount == 0 || sectionCount > kMaxSections || optionalSize < 112)
        return PeLoadError::TooManySections;

    const uint64_t optional = static_cast<uint64_t>(peOffset) + 24U;
    const uint64_t sectionTable = optional + optionalSize;
    uint32_t directoryCount = 0;
    if (!view.read(optional + 16U, entryRva_) ||
        !view.read(optional + 24U, preferredBase_) ||
        !view.read(optional + 56U, imageSize_) ||
        !view.read(optional + 108U, directoryCount) ||
        !view.read(optional + 60U, headersSize_))
        return PeLoadError::TruncatedHeaders;
    isDll_ = false;
    uint16_t characteristics = 0;
    if (!view.read(static_cast<uint64_t>(peOffset) + 22U, characteristics))
        return PeLoadError::TruncatedHeaders;
    isDll_ = (characteristics & 0x2000U) != 0;
    directoryCount = std::min(directoryCount, kDirectoryCount);
    if (!view.range(sectionTable, static_cast<uint64_t>(sectionCount) * 40U))
        return PeLoadError::TruncatedHeaders;
    if (imageSize_ == 0 || headersSize_ == 0 || headersSize_ > imageSize_)
        return PeLoadError::InvalidSectionTable;
    if (headersSize_ > fileSize) return PeLoadError::TruncatedHeaders;

    const uint64_t fileAlignment = [&view, optional]() -> uint64_t {
        uint32_t value = 0;
        return view.read(optional + 36U, value) && value != 0 ? value : 512U;
    }();

    // Validate the whole section table before touching guest memory.
    struct RawSection {
        PeSectionInfo info;
        uint32_t copySize = 0;
    };
    std::vector<RawSection> parsed;
    parsed.reserve(sectionCount);
    for (uint32_t i = 0; i < sectionCount; ++i) {
        const uint64_t header = sectionTable + static_cast<uint64_t>(i) * 40U;
        char name[8] = {};
        RawSection section;
        if (!view.read(header, name) ||
            !view.read(header + 8U, section.info.virtualSize) ||
            !view.read(header + 12U, section.info.rva) ||
            !view.read(header + 16U, section.info.rawSize) ||
            !view.read(header + 20U, section.info.rawOffset) ||
            !view.read(header + 36U, section.info.characteristics))
            return PeLoadError::TruncatedHeaders;
        section.info.name = sanitize(name, sizeof(name));
        const uint32_t mappedSize = section.info.virtualSize != 0
                                        ? section.info.virtualSize
                                        : section.info.rawSize;
        if (section.info.rva < headersSize_ ||
            static_cast<uint64_t>(section.info.rva) + mappedSize > imageSize_)
            return PeLoadError::SectionOutOfRange;
        if (section.info.rawSize != 0) {
            if (!view.range(section.info.rawOffset, section.info.rawSize))
                return PeLoadError::TruncatedSectionData;
            section.copySize = std::min(section.info.rawSize, mappedSize);
            if (section.copySize % fileAlignment != 0 &&
                section.copySize < fileAlignment) {
                // A short final section is legal; only the file bounds matter.
            }
        }
        section.info.protection = protectionFor(section.info.characteristics);
        parsed.push_back(section);
    }

    const uint32_t commitSize = alignUp(imageSize_, ROTTR_GUEST_PAGE_SIZE);
    void* region = nullptr;
    if (options.loadBase == 0) {
        region = rottr_guest_reserve_region(memory, nullptr, commitSize);
    } else {
        if ((options.loadBase & (ROTTR_ALLOC_GRANULARITY - 1U)) != 0)
            return PeLoadError::ImageOutOfRange;
        rottr_mem_result bounds{};
        if (rottr_guest_translate(memory, static_cast<uintptr_t>(options.loadBase),
                                  commitSize, &bounds) != 1)
            return PeLoadError::ImageOutOfRange;
        region = rottr_guest_reserve_region(memory,
                                            reinterpret_cast<void*>(options.loadBase),
                                            commitSize);
    }
    if (region == nullptr) return PeLoadError::AllocationFailed;
    base_ = reinterpret_cast<uintptr_t>(region);
    memory_ = memory;

    if (rottr_guest_commit_pages(memory, static_cast<uintptr_t>(base_), commitSize) != 0)
        return PeLoadError::AllocationFailed;

    // Headers and section payloads. Pages beyond a section's raw data remain
    // zero because rottr_guest_commit_pages zero-fills the whole image.
    if (rottr_guest_write(memory, static_cast<uintptr_t>(base_), file, headersSize_) != 0)
        return PeLoadError::AllocationFailed;
    for (const RawSection& section : parsed) {
        if (section.copySize == 0) continue;
        if (rottr_guest_write(memory, static_cast<uintptr_t>(base_ + section.info.rva),
                              file + section.info.rawOffset, section.copySize) != 0)
            return PeLoadError::AllocationFailed;
    }
    sections_.reserve(parsed.size());
    for (const RawSection& section : parsed) sections_.push_back(section.info);
    loaded_ = true;

    // Data directories are read from the file header (the same bytes that are
    // now mapped at the image base).
    uint32_t directories[16] = {};
    uint32_t directoryRvas[16] = {};
    for (uint32_t i = 0; i < directoryCount; ++i) {
        uint32_t rva = 0, size = 0;
        if (view.read(optional + 112U + static_cast<uint64_t>(i) * 8U, rva) &&
            view.read(optional + 116U + static_cast<uint64_t>(i) * 8U, size)) {
            directoryRvas[i] = rva;
            directories[i] = size;
        }
    }
    exportDirRva_ = directoryRvas[kExportDirectory];
    exportDirSize_ = directories[kExportDirectory];

    if (directoryRvas[kTlsDirectory] != 0 && directories[kTlsDirectory] >= 40) {
        uint64_t tlsAddress = base_ + directoryRvas[kTlsDirectory];
        read(tlsAddress, &tlsStart_, sizeof(tlsStart_));
        read(tlsAddress + 8U, &tlsEnd_, sizeof(tlsEnd_));
        read(tlsAddress + 16U, &tlsIndex_, sizeof(tlsIndex_));
        read(tlsAddress + 24U, &tlsCallbacks_, sizeof(tlsCallbacks_));
        read(tlsAddress + 32U, &tlsZeroFill_, sizeof(tlsZeroFill_));
        tlsDirectorySize_ = directories[kTlsDirectory];
    }

    // Step 10: DIR64 relocations. Delta is computed from the actual load base;
    // a zero delta is a no-op, but the table is still validated.
    if (options.applyRelocations) {
        const uint32_t relocRva = directoryRvas[kRelocDirectory];
        const uint32_t relocSize = directories[kRelocDirectory];
        if (relocRva != 0 && relocSize != 0) {
            if (static_cast<uint64_t>(relocRva) + relocSize > imageSize_)
                return PeLoadError::InvalidRelocationTable;
            std::vector<uint8_t> table(relocSize);
            if (!read(base_ + relocRva, table.data(), table.size()))
                return PeLoadError::InvalidRelocationTable;
            const int64_t delta = static_cast<int64_t>(base_) -
                                  static_cast<int64_t>(preferredBase_);
            size_t cursor = 0;
            while (cursor < table.size()) {
                if (table.size() - cursor < 8) return PeLoadError::InvalidRelocationTable;
                uint32_t pageRva = 0, blockSize = 0;
                std::memcpy(&pageRva, table.data() + cursor, sizeof(pageRva));
                std::memcpy(&blockSize, table.data() + cursor + 4U, sizeof(blockSize));
                if (blockSize < 8 || (blockSize & 1U) != 0 ||
                    blockSize > table.size() - cursor)
                    return PeLoadError::InvalidRelocationTable;
                for (size_t entry = cursor + 8; entry < cursor + blockSize; entry += 2) {
                    uint16_t value = 0;
                    std::memcpy(&value, table.data() + entry, sizeof(value));
                    const uint32_t type = value >> 12U;
                    const uint32_t offset = value & 0xfffU;
                    if (type == 0) continue;  // IMAGE_REL_BASED_ABSOLUTE padding
                    if (type != kRelocDir64) return PeLoadError::UnsupportedRelocation;
                    const uint64_t targetRva = static_cast<uint64_t>(pageRva) + offset;
                    if (targetRva + 8U > imageSize_)
                        return PeLoadError::InvalidRelocationTable;
                    uint64_t pointer = 0;
                    if (!read(base_ + targetRva, &pointer, sizeof(pointer)))
                        return PeLoadError::InvalidRelocationTable;
                    pointer = static_cast<uint64_t>(static_cast<int64_t>(pointer) + delta);
                    if (!write(base_ + targetRva, &pointer, sizeof(pointer)))
                        return PeLoadError::InvalidRelocationTable;
                }
                cursor += blockSize;
            }
        }
    }

    // Step 13: exports are parsed before imports so a DLL can satisfy another
    // DLL's imports while that DLL is still initializing (dependency cycles).
    if (exportDirRva_ != 0 && exportDirSize_ >= 40) {
        if (static_cast<uint64_t>(exportDirRva_) + exportDirSize_ > imageSize_)
            return PeLoadError::InvalidExportTable;
        uint32_t ordinalBase = 0, functionCount = 0, nameCount = 0;
        uint32_t functionsRva = 0, namesRva = 0, ordinalsRva = 0;
        const uint64_t dir = base_ + exportDirRva_;
        if (!read(dir + 16U, &ordinalBase, sizeof(ordinalBase)) ||
            !read(dir + 20U, &functionCount, sizeof(functionCount)) ||
            !read(dir + 24U, &nameCount, sizeof(nameCount)) ||
            !read(dir + 28U, &functionsRva, sizeof(functionsRva)) ||
            !read(dir + 32U, &namesRva, sizeof(namesRva)) ||
            !read(dir + 36U, &ordinalsRva, sizeof(ordinalsRva)))
            return PeLoadError::InvalidExportTable;
        if (functionCount > kMaxThunksPerModule || nameCount > kMaxThunksPerModule)
            return PeLoadError::InvalidExportTable;
        std::vector<uint16_t> nameOrdinals(nameCount);
        std::vector<std::string> names(nameCount);
        for (uint32_t i = 0; i < nameCount; ++i) {
            uint32_t nameRva = 0;
            uint16_t ordinalIndex = 0;
            if (!read(base_ + static_cast<uint64_t>(namesRva) + i * 4ULL, &nameRva,
                      sizeof(nameRva)) ||
                !read(base_ + static_cast<uint64_t>(ordinalsRva) + i * 2ULL,
                      &ordinalIndex, sizeof(ordinalIndex)) ||
                !readAscii(*this, base_ + nameRva, kMaxNameLength, names[i]))
                return PeLoadError::InvalidExportTable;
            nameOrdinals[i] = ordinalIndex;
        }
        for (uint32_t i = 0; i < functionCount; ++i) {
            uint32_t functionRva = 0;
            if (!read(base_ + static_cast<uint64_t>(functionsRva) + i * 4ULL,
                      &functionRva, sizeof(functionRva)))
                return PeLoadError::InvalidExportTable;
            if (functionRva == 0) continue;
            PeExport exported;
            exported.ordinal = static_cast<uint16_t>(ordinalBase + i);
            if (functionRva >= exportDirRva_ &&
                functionRva - exportDirRva_ < exportDirSize_) {
                if (!readAscii(*this, base_ + functionRva, kMaxNameLength,
                               exported.forwarder))
                    return PeLoadError::InvalidExportTable;
            } else {
                exported.address = base_ + functionRva;
            }
            for (uint32_t n = 0; n < nameCount; ++n) {
                if (nameOrdinals[n] == i) {
                    exported.name = names[n];
                    break;
                }
            }
            exports_.push_back(exported);
        }
    }

    // Step 12: import descriptors, by name and by ordinal. Unresolved imports
    // get an executable UD2 trap thunk instead of a fake success address.
    if (options.resolveImports && directoryRvas[kImportDirectory] != 0) {
        const uint64_t importBase = base_ + directoryRvas[kImportDirectory];
        for (uint32_t index = 0; index < kMaxImportModules; ++index) {
            uint32_t lookupRva = 0, nameRva = 0, firstThunkRva = 0;
            uint32_t descriptor[5] = {};
            if (!read(importBase + static_cast<uint64_t>(index) * 20U, descriptor,
                      sizeof(descriptor)))
                return PeLoadError::InvalidImportTable;
            if (descriptor[0] == 0 && descriptor[1] == 0 && descriptor[2] == 0 &&
                descriptor[3] == 0 && descriptor[4] == 0)
                break;
            lookupRva = descriptor[0];
            nameRva = descriptor[3];
            firstThunkRva = descriptor[4];
            (void)lookupRva;
            if (nameRva == 0 || firstThunkRva == 0 ||
                nameRva >= imageSize_ || firstThunkRva + 8U > imageSize_)
                return PeLoadError::InvalidImportTable;
            std::string module;
            if (!readAscii(*this, base_ + nameRva, kMaxNameLength, module) ||
                module.empty())
                return PeLoadError::InvalidImportTable;
            const uint32_t thunkRva = lookupRva != 0 ? lookupRva : firstThunkRva;
            for (uint32_t ordinalIndex = 0; ordinalIndex < kMaxThunksPerModule;
                 ++ordinalIndex) {
                const uint64_t thunkOffset =
                        static_cast<uint64_t>(thunkRva) + ordinalIndex * 8ULL;
                if (thunkOffset + 8U > imageSize_) return PeLoadError::InvalidImportTable;
                uint64_t thunk = 0;
                if (!read(base_ + thunkOffset, &thunk, sizeof(thunk)))
                    return PeLoadError::InvalidImportTable;
                if (thunk == 0) break;
                PeImportRequest request;
                request.module = module;
                request.iatRva = static_cast<uint64_t>(firstThunkRva) + ordinalIndex * 8ULL;
                if ((thunk & (1ULL << 63U)) != 0) {
                    request.byOrdinal = true;
                    request.ordinal = static_cast<uint16_t>(thunk & 0xffffU);
                } else {
                    if (thunk >= imageSize_) return PeLoadError::InvalidImportTable;
                    uint16_t hint = 0;
                    if (!read(base_ + thunk, &hint, sizeof(hint)) ||
                        !readAscii(*this, base_ + thunk + 2U, kMaxNameLength,
                                   request.function) ||
                        request.function.empty())
                        return PeLoadError::InvalidImportTable;
                    (void)hint;
                }
                imports_.push_back(request);
                uint64_t resolved = 0;
                const bool ok = options.importResolver &&
                                options.importResolver(request, resolved);
                if (ok && resolved != 0) {
                    if (!write(base_ + request.iatRva, &resolved, sizeof(resolved)))
                        return PeLoadError::AllocationFailed;
                    continue;
                }

                if (trapBase_ == 0) {
                    void* trap = rottr_guest_reserve_region(memory, nullptr,
                                                            kTrapRegionSize);
                    if (trap == nullptr) return PeLoadError::AllocationFailed;
                    trapBase_ = reinterpret_cast<uintptr_t>(trap);
                    if (rottr_guest_commit_pages(memory, static_cast<uintptr_t>(trapBase_),
                                                 ROTTR_GUEST_PAGE_SIZE) != 0)
                        return PeLoadError::AllocationFailed;
                }
                if (trapBytes_ + 16U > kTrapRegionSize)
                    return PeLoadError::AllocationFailed;
                if (trapBytes_ != 0 && (trapBytes_ % ROTTR_GUEST_PAGE_SIZE) == 0 &&
                    rottr_guest_commit_pages(
                            memory,
                            static_cast<uintptr_t>(trapBase_ + trapBytes_),
                            ROTTR_GUEST_PAGE_SIZE) != 0)
                    return PeLoadError::AllocationFailed;
                const uint64_t trapAddress = trapBase_ + trapBytes_;
                const uint8_t ud2[16] = {0x0f, 0x0b};
                if (!write(trapAddress, ud2, sizeof(ud2)))
                    return PeLoadError::AllocationFailed;
                trapBytes_ += sizeof(ud2);
                PeMissingImport missing;
                missing.trapAddress = trapAddress;
                missing.iatRva = request.iatRva;
                missing.description = request.module + "!" +
                        (request.byOrdinal ? "#" + std::to_string(request.ordinal)
                                           : request.function);
                missingImports_.push_back(missing);
                if (!write(base_ + request.iatRva, &trapAddress, sizeof(trapAddress)))
                    return PeLoadError::AllocationFailed;
            }
        }
    }

    // Step 11: final section protections, merged per 4 KB page. Applying them
    // after relocations and IAT writes keeps code writable during loading only.
    if (options.applyProtections) {
        const size_t pageCount = commitSize / ROTTR_GUEST_PAGE_SIZE;
        std::vector<uint32_t> pageProtection(pageCount, ROTTR_MEM_READ);
        for (const PeSectionInfo& section : sections_) {
            const uint32_t mappedSize = std::max(section.virtualSize, section.rawSize);
            if (mappedSize == 0) continue;
            const size_t first = section.rva / ROTTR_GUEST_PAGE_SIZE;
            const size_t last = (section.rva + mappedSize - 1U) / ROTTR_GUEST_PAGE_SIZE;
            for (size_t page = first; page <= last && page < pageCount; ++page)
                pageProtection[page] |= section.protection;
        }
        size_t page = 0;
        while (page < pageCount) {
            const uint32_t protection = pageProtection[page];
            size_t end = page + 1;
            while (end < pageCount && pageProtection[end] == protection) ++end;
            const uint64_t address = base_ + static_cast<uint64_t>(page) * ROTTR_GUEST_PAGE_SIZE;
            if (rottr_guest_protect_pages(
                        memory, static_cast<uintptr_t>(address),
                        (end - page) * ROTTR_GUEST_PAGE_SIZE, protection) != 0)
                return PeLoadError::AllocationFailed;
            page = end;
        }
        if (trapBase_ != 0) {
            const size_t trapPages =
                    (trapBytes_ + ROTTR_GUEST_PAGE_SIZE - 1U) / ROTTR_GUEST_PAGE_SIZE;
            for (size_t page = 0; page < trapPages; ++page) {
                rottr_guest_protect_pages(
                        memory,
                        static_cast<uintptr_t>(trapBase_ +
                                               page * ROTTR_GUEST_PAGE_SIZE),
                        ROTTR_GUEST_PAGE_SIZE, ROTTR_MEM_READ | ROTTR_MEM_EXEC);
            }
        }
    }

    return PeLoadError::None;
}

std::string PeModuleRegistry::normalize(const std::string& name) {
    std::string result;
    for (char c : name) {
        if (c == '\\' || c == '/') {
            result.clear();
            continue;
        }
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return result;
}

PeModuleRegistry::Module* PeModuleRegistry::findModuleEntry(const std::string& name) {
    const std::string key = normalize(name);
    for (Module& module : modules_) {
        if (module.name == key) return &module;
    }
    return nullptr;
}

const PeModuleRegistry::Module* PeModuleRegistry::findModuleEntry(
        const std::string& name) const {
    const std::string key = normalize(name);
    for (const Module& module : modules_) {
        if (module.name == key) return &module;
    }
    return nullptr;
}

PeImage* PeModuleRegistry::findModule(const std::string& name) const {
    const Module* module = findModuleEntry(name);
    return module == nullptr ? nullptr : module->image.get();
}

PeImage* PeModuleRegistry::addImage(std::unique_ptr<PeImage> image,
                                    const std::string& name) {
    if (!image || !image->loaded()) return nullptr;
    Module module;
    module.name = normalize(name);
    module.image = std::move(image);
    module.refs = 1;
    PeImage* result = module.image.get();
    modules_.push_back(std::move(module));
    return result;
}

bool PeModuleRegistry::resolveExport(PeImage* image, const std::string& name,
                                     uint16_t ordinal, bool byOrdinal,
                                     uint64_t& address, int depth) {
    if (image == nullptr || depth > 8) return false;
    PeExport exported;
    const bool found = byOrdinal ? image->findExportByOrdinal(ordinal, exported)
                                 : image->findExport(name, exported);
    if (!found) return false;
    if (exported.forwarder.empty()) {
        address = exported.address;
        return address != 0;
    }
    const size_t separator = exported.forwarder.find('.');
    if (separator == std::string::npos || separator == 0) return false;
    PeImportRequest forwarded;
    forwarded.module = exported.forwarder.substr(0, separator) + ".dll";
    const std::string symbol = exported.forwarder.substr(separator + 1U);
    if (!symbol.empty() && symbol[0] == '#') {
        forwarded.byOrdinal = true;
        forwarded.ordinal = static_cast<uint16_t>(std::stoul(symbol.substr(1U)));
    } else {
        forwarded.function = symbol;
    }
    return resolveImport(forwarded, address);
}

bool PeModuleRegistry::resolveImport(const PeImportRequest& request,
                                     uint64_t& address) {
    // Check host-registered modules first (Windows API implementations).
    static bool hostModulesRegistered = false;
    if (!hostModulesRegistered) {
        registerHostModules();
        hostModulesRegistered = true;
    }
    if (uint64_t hostAddr = resolveHostExport(request.module, request.function,
                                              request.ordinal, request.byOrdinal)) {
        address = hostAddr;
        return true;
    }

    Module* module = findModuleEntry(request.module);
    if (module == nullptr) {
        std::string error;
        loadModule(request.module, &error);
        module = findModuleEntry(request.module);
    } else {
        // Module already loaded: count this new dependent.
        ++module->refs;
    }
    if (module == nullptr || module->image == nullptr || module->failed) return false;
    if (!resolveExport(module->image.get(), request.function, request.ordinal,
                       request.byOrdinal, address, 0))
        return false;
    return true;
}

PeImage* PeModuleRegistry::loadModule(const std::string& name, std::string* error,
                                      const PeLoadOptions& extraOptions) {
    const std::string key = normalize(name);
    if (Module* existing = findModuleEntry(key)) {
        if (existing->failed) {
            if (error) *error = "module failed to load: " + name;
            return nullptr;
        }
        ++existing->refs;
        return existing->image.get();
    }
    if (!opener_) {
        if (error) *error = "no file opener for " + name;
        return nullptr;
    }
    std::vector<uint8_t> bytes;
    if (!opener_(key, bytes) || bytes.empty()) {
        if (error) *error = "module not found: " + name;
        return nullptr;
    }

    // Insert the placeholder before mapping: exports become available as soon
    // as sections are copied, so a dependency cycle can resolve the module
    // that is still loading instead of recursing forever.
    Module module;
    module.name = key;
    module.image.reset(new PeImage());
    module.refs = 1;
    module.loading = true;
    modules_.push_back(std::move(module));
    const size_t myIndex = modules_.size() - 1;
    PeImage* result = modules_[myIndex].image.get();

    PeLoadOptions options = extraOptions;
    options.importResolver = [this](const PeImportRequest& request, uint64_t& address) {
        return resolveImport(request, address);
    };
    const PeLoadError status = result->load(memory_, bytes.data(), bytes.size(), options);
    modules_[myIndex].loading = false;
    if (status != PeLoadError::None) {
        modules_[myIndex].failed = true;
        if (error) *error = std::string(describePeLoadError(status)) + ": " + name;
        return nullptr;
    }
    // Record dependencies from this module's imports (for initialization
    // order: dependencies must initialize before dependents). Use myIndex
    // (not modules_.back()) because recursive loadModule calls may have
    // appended new modules after this one.
    for (const auto& imp : result->imports()) {
        const std::string& depName = imp.module;
        if (std::find(modules_[myIndex].dependencies.begin(),
                      modules_[myIndex].dependencies.end(),
                      depName) == modules_[myIndex].dependencies.end()) {
            modules_[myIndex].dependencies.push_back(depName);
        }
    }
    return result;
}

std::vector<PeImage*> PeModuleRegistry::initializationOrder() const {
    std::vector<PeImage*> order;
    std::vector<uint8_t> visited(modules_.size(), 0);
    std::vector<uint8_t> active(modules_.size(), 0);
    std::function<void(size_t)> visit = [&](size_t index) {
        if (visited[index] != 0 || active[index] != 0) return;
        active[index] = 1;
        for (const std::string& dependency : modules_[index].dependencies) {
            for (size_t i = 0; i < modules_.size(); ++i) {
                if (modules_[i].name == dependency) {
                    visit(i);
                    break;
                }
            }
        }
        active[index] = 0;
        visited[index] = 1;
        if (modules_[index].image != nullptr) order.push_back(modules_[index].image.get());
    };
    for (size_t i = 0; i < modules_.size(); ++i) visit(i);
    return order;
}

bool PeModuleRegistry::callEntryPoints(rottr_cpu& cpu,
                                      const X64GuestMemory& memory) {
    constexpr uint32_t kDllProcessAttach = 1U;
    for (PeImage* image : initializationOrder()) {
        if (image == nullptr || !image->isDll()) continue;
        uint64_t result = 0;
        if (!image->callDllMain(cpu, memory, kDllProcessAttach, result) ||
            result == 0)
            return false;
    }
    return true;
}

std::vector<uint64_t> PeImage::tlsCallbackAddresses() const {
    std::vector<uint64_t> result;
    if (!hasTls() || tlsCallbacks_ == 0) return result;
    // Read the null-terminated array of 8-byte callback addresses (each an RVA).
    for (uint64_t off = 0;; off += 8ULL) {
        uint64_t addr = 0;
        if (!read(base_ + tlsCallbacks_ + off, &addr, sizeof(addr))) break;
        if (addr == 0) break;
        result.push_back(base_ + addr);  // Convert RVA to guest VA
    }
    return result;
}

bool PeImage::runTlsCallbacks(rottr_cpu& cpu,
                              const X64GuestMemory& guest) const {
    const std::vector<uint64_t> cbs = tlsCallbackAddresses();
    if (cbs.empty()) return true;
    const uint64_t kDllHandle = base_;
    const uint32_t kProcessAttach = 1u;
    for (uint64_t cb : cbs) {
        uint64_t result = 0;
        if (!rottr::callGuestProc(rottr::registerGuestCallback(cb), cpu, guest,
                                  kDllHandle, kProcessAttach, 0, 0, result)) {
            return false;
        }
    }
    return true;
}

bool PeImage::callDllMain(rottr_cpu& cpu, const X64GuestMemory& guest,
                          uint32_t reason, uint64_t& outResult) const {
    if (!loaded() || !isDll()) return false;
    uint64_t result = 0;
    if (!rottr::callGuestProc(rottr::registerGuestCallback(entryPoint()), cpu,
                              guest, base_, reason, 0, 0, result)) {
        return false;
    }
    outResult = result;
    return true;
}

int PeModuleRegistry::refCount(const std::string& name) const {
    const Module* module = findModuleEntry(name);
    return module == nullptr ? 0 : module->refs;
}

std::string PeModuleRegistry::describe() const {
    std::string text = "Guest modules: " + std::to_string(modules_.size()) + "\n";
    for (const Module& module : modules_) {
        text += "  " + module.name + " refs=" + std::to_string(module.refs) +
                " imports=" + std::to_string(module.image->imports().size()) + "\n";
    }
    return text;
}

}  // namespace rottr
