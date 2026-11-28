// Roadmap step 48: translated basic block state matches the interpreter.
// Pass: gpr[] and rip are identical after executing a supported block.
#include "rottr/arm64_block_translator.h"
#include "rottr/arm64_code_buffer.h"
#include "rottr/arm64_emitter.h"
#include "rottr/x64_runtime.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace rottr;
using namespace rottr::arm64;

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

bool cpuEqual(const rottr_cpu& a, const rottr_cpu& b) {
    if (a.rip != b.rip) {
        std::fprintf(stderr, "  rip mismatch: %llx vs %llx\n",
                     static_cast<unsigned long long>(a.rip),
                     static_cast<unsigned long long>(b.rip));
        return false;
    }
    for (int i = 0; i < 16; ++i) {
        if (a.gpr[i] != b.gpr[i]) {
            std::fprintf(stderr, "  gpr[%d] mismatch: %llx vs %llx\n", i,
                         static_cast<unsigned long long>(a.gpr[i]),
                         static_cast<unsigned long long>(b.gpr[i]));
            return false;
        }
    }
    return true;
}

void resetCpu(rottr_cpu& cpu, uint64_t rip, uint64_t rsp) {
    std::memset(&cpu, 0, sizeof(cpu));
    cpu.rip = rip;
    cpu.gpr[4] = rsp;  // RSP
}

}  // namespace

int main() {
    const uint64_t kBase = 0x1000;
    const size_t kMemSize = 65536;
    std::vector<uint8_t> guestMem(kMemSize);
    X64GuestMemory mem{kBase, guestMem.data(), kMemSize, true, true};

    // ---- Block 1: mov rax, 5; mov rbx, 7; add rax, rbx ----
    {
        std::vector<uint8_t> code = {
            0x48, 0xb8, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // mov rax, 5
            0x48, 0xbb, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // mov rbx, 7
            0x48, 0x01, 0xd8,  // add rax, rbx
        };
        std::memcpy(guestMem.data() + (0x1000 - kBase), code.data(), code.size());

        auto result = translateBasicBlock(mem, 0x1000, 3);
        if (!expect(result.status == TranslationResult::Status::Translated,
                    "block 1 should translate")) return 1;
        if (!expect(result.instructionCount == 3, "block 1 should have 3 instructions"))
            return 1;

        ExecutableMemory buffer(4096);
        if (!expect(buffer.writeWords(0, result.code.data(), result.code.size()),
                    "write translated code")) return 1;
        if (!expect(buffer.seal(), "seal buffer")) return 1;

        rottr_cpu cpuTranslated{};
        resetCpu(cpuTranslated, 0x1000, 0x8000);
        auto fn = reinterpret_cast<void (*)(rottr_cpu*)>(buffer.entry());
        fn(&cpuTranslated);
        cpuTranslated.rip = result.endRip;

        rottr_cpu cpuInterp{};
        resetCpu(cpuInterp, 0x1000, 0x8000);
        runX64Interpreter(cpuInterp, mem, 16);

        if (!expect(cpuEqual(cpuTranslated, cpuInterp), "block 1 state matches"))
            return 1;
        if (!expect(cpuTranslated.gpr[0] == 12, "rax == 12")) return 1;  // 5+7
    }

    // ---- Block 2: mov rcx, 100; sub rcx, 25 ----
    {
        std::vector<uint8_t> code = {
            0x48, 0xb9, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // mov rcx, 100
            0x48, 0x83, 0xe9, 0x19,  // sub rcx, 25
        };
        std::memcpy(guestMem.data() + (0x2000 - kBase), code.data(), code.size());

        auto result = translateBasicBlock(mem, 0x2000, 2);
        if (!expect(result.status == TranslationResult::Status::Translated,
                    "block 2 should translate")) return 1;

        ExecutableMemory buffer(4096);
        buffer.writeWords(0, result.code.data(), result.code.size());
        buffer.seal();

        rottr_cpu cpuTranslated{};
        resetCpu(cpuTranslated, 0x2000, 0x8000);
        auto fn = reinterpret_cast<void (*)(rottr_cpu*)>(buffer.entry());
        fn(&cpuTranslated);
        cpuTranslated.rip = result.endRip;

        rottr_cpu cpuInterp{};
        resetCpu(cpuInterp, 0x2000, 0x8000);
        runX64Interpreter(cpuInterp, mem, 16);

        if (!expect(cpuEqual(cpuTranslated, cpuInterp), "block 2 state matches"))
            return 1;
        if (!expect(cpuTranslated.gpr[1] == 75, "rcx == 75")) return 1;
    }

    // ---- Block 3: mov rdx, 10; mov rsi, 20; xor rdx, rsi ----
    {
        std::vector<uint8_t> code = {
            0x48, 0xba, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // mov rdx, 10
            0x48, 0xbe, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // mov rsi, 20
            0x48, 0x31, 0xf2,  // xor rdx, rsi
        };
        std::memcpy(guestMem.data() + (0x3000 - kBase), code.data(), code.size());

        auto result = translateBasicBlock(mem, 0x3000, 3);
        if (!expect(result.status == TranslationResult::Status::Translated,
                    "block 3 should translate")) return 1;

        ExecutableMemory buffer(4096);
        buffer.writeWords(0, result.code.data(), result.code.size());
        buffer.seal();

        rottr_cpu cpuTranslated{};
        resetCpu(cpuTranslated, 0x3000, 0x8000);
        auto fn = reinterpret_cast<void (*)(rottr_cpu*)>(buffer.entry());
        fn(&cpuTranslated);
        cpuTranslated.rip = result.endRip;

        rottr_cpu cpuInterp{};
        resetCpu(cpuInterp, 0x3000, 0x8000);
        runX64Interpreter(cpuInterp, mem, 16);

        if (!expect(cpuEqual(cpuTranslated, cpuInterp), "block 3 state matches"))
            return 1;
        if (!expect(cpuTranslated.gpr[2] == (0xa ^ 0x14), "rdx == 10^20")) return 1;
    }

    // ---- Block 4: unsupported memory inc makes the whole block fall back ----
    {
        const uint64_t target = 0x4011;
        std::vector<uint8_t> code = {
            0x48, 0xb8, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // mov rax, 5
            0x48, 0xff, 0x05, 0x00, 0x00, 0x00, 0x00  // inc qword [rip]
        };
        std::memcpy(guestMem.data() + (0x4000 - kBase), code.data(), code.size());
        uint64_t value = 0x10;
        std::memcpy(guestMem.data() + (target - kBase), &value, sizeof(value));

        auto result = translateBasicBlock(mem, 0x4000, 2);
        if (!expect(result.status == TranslationResult::Status::Unsupported,
                    "block 4 should fall back to the interpreter")) return 1;
        if (!expect(result.code.empty(),
                    "unsupported block must not emit partial native code")) return 1;

        rottr_cpu cpuInterp{};
        resetCpu(cpuInterp, 0x4000, 0x8000);
        runX64Interpreter(cpuInterp, mem, 2);
        uint64_t updated = 0;
        std::memcpy(&updated, guestMem.data() + (target - kBase), sizeof(updated));
        if (!expect(cpuInterp.gpr[0] == 5 && updated == 0x11,
                    "interpreter fallback executes the full block")) return 1;
    }

    std::puts("OK (translated blocks match interpreter state)");
    return 0;
}