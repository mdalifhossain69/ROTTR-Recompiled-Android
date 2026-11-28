// Roadmap step 39: table-based exceptions and stack unwinding using .pdata.
// Pass: nested __try/__finally scope lookup and frame unwinding match the
// documented Windows x64 layout (verified against a synthetic image whose
// .pdata, UNWIND_INFO and scope table are laid out exactly as MSVC emits).
#include "rottr/se_unwind.h"

#include "pe_fixture.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#define CHECK(x)                                                              \
    do {                                                                      \
        if (!(x)) {                                                           \
            std::printf("FAILED line %d: %s\n", __LINE__, #x);                \
            return 1;                                                         \
        }                                                                     \
    } while (0)

using namespace pe_fixture;

namespace {

constexpr uint64_t kImageBase = 0x140000000ULL;

struct Stack {
    std::map<uint64_t, uint64_t> words;

    bool read(uint64_t address, void* dst, size_t size) const {
        auto* out = static_cast<uint8_t*>(dst);
        for (size_t i = 0; i < size; ++i) {
            const uint64_t wordAddress = (address + i) & ~7ULL;
            auto it = words.find(wordAddress);
            if (it == words.end()) return false;
            out[i] = static_cast<uint8_t>(it->second >> ((address + i) % 8) * 8);
        }
        return true;
    }
};

void writeUnwindInfo(std::vector<uint8_t>& file) {
    // .rdata RVA 0x2000 -> raw 0x1400.
    const size_t base = 0x1400;
    file[base + 0] = 0x09;  // version 1, UNW_FLAG_EHANDLER
    file[base + 1] = 0x10;  // SizeOfProlog
    file[base + 2] = 0x03;  // CountOfCodes
    file[base + 3] = 0x25;  // FrameRegister=rbp(5), FrameOffset=2
    // Codes are stored in descending code-offset order.
    file[base + 4] = 0x0c;  // SET_FPREG at prolog offset 0x0c
    file[base + 5] = 0x03;
    file[base + 6] = 0x08;  // ALLOC_SMALL 0x20 at 0x08
    file[base + 7] = 0x32;  // op=2, info=3 -> (3+1)*8 = 0x20
    file[base + 8] = 0x01;  // PUSH_NONVOL rbp at 0x01
    file[base + 9] = 0x50;  // op=0, info=5
    // Handler RVA follows the codes, 4-byte aligned (offset 12).
    put<uint32_t>(file, base + 12, 0x1500U);
    // Handler data begins at unwindInfoRva + 16 == RVA 0x2010 == raw 0x1410.
    put<uint32_t>(file, 0x1410, 2U);  // two scope records
    // Outer __try range with a __finally continuation.
    put<uint32_t>(file, 0x1414, 0x1000U);
    put<uint32_t>(file, 0x1418, 0x1100U);
    put<uint32_t>(file, 0x141c, 0x1500U);
    put<uint32_t>(file, 0x1420, 0x1010U);
    // Inner __try range.
    put<uint32_t>(file, 0x1424, 0x1020U);
    put<uint32_t>(file, 0x1428, 0x1080U);
    put<uint32_t>(file, 0x142c, 0x1500U);
    put<uint32_t>(file, 0x1430, 0x1030U);

    // Caller function's unwind info at RVA 0x2080 -> raw 0x1480.
    const size_t caller = 0x1480;
    file[caller + 0] = 0x01;  // version 1, no handler
    file[caller + 1] = 0x01;  // SizeOfProlog
    file[caller + 2] = 0x01;  // CountOfCodes
    file[caller + 3] = 0x00;  // no frame register
    file[caller + 4] = 0x01;  // PUSH_NONVOL rbx at 0x01
    file[caller + 5] = 0x30;  // op=0, info=3
}

std::vector<uint8_t> buildImage() {
    Builder builder(kImageBase, 0x6000, 0x3000);
    builder.entry(0x1000);
    builder.section(".text", 0x1000, 0x1000, 0x400, 0x1000, 0x60000020U);
    builder.section(".rdata", 0x2000, 0x1000, 0x1400, 0x1000, 0x40000040U);
    builder.section(".pdata", 0x3000, 0x1000, 0x2400, 0x200, 0x40000040U);
    // Exception directory (data directory 3).
    builder.directory(3, 0x3000, 24);
    writeUnwindInfo(builder.file);
    // .pdata records.
    put<uint32_t>(builder.file, 0x2400, 0x1000U);
    put<uint32_t>(builder.file, 0x2404, 0x1100U);
    put<uint32_t>(builder.file, 0x2408, 0x2000U);
    put<uint32_t>(builder.file, 0x240c, 0x1200U);
    put<uint32_t>(builder.file, 0x2410, 0x1300U);
    put<uint32_t>(builder.file, 0x2414, 0x2080U);
    return builder.build();
}

}  // namespace

int main() {
    const std::vector<uint8_t> file = buildImage();
    rottr::PeUnwindTable table;
    std::string error;
    CHECK(table.parse(file.data(), file.size(), kImageBase, 0x3000, 24, &error));
    CHECK(table.valid());
    CHECK(table.functions().size() == 2);

    // ---- RUNTIME_FUNCTION lookup uses half-open ranges ----
    rottr::SeRuntimeFunction function{};
    CHECK(table.lookup(kImageBase + 0x1000, function));
    CHECK(function.beginRva == 0x1000 && function.endRva == 0x1100 &&
          function.unwindInfoRva == 0x2000);
    CHECK(!table.lookup(kImageBase + 0x1100, function));  // end is exclusive
    CHECK(table.lookup(kImageBase + 0x1200, function));
    CHECK(function.beginRva == 0x1200 && function.endRva == 0x1300);
    CHECK(!table.lookup(kImageBase + 0x0500, function));

    // ---- UNWIND_INFO decoding ----
    rottr::SeRuntimeFunction inner{};
    CHECK(table.lookup(kImageBase + 0x1050, inner));
    rottr::SeUnwindInfo info{};
    CHECK(table.decode(inner, info, &error));
    CHECK(info.version == 1);
    CHECK(info.prologSize == 0x10);
    CHECK(info.frameRegister == 5 && info.frameOffset == 2);
    CHECK(info.hasExceptionHandler && !info.hasChainInfo);
    CHECK(info.exceptionHandlerRva == 0x1500);
    CHECK(info.handlerDataRva == 0x2010);
    CHECK(info.codes.size() == 3);
    CHECK(info.codes[0].operation == rottr::kUnwSetFpReg &&
          info.codes[0].codeOffset == 0x0c);
    CHECK(info.codes[1].operation == rottr::kUnwAllocSmall &&
          info.codes[1].operationInfo == 3 && info.codes[1].frameOffset == 0x20);
    CHECK(info.codes[2].operation == rottr::kUnwPushNonVol &&
          info.codes[2].operationInfo == 5);

    // ---- Nested __try/__finally scope lookup, innermost first ----
    std::vector<rottr::SeScopeRecord> scopes;
    CHECK(table.handlersAt(kImageBase + 0x1040, scopes, &error));
    CHECK(scopes.size() == 2);
    CHECK(scopes[0].beginAddress == 0x1020 && scopes[0].endAddress == 0x1080);
    CHECK(scopes[1].beginAddress == 0x1000 && scopes[1].endAddress == 0x1100);
    CHECK(scopes[0].jumpTarget == 0x1030);  // inner finally runs first
    CHECK(scopes[1].jumpTarget == 0x1010);  // outer finally runs second

    scopes.clear();
    CHECK(table.handlersAt(kImageBase + 0x1090, scopes, &error));
    CHECK(scopes.size() == 1 && scopes[0].beginAddress == 0x1000);
    scopes.clear();
    CHECK(!table.handlersAt(kImageBase + 0x1150, scopes, &error));

    // ---- Frame unwinding restores rsp, rbp and rip exactly ----
    const uint64_t x = 0x700000;
    Stack stack;
    stack.words[x + 0x20] = 0xdeadbeefcafef00dULL;       // saved rbp
    stack.words[x + 0x28] = kImageBase + 0x1250;         // return into caller
    stack.words[x + 0x30] = 0x1122334455667788ULL;       // caller's saved rbx
    stack.words[x + 0x38] = kImageBase + 0x9999;         // caller's return
    rottr::SeStackReader reader = [&stack](uint64_t address, void* dst, size_t size) {
        return stack.read(address, dst, size);
    };

    rottr::SeContext context{};
    context.rip = kImageBase + 0x1050;
    context.rsp = x;
    context.gpr[5] = x + 0x20;  // rbp = rsp + 0x20
    CHECK(table.virtualUnwind(inner, info, context, reader, &error));
    CHECK(context.rip == kImageBase + 0x1250);
    CHECK(context.rsp == x + 0x30);
    CHECK(context.gpr[5] == 0xdeadbeefcafef00dULL);
    CHECK(context.gpr[4] == context.rsp);

    rottr::SeRuntimeFunction caller{};
    CHECK(table.lookup(context.rip, caller));
    rottr::SeUnwindInfo callerInfo{};
    CHECK(table.decode(caller, callerInfo, &error));
    CHECK(table.virtualUnwind(caller, callerInfo, context, reader, &error));
    CHECK(context.rip == kImageBase + 0x9999);
    CHECK(context.rsp == x + 0x40);
    CHECK(context.gpr[3] == 0x1122334455667788ULL);  // rbx restored

    // A stack read fault is reported, never treated as success.
    rottr::SeContext faulting{};
    faulting.rip = kImageBase + 0x1050;
    faulting.rsp = 0x1000;  // no stack words here
    faulting.gpr[5] = 0x1020;
    CHECK(!table.virtualUnwind(inner, info, faulting, reader, &error));

    // ---- Directory parsing and malformed input rejection ----
    rottr::PeUnwindTable fromImage;
    CHECK(fromImage.parseFromImage(file.data(), file.size(), kImageBase, &error));
    CHECK(fromImage.functions().size() == 2);
    rottr::PeUnwindTable bad;
    CHECK(!bad.parse(file.data(), file.size(), kImageBase, 0x3000, 13, &error));
    CHECK(!bad.parse(file.data(), file.size(), kImageBase, 0x4000, 24, &error));

    std::puts("OK (pdata lookup, nested finally scopes, two-frame unwind)");
    return 0;
}
