// Roadmap step 47: generated ARM64 code must run repeatedly on the phone.
// Pass: the same sealed code buffer executes many times with correct results,
// and patched code is visible after a cache flush.
#include "rottr/arm64_code_buffer.h"
#include "rottr/arm64_emitter.h"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace rottr::arm64;

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

}  // namespace

int main() {
    ExecutableMemory memory(4096);
    if (!expect(memory.valid(), "buffer must be allocated")) return 1;
    if (!expect(memory.capacity() >= 4096, "buffer must expose its capacity"))
        return 1;
    if (!expect(memory.entry() != nullptr, "buffer must have an entry point"))
        return 1;

    // Bounds and state checks are portable.
    const uint32_t probe = 0;
    if (!expect(!memory.write(4094, &probe, 4),
                "write past capacity must be rejected"))
        return 1;
    if (!expect(!memory.writeWords(0, nullptr, 1),
                "null source must be rejected"))
        return 1;

    const std::vector<uint32_t> constant = {movz(0, 42, 0), ret()};
    if (!expect(memory.writeWords(0, constant.data(), constant.size()),
                "code must be writable before sealing"))
        return 1;
    if (!expect(memory.seal(), "buffer must seal read+execute")) return 1;
    if (!expect(memory.executable(), "sealed buffer must report executable"))
        return 1;
    if (!expect(!memory.writeWords(0, constant.data(), constant.size()),
                "sealed buffer must reject writes"))
        return 1;

#if !defined(__aarch64__)
    std::puts("SKIP: native execution requires an AArch64 host");
    return 77;
#else
    auto constantFunction = reinterpret_cast<uint64_t (*)()>(memory.entry());
    for (int i = 0; i < 100000; ++i) {
        if (constantFunction() != 42) {
            std::fprintf(stderr, "FAIL: constant function returned wrong value on run %d\n", i);
            return 1;
        }
    }

    // Second function: add x0, x0, x1; ret. Exercises the native ABI and a
    // different offset in the same executable mapping.
    const std::vector<uint32_t> add = {addReg(0, 0, 1), ret()};
    if (!expect(memory.unseal(), "buffer must unseal for patching")) return 1;
    if (!expect(memory.writeWords(256, add.data(), add.size()),
                "second function must be writable"))
        return 1;
    if (!expect(memory.seal(), "buffer must reseal")) return 1;

    auto addFunction = reinterpret_cast<uint64_t (*)(uint64_t, uint64_t)>(
        memory.entry(256));
    for (uint64_t i = 0; i < 100000; ++i) {
        const uint64_t a = i * 2654435761ULL;
        const uint64_t b = i * 40503ULL;
        if (addFunction(a, b) != a + b) {
            std::fprintf(stderr, "FAIL: add function wrong on run %llu\n",
                         static_cast<unsigned long long>(i));
            return 1;
        }
    }

    // Patch the first function and confirm the new immediate is visible after
    // sealing (which flushes the instruction cache).
    if (!expect(memory.unseal(), "buffer must unseal before patching")) return 1;
    const uint32_t patched = movz(0, 43, 0);
    if (!expect(memory.writeWords(0, &patched, 1), "patch must be written"))
        return 1;
    if (!expect(memory.seal(), "patched buffer must reseal")) return 1;
    for (int i = 0; i < 100000; ++i) {
        if (constantFunction() != 43) {
            std::fprintf(stderr, "FAIL: patched function returned stale value on run %d\n", i);
            return 1;
        }
    }

    std::puts("OK (generated code ran 300000 times; patched code visible after flush)");
    return 0;
#endif
}
