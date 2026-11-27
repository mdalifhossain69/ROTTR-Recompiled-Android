#include "rottr/x64_bridge.h"
#include "rottr/x64_runtime.h"
#include "rottr/guest_mem.h"

#include <cstdio>
#include <cstring>
#include <array>
#include <vector>

extern "C" uint64_t host_ExitProcess(uint64_t exitCode, uint64_t, uint64_t,
                                     uint64_t, uint64_t, uint64_t,
                                     uint64_t, uint64_t);

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}
}  // namespace

// A guest program that calls ExitProcess(42):
//   mov rcx, 42
//   mov rax, <exitProcAddr>
//   call rax
//   hlt
int main() {
    rottr::registerHostModules();

    rottr_guest_mem* mem = rottr_guest_reserve(4096);
    if (!expect(mem != nullptr, "guest reserve")) return 1;
    uintptr_t base = (uintptr_t)rottr_guest_base(mem);
    rottr_guest_commit_pages(mem, base, 4096);

    // Register host_ExitProcess as a host-call proc.
    uint64_t exitAddr = rottr::addHostCallProc(
        reinterpret_cast<rottr::HostProc>(&host_ExitProcess));
    if (!expect(exitAddr != 0, "ExitProcess registration must return address"))
        return 1;

    // Build guest program.
    constexpr std::size_t kCodeSize = 256;
    std::vector<uint8_t> code(kCodeSize, 0);
    std::size_t pc = 0;

    auto emit = [&](uint8_t b) { code[pc++] = b; };

    // mov rcx, 42
    emit(0x48); emit(0xb9);
    for (int i = 0; i < 8; ++i)
        emit((uint8_t)(((uint64_t)42 >> (i * 8)) & 0xff));
    // mov rax, exitAddr
    emit(0x48); emit(0xb8);
    for (int i = 0; i < 8; ++i)
        emit((uint8_t)((exitAddr >> (i * 8)) & 0xff));
    // call rax
    emit(0xff); emit(0xd0);
    // hlt (unreachable after ExitProcess)
    emit(0xf4);

    rottr_guest_write(mem, base, code.data(), pc);

    rottr_cpu cpu{};
    rottr_cpu_reset(&cpu, base, base + 4096);

    rottr::X64GuestMemory guest{
        base, reinterpret_cast<uint8_t*>(rottr_guest_base(mem)), 4096, true, true};
    rottr_mem_result r;
    rottr_guest_translate(mem, base, 4096, &r);
    guest.data = static_cast<uint8_t*>(r.host);

    auto result = rottr::runX64Interpreter(cpu, guest, 64);

    if (!expect(rottr::isExitRequested(), "ExitProcess must set exit requested") ||
        !expect(rottr::getExitCode() == 42, "ExitProcess(42) must set exit code to 42") ||
        !expect(result.instructionsExecuted > 0, "instructions must be counted")) {
        return 1;
    }

    rottr_guest_release(mem);
    std::puts("OK");
    return 0;
}
