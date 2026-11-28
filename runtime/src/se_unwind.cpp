#include "rottr/se_unwind.h"

#include <algorithm>
#include <cstring>

namespace rottr {
namespace {

constexpr uint32_t kMaxScopeRecords = 4096;

uint32_t alignUp(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1U) & ~(alignment - 1U);
}

void setError(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
}

}  // namespace

bool PeUnwindTable::rvaToOffset(uint32_t rva, uint32_t size, uint32_t& offset) const {
    for (const Section& section : sections_) {
        if (rva < section.rva) continue;
        const uint64_t delta = static_cast<uint64_t>(rva) - section.rva;
        if (delta + size > section.rawSize) continue;
        const uint64_t fileOffset = static_cast<uint64_t>(section.rawOffset) + delta;
        if (fileOffset + size > fileSize_) continue;
        offset = static_cast<uint32_t>(fileOffset);
        return true;
    }
    return false;
}

bool PeUnwindTable::readAt(uint32_t rva, void* dst, uint32_t size) const {
    uint32_t offset = 0;
    if (!rvaToOffset(rva, size, offset)) return false;
    std::memcpy(dst, file_ + offset, size);
    return true;
}

bool PeUnwindTable::parse(const uint8_t* file, size_t fileSize, uint64_t imageBase,
                          uint32_t pdataRva, uint32_t pdataSize, std::string* error) {
    file_ = file;
    fileSize_ = fileSize;
    imageBase_ = imageBase;
    valid_ = false;
    sections_.clear();
    functions_.clear();

    if (file == nullptr || fileSize < 0x40) {
        setError(error, "file too small for PE headers");
        return false;
    }
    uint32_t peOffset = 0;
    std::memcpy(&peOffset, file + 0x3c, sizeof(peOffset));
    if (static_cast<uint64_t>(peOffset) + 24 > fileSize) {
        setError(error, "truncated PE header");
        return false;
    }
    if (std::memcmp(file + peOffset, "PE\0\0", 4) != 0) {
        setError(error, "missing PE signature");
        return false;
    }
    uint16_t sectionCount = 0;
    uint16_t optionalSize = 0;
    std::memcpy(&sectionCount, file + peOffset + 6, sizeof(sectionCount));
    std::memcpy(&optionalSize, file + peOffset + 20, sizeof(optionalSize));
    if (sectionCount == 0 || sectionCount > 96) {
        setError(error, "invalid section count");
        return false;
    }
    const uint64_t sectionTable = static_cast<uint64_t>(peOffset) + 24 + optionalSize;
    if (sectionTable + static_cast<uint64_t>(sectionCount) * 40 > fileSize) {
        setError(error, "truncated section table");
        return false;
    }
    for (uint16_t i = 0; i < sectionCount; ++i) {
        const uint8_t* entry = file + sectionTable + static_cast<uint64_t>(i) * 40;
        Section section{};
        std::memcpy(&section.virtualSize, entry + 8, 4);
        std::memcpy(&section.rva, entry + 12, 4);
        std::memcpy(&section.rawSize, entry + 16, 4);
        std::memcpy(&section.rawOffset, entry + 20, 4);
        sections_.push_back(section);
    }

    if (pdataRva == 0 || pdataSize == 0) {
        valid_ = true;  // image simply has no exception directory
        return true;
    }
    if ((pdataSize % 12U) != 0) {
        setError(error, "exception directory size is not a multiple of 12");
        return false;
    }
    const uint32_t count = pdataSize / 12U;
    for (uint32_t i = 0; i < count; ++i) {
        SeRuntimeFunction function{};
        if (!readAt(pdataRva + i * 12U, &function, sizeof(function))) {
            setError(error, "exception directory is outside file-backed data");
            return false;
        }
        if (function.beginRva >= function.endRva) {
            setError(error, "runtime function has an empty or inverted range");
            return false;
        }
        functions_.push_back(function);
    }
    std::sort(functions_.begin(), functions_.end(),
              [](const SeRuntimeFunction& a, const SeRuntimeFunction& b) {
                  return a.beginRva < b.beginRva;
              });
    valid_ = true;
    return true;
}

bool PeUnwindTable::parseFromImage(const uint8_t* file, size_t fileSize,
                                   uint64_t imageBase, std::string* error) {
    if (file == nullptr || fileSize < 0x98) {
        setError(error, "file too small for optional header");
        return false;
    }
    uint32_t peOffset = 0;
    std::memcpy(&peOffset, file + 0x3c, sizeof(peOffset));
    if (static_cast<uint64_t>(peOffset) + 24 > fileSize) {
        setError(error, "truncated PE header");
        return false;
    }
    const uint64_t optional = static_cast<uint64_t>(peOffset) + 24;
    if (optional + 112 + 4 * 8 > fileSize) {
        setError(error, "truncated data directories");
        return false;
    }
    uint32_t pdataRva = 0;
    uint32_t pdataSize = 0;
    std::memcpy(&pdataRva, file + optional + 112 + 3 * 8, 4);
    std::memcpy(&pdataSize, file + optional + 112 + 3 * 8 + 4, 4);
    return parse(file, fileSize, imageBase, pdataRva, pdataSize, error);
}

bool PeUnwindTable::lookup(uint64_t pc, SeRuntimeFunction& out) const {
    if (!valid_) return false;
    const uint64_t rva = pc - imageBase_;
    if (pc < imageBase_ || rva > 0xffffffffULL) return false;
    auto it = std::upper_bound(
        functions_.begin(), functions_.end(), static_cast<uint32_t>(rva),
        [](uint32_t value, const SeRuntimeFunction& function) {
            return value < function.beginRva;
        });
    if (it == functions_.begin()) return false;
    --it;
    if (rva < it->beginRva || rva >= it->endRva) return false;
    out = *it;
    return true;
}

bool PeUnwindTable::decode(const SeRuntimeFunction& function, SeUnwindInfo& out,
                           std::string* error) const {
    out = SeUnwindInfo{};
    if (!valid_) {
        setError(error, "unwind table is not valid");
        return false;
    }
    uint8_t header[4] = {};
    if (!readAt(function.unwindInfoRva, header, sizeof(header))) {
        setError(error, "unwind info is outside file-backed data");
        return false;
    }
    out.version = header[0] & 0x7U;
    out.flags = static_cast<uint8_t>(header[0] >> 3);
    out.prologSize = header[1];
    out.countOfCodes = header[2];
    out.frameRegister = header[3] & 0xfU;
    out.frameOffset = static_cast<uint8_t>(header[3] >> 4);
    if (out.version != 1 && out.version != 2) {
        setError(error, "unsupported unwind info version");
        return false;
    }
    std::vector<uint8_t> slots(static_cast<size_t>(out.countOfCodes) * 2U);
    if (!slots.empty() &&
        !readAt(function.unwindInfoRva + 4U, slots.data(),
                static_cast<uint32_t>(slots.size()))) {
        setError(error, "truncated unwind code array");
        return false;
    }
    const unsigned count = out.countOfCodes;
    unsigned index = 0;
    while (index < count) {
        SeUnwindCode code{};
        code.codeOffset = slots[index * 2U];
        const uint8_t packed = slots[index * 2U + 1U];
        code.operation = packed & 0xfU;
        code.operationInfo = static_cast<uint8_t>(packed >> 4);
        unsigned extra = 0;
        switch (code.operation) {
            case kUnwAllocLarge:
                extra = code.operationInfo == 0 ? 1U : 2U;
                break;
            case kUnwSaveNonVol:
            case kUnwSaveXmm128:
                extra = 1;
                break;
            case kUnwSaveNonVolFar:
            case kUnwSaveXmm128Far:
                extra = 2;
                break;
            default:
                extra = 0;
                break;
        }
        if (index + extra >= count) {
            setError(error, "unwind code immediate is truncated");
            return false;
        }
        const uint8_t* immediate = slots.data() + (index + 1U) * 2U;
        switch (code.operation) {
            case kUnwAllocSmall:
                code.frameOffset =
                    static_cast<uint32_t>(code.operationInfo + 1U) * 8U;
                break;
            case kUnwAllocLarge: {
                uint32_t value = 0;
                if (code.operationInfo == 0) {
                    uint16_t small = 0;
                    std::memcpy(&small, immediate, sizeof(small));
                    value = static_cast<uint32_t>(small) * 8U;
                } else {
                    std::memcpy(&value, immediate, sizeof(value));
                }
                code.frameOffset = value;
                break;
            }
            case kUnwSaveNonVol: {
                uint16_t small = 0;
                std::memcpy(&small, immediate, sizeof(small));
                code.frameOffset = static_cast<uint32_t>(small) * 8U;
                break;
            }
            case kUnwSaveNonVolFar:
            case kUnwSaveXmm128Far: {
                uint32_t value = 0;
                std::memcpy(&value, immediate, sizeof(value));
                code.frameOffset = value;
                break;
            }
            case kUnwSaveXmm128: {
                uint16_t small = 0;
                std::memcpy(&small, immediate, sizeof(small));
                code.frameOffset = static_cast<uint32_t>(small) * 16U;
                break;
            }
            default:
                break;
        }
        out.codes.push_back(code);
        index += 1U + extra;
    }

    const uint32_t tail = function.unwindInfoRva + alignUp(4U + out.countOfCodes * 2U, 4U);
    if ((out.flags & kUnwFlagChainInfo) != 0) {
        out.hasChainInfo = true;
        if (!readAt(tail, &out.chained, sizeof(out.chained))) {
            setError(error, "truncated chained runtime function");
            return false;
        }
    } else if ((out.flags & (kUnwFlagExceptionHandler | kUnwFlagTerminationHandler)) != 0) {
        out.hasExceptionHandler = true;
        if (!readAt(tail, &out.exceptionHandlerRva, sizeof(out.exceptionHandlerRva))) {
            setError(error, "truncated exception handler RVA");
            return false;
        }
        out.handlerDataRva = tail + 4U;
    }
    return true;
}

bool PeUnwindTable::scopeTable(const SeUnwindInfo& info,
                               std::vector<SeScopeRecord>& out,
                               std::string* error) const {
    out.clear();
    if (!info.hasExceptionHandler) {
        setError(error, "function has no exception handler data");
        return false;
    }
    uint32_t count = 0;
    if (!readAt(info.handlerDataRva, &count, sizeof(count))) {
        setError(error, "handler data is outside file-backed data");
        return false;
    }
    if (count > kMaxScopeRecords) {
        setError(error, "scope table has an implausible record count");
        return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
        SeScopeRecord record{};
        const uint32_t address = info.handlerDataRva + 4U + i * 16U;
        if (!readAt(address, &record, sizeof(record))) {
            setError(error, "truncated scope table");
            return false;
        }
        if (record.beginAddress >= record.endAddress) {
            setError(error, "scope record has an empty or inverted range");
            return false;
        }
        out.push_back(record);
    }
    return true;
}

bool PeUnwindTable::handlersAt(uint64_t pc, std::vector<SeScopeRecord>& out,
                               std::string* error) const {
    out.clear();
    SeRuntimeFunction function{};
    if (!lookup(pc, function)) {
        setError(error, "no runtime function covers the control PC");
        return false;
    }
    SeUnwindInfo info{};
    if (!decode(function, info, error)) return false;
    std::vector<SeScopeRecord> records;
    if (!scopeTable(info, records, error)) return false;
    const uint64_t rva = pc - imageBase_;
    for (const SeScopeRecord& record : records) {
        if (rva >= record.beginAddress && rva < record.endAddress) {
            out.push_back(record);
        }
    }
    std::sort(out.begin(), out.end(),
              [](const SeScopeRecord& a, const SeScopeRecord& b) {
                  const uint32_t aSize = a.endAddress - a.beginAddress;
                  const uint32_t bSize = b.endAddress - b.beginAddress;
                  if (aSize != bSize) return aSize < bSize;
                  return a.beginAddress > b.beginAddress;
              });
    return true;
}

bool PeUnwindTable::virtualUnwind(const SeRuntimeFunction& function,
                                  const SeUnwindInfo& info, SeContext& context,
                                  const SeStackReader& readStack,
                                  std::string* error) const {
    if (!readStack) {
        setError(error, "no stack reader supplied");
        return false;
    }
    const uint64_t functionBegin = imageBase_ + function.beginRva;
    if (context.rip < functionBegin) {
        setError(error, "control PC is before the function");
        return false;
    }
    const uint64_t prologOffset = context.rip - functionBegin;
    uint64_t rsp = context.rsp;
    auto readQword = [&](uint64_t address, uint64_t& value) {
        return readStack(address, &value, sizeof(value));
    };
    bool machineFrame = false;
    for (const SeUnwindCode& code : info.codes) {
        // Codes whose prolog offset has not been reached have not run yet.
        if (code.codeOffset > prologOffset) continue;
        switch (code.operation) {
            case kUnwPushNonVol: {
                uint64_t value = 0;
                if (!readQword(rsp, value)) {
                    setError(error, "fault reading pushed register");
                    return false;
                }
                context.gpr[code.operationInfo & 15U] = value;
                rsp += 8;
                break;
            }
            case kUnwAllocLarge:
                rsp += code.frameOffset;
                break;
            case kUnwAllocSmall:
                rsp += static_cast<uint64_t>(code.operationInfo + 1U) * 8U;
                break;
            case kUnwSetFpReg: {
                if (info.frameRegister == 0 || info.frameRegister >= 16) {
                    setError(error, "SET_FPREG without a valid frame register");
                    return false;
                }
                rsp = context.gpr[info.frameRegister] -
                      static_cast<uint64_t>(info.frameOffset) * 16U;
                break;
            }
            case kUnwSaveNonVol:
            case kUnwSaveNonVolFar: {
                uint64_t value = 0;
                if (!readQword(rsp + code.frameOffset, value)) {
                    setError(error, "fault reading saved register");
                    return false;
                }
                context.gpr[code.operationInfo & 15U] = value;
                break;
            }
            case kUnwSaveXmm128:
            case kUnwSaveXmm128Far:
                break;  // no XMM state in the integer context
            case kUnwPushMachineFrame: {
                if ((code.operationInfo & 1U) != 0) rsp += 8;
                uint64_t rip = 0;
                uint64_t oldRsp = 0;
                if (!readQword(rsp, rip) || !readQword(rsp + 24U, oldRsp)) {
                    setError(error, "fault reading machine frame");
                    return false;
                }
                context.rip = rip;
                rsp = oldRsp;
                machineFrame = true;
                break;
            }
            default:
                setError(error, "unsupported unwind operation");
                return false;
        }
    }
    if (!machineFrame) {
        uint64_t returnAddress = 0;
        if (!readQword(rsp, returnAddress)) {
            setError(error, "fault reading return address");
            return false;
        }
        context.rip = returnAddress;
        rsp += 8;
    }
    context.rsp = rsp;
    context.gpr[4] = rsp;
    return true;
}

}  // namespace rottr
