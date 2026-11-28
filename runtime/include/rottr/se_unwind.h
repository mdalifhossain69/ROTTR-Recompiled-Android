// Roadmap step 39: table-based exceptions and stack unwinding using .pdata.
//
// This module parses the PE exception directory (RUNTIME_FUNCTION records),
// decodes UNWIND_INFO, exposes the __C_specific_handler scope table used by
// MSVC __try/__finally, and performs a virtual unwind of one frame. It is the
// data half of SEH; the guest CPU and dispatcher drive it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rottr {

struct SeRuntimeFunction {
    uint32_t beginRva = 0;
    uint32_t endRva = 0;
    uint32_t unwindInfoRva = 0;
};

// One decoded unwind-code slot. Slots that carry an extra immediate are kept
// as a single entry with `frameOffset` set; the raw slots are not repeated.
struct SeUnwindCode {
    uint8_t codeOffset = 0;
    uint8_t operation = 0;   // UWOP_*
    uint8_t operationInfo = 0;
    uint32_t frameOffset = 0;
};

enum : uint8_t {
    kUnwPushNonVol = 0,
    kUnwAllocLarge = 1,
    kUnwAllocSmall = 2,
    kUnwSetFpReg = 3,
    kUnwSaveNonVol = 4,
    kUnwSaveNonVolFar = 5,
    kUnwSaveXmm128 = 8,
    kUnwSaveXmm128Far = 9,
    kUnwPushMachineFrame = 10,
};

enum : uint8_t {
    kUnwFlagExceptionHandler = 0x1,
    kUnwFlagTerminationHandler = 0x2,
    kUnwFlagChainInfo = 0x4,
};

struct SeUnwindInfo {
    uint8_t version = 0;
    uint8_t flags = 0;
    uint8_t prologSize = 0;
    uint8_t countOfCodes = 0;
    uint8_t frameRegister = 0;
    uint8_t frameOffset = 0;
    bool hasExceptionHandler = false;
    bool hasChainInfo = false;
    uint32_t exceptionHandlerRva = 0;
    uint32_t handlerDataRva = 0;
    std::vector<SeUnwindCode> codes;
    SeRuntimeFunction chained;
};

// A scope-table record as used by __C_specific_handler for __try/__except and
// __try/__finally. The fourth field is the finally continuation address.
struct SeScopeRecord {
    uint32_t beginAddress = 0;
    uint32_t endAddress = 0;
    uint32_t handlerAddress = 0;
    uint32_t jumpTarget = 0;
};

// Register context used by the virtual unwinder. gpr indices follow the
// rottr_cpu layout (0=rax, 1=rcx, 4=rsp, 5=rbp, ...).
struct SeContext {
    uint64_t rip = 0;
    uint64_t rsp = 0;
    uint64_t gpr[16] = {};
};

// Reads guest stack bytes. Returns false on a fault.
using SeStackReader = std::function<bool(uint64_t address, void* dst, size_t size)>;

class PeUnwindTable {
public:
    // Parses the exception directory of a PE32+ image. `pdataRva` is the
    // directory RVA (data directory 3) and `pdataSize` its byte size.
    bool parse(const uint8_t* file, size_t fileSize, uint64_t imageBase,
               uint32_t pdataRva, uint32_t pdataSize, std::string* error = nullptr);

    // Parses using data directory 3 from the image's optional header.
    bool parseFromImage(const uint8_t* file, size_t fileSize, uint64_t imageBase,
                        std::string* error = nullptr);

    bool valid() const { return valid_; }
    uint64_t imageBase() const { return imageBase_; }
    const std::vector<SeRuntimeFunction>& functions() const { return functions_; }

    // Finds the function whose half-open range [begin,end) contains `pc`.
    bool lookup(uint64_t pc, SeRuntimeFunction& out) const;

    bool decode(const SeRuntimeFunction& function, SeUnwindInfo& out,
                std::string* error = nullptr) const;

    // Reads the __C_specific_handler scope table pointed at by the unwind
    // info's handler data. Returns false when the table is absent or malformed.
    bool scopeTable(const SeUnwindInfo& info, std::vector<SeScopeRecord>& out,
                    std::string* error = nullptr) const;

    // Scope records covering `pc`, innermost (smallest range) first. This is
    // the order in which __finally blocks and handlers must run.
    bool handlersAt(uint64_t pc, std::vector<SeScopeRecord>& out,
                    std::string* error = nullptr) const;

    // Simulates unwinding one frame using the unwind codes. On success the
    // context describes the caller: rip is the return address, rsp points just
    // past it, and nonvolatile registers restored by the codes are updated.
    bool virtualUnwind(const SeRuntimeFunction& function, const SeUnwindInfo& info,
                       SeContext& context, const SeStackReader& readStack,
                       std::string* error = nullptr) const;

private:
    bool rvaToOffset(uint32_t rva, uint32_t size, uint32_t& offset) const;
    bool readAt(uint32_t rva, void* dst, uint32_t size) const;

    struct Section {
        uint32_t rva = 0;
        uint32_t virtualSize = 0;
        uint32_t rawSize = 0;
        uint32_t rawOffset = 0;
    };

    const uint8_t* file_ = nullptr;
    size_t fileSize_ = 0;
    uint64_t imageBase_ = 0;
    bool valid_ = false;
    std::vector<Section> sections_;
    std::vector<SeRuntimeFunction> functions_;
};

}  // namespace rottr
