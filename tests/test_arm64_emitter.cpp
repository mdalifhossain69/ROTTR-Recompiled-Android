// Roadmap step 46: every emitted instruction must match LLVM's assembler.
// Pass: the emitter output matches the LLVM assembler for the covered set.
#include "rottr/arm64_emitter.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <unistd.h>

#ifndef LLVM_MC_PATH
#define LLVM_MC_PATH "llvm-mc"
#endif

using namespace rottr::arm64;

namespace {

const std::string kTempFile =
    "/tmp/rottr-arm64-emitter-" + std::to_string(static_cast<long>(getpid())) + ".s";

bool llvmEncode(const std::string& instruction, uint32_t& word) {
    {
        std::ofstream output(kTempFile, std::ios::trunc);
        if (!output) return false;
        output << ".text\n" << instruction << "\n";
    }
    const std::string command = "\"" LLVM_MC_PATH
                                "\" -triple=aarch64 -show-encoding " +
                                kTempFile + " 2>/dev/null";
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) return false;
    std::string output;
    char buffer[512];
    while (std::fgets(buffer, sizeof buffer, pipe) != nullptr) output += buffer;
    const int status = pclose(pipe);
    if (status != 0) return false;

    const std::string marker = "encoding: [";
    std::size_t position = output.find(marker);
    if (position == std::string::npos) return false;
    position += marker.size();
    uint32_t value = 0;
    int count = 0;
    while (position < output.size() && count < 4) {
        if (output[position] == '0' && position + 1 < output.size() &&
            (output[position + 1] == 'x' || output[position + 1] == 'X')) {
            char* end = nullptr;
            const long byte = std::strtol(output.c_str() + position + 2, &end, 16);
            value |= static_cast<uint32_t>(byte & 0xff) << (8 * count);
            ++count;
            position = static_cast<std::size_t>(end - output.c_str());
        } else {
            ++position;
        }
    }
    if (count != 4) return false;
    word = value;
    return true;
}

bool check(const char* source, uint32_t actual) {
    uint32_t expected = 0;
    if (!llvmEncode(source, expected)) {
        std::fprintf(stderr, "FAIL: llvm-mc could not assemble: %s\n", source);
        return false;
    }
    if (actual != expected) {
        std::fprintf(stderr, "FAIL: %s\n  emitter 0x%08x\n  llvm    0x%08x\n",
                     source, actual, expected);
        return false;
    }
    return true;
}

}  // namespace

int main() {
    const std::string versionCommand =
        "\"" LLVM_MC_PATH "\" --version >/dev/null 2>&1";
    if (std::system(versionCommand.c_str()) != 0) {
        std::puts("SKIP: llvm-mc not available");
        return 77;
    }

    bool ok = true;
    ok &= check("nop", nop());
    ok &= check("ret", ret());
    ok &= check("ret x19", ret(19));
    ok &= check("br x5", br(5));
    ok &= check("blr x6", blr(6));
    ok &= check("svc #0", svc(0));
    ok &= check("brk #1", brk(1));

    ok &= check("mov x1, x2", movReg(1, 2));
    ok &= check("movz x3, #0x1234, lsl #16", movz(3, 0x1234, 16));
    ok &= check("movn x4, #0xabcd", movn(4, 0xabcd));
    ok &= check("movk x5, #0xbeef, lsl #32", movk(5, 0xbeef, 32));
    const std::vector<uint32_t> materialised = movImm(7, 0x123456789abcdef0ULL);
    if (materialised.size() != 4) {
        std::fprintf(stderr, "FAIL: movImm produced %zu words\n",
                     materialised.size());
        ok = false;
    } else {
        ok &= check("movz x7, #0xdef0", materialised[0]);
        ok &= check("movk x7, #0x9abc, lsl #16", materialised[1]);
        ok &= check("movk x7, #0x5678, lsl #32", materialised[2]);
        ok &= check("movk x7, #0x1234, lsl #48", materialised[3]);
    }

    ok &= check("add x1, x2, #0x123", addImm(1, 2, 0x123));
    ok &= check("add x1, x2, #0x123, lsl #12", addImm(1, 2, 0x123, 12));
    ok &= check("sub x3, x4, #5", subImm(3, 4, 5));
    ok &= check("cmp x5, #7", cmpImm(5, 7));
    ok &= check("add x1, x2, x3", addReg(1, 2, 3));
    ok &= check("add x1, x2, x3, lsl #4", addReg(1, 2, 3, Shift::Lsl, 4));
    ok &= check("sub x1, x2, x3, asr #2",
                subReg(1, 2, 3, Shift::Asr, 2));
    ok &= check("cmp x1, x2", cmpReg(1, 2));
    ok &= check("and x1, x2, x3", andReg(1, 2, 3));
    ok &= check("orr x1, x2, x3", orrReg(1, 2, 3));
    ok &= check("eor x1, x2, x3", eorReg(1, 2, 3));

    ok &= check("lsl x1, x2, #5", lslImm(1, 2, 5));
    ok &= check("lsr x1, x2, #5", lsrImm(1, 2, 5));
    ok &= check("asr x1, x2, #5", asrImm(1, 2, 5));
    ok &= check("lsl x1, x2, x3", lslReg(1, 2, 3));
    ok &= check("lsr x1, x2, x3", lsrReg(1, 2, 3));
    ok &= check("asr x1, x2, x3", asrReg(1, 2, 3));

    ok &= check("mul x1, x2, x3", mul(1, 2, 3));
    ok &= check("msub x1, x2, x3, x4", msub(1, 2, 3, 4));

    ok &= check("ldr x1, [x2, #16]", ldrX(1, 2, 16));
    ok &= check("str x1, [x2, #24]", strX(1, 2, 24));
    ok &= check("ldr w1, [x2, #12]", ldrW(1, 2, 12));
    ok &= check("str w1, [x2, #12]", strW(1, 2, 12));
    ok &= check("ldrb w1, [x2, #3]", ldrb(1, 2, 3));
    ok &= check("strb w1, [x2, #3]", strb(1, 2, 3));
    ok &= check("ldrh w1, [x2, #6]", ldrh(1, 2, 6));
    ok &= check("strh w1, [x2, #6]", strh(1, 2, 6));
    ok &= check("ldrsw x1, [x2, #8]", ldrsw(1, 2, 8));

    ok &= check("b #8", b(8));
    ok &= check("bl #-4", bl(-4));
    ok &= check("b.eq #12", bCond(Condition::Eq, 12));
    ok &= check("b.ne #-16", bCond(Condition::Ne, -16));
    ok &= check("cbz x1, #16", cbz(1, 16));
    ok &= check("cbnz x1, #-8", cbnz(1, -8));
    ok &= check("tbz x1, #3, #8", tbz(1, 3, 8));
    ok &= check("tbnz x1, #40, #-8", tbnz(1, 40, -8));

    ok &= check("adr x1, #16", adr(1, 16));
    ok &= check("adrp x1, #4096", adrp(1, 4096));
    ok &= check("adrp x2, #-8192", adrp(2, -8192));

    ok &= check("mrs x12, NZCV", mrsNzcv(12));
    ok &= check("msr NZCV, x12", msrNzcv(12));
    ok &= check("cset x10, eq", cset(10, Condition::Eq));
    ok &= check("cset x10, ne", cset(10, Condition::Ne));
    ok &= check("cset x10, cs", cset(10, Condition::Cs));
    ok &= check("cset x10, cc", cset(10, Condition::Cc));
    ok &= check("cset x10, lt", cset(10, Condition::Lt));

    std::remove(kTempFile.c_str());
    if (!ok) return 1;
    std::puts("OK (emitter matches llvm-mc for all covered instructions)");
    return 0;
}
