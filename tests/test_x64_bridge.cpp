#include "rottr/x64_bridge.h"
#include "rottr/x64_runtime.h"
#include "x64_test_runner.h"
#include "rottr/guest_mem.h"

#include <cstdio>
#include <cstring>
#include <array>
#include <vector>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}
}  // namespace

// A simple host function: returns arg1 + arg2.
extern "C" uint64_t host_add(uint64_t a, uint64_t b, uint64_t, uint64_t,
                             uint64_t, uint64_t, uint64_t, uint64_t) {
    return a + b;
}

// A host function that reads a stack argument (5th arg).
extern "C" uint64_t host_stack_arg(uint64_t, uint64_t, uint64_t, uint64_t,
                                   uint64_t a5, uint64_t, uint64_t,
                                   uint64_t) {
    return a5;
}

// A guest function (x86-64 machine code) that host code can call via callback:
//   mov rax, rcx
//   add rax, rdx
//   ret
constexpr std::array<uint8_t, 7> kGuestAddCode = {
    0x48, 0x89, 0xc8,  // mov rax, rcx
    0x48, 0x01, 0xd0,  // add rax, rdx
    0xc3,              // ret
};

int main() {
    // ---- Step 29: guest-to-host call bridge ----
    // Reserve guest memory first so we know the base for code generation.
    rottr_guest_mem* mem = rottr_guest_reserve(0x4000);
    if (!expect(mem != nullptr, "guest reserve")) return 1;
    uintptr_t base = (uintptr_t)rottr_guest_base(mem);
    rottr_guest_commit_pages(mem, base, 0x4000);

    // Register host_add as a host-call proc.
    uint64_t addAddr = rottr::addHostCallProc(reinterpret_cast<rottr::HostProc>(&host_add));
    uint64_t stackAddr = rottr::addHostCallProc(
        reinterpret_cast<rottr::HostProc>(&host_stack_arg));

    // Build a tiny guest program:
    //   mov rax, <addAddr>; call rax; hlt          -> host_add(3,4) = 7
    //   mov rax, <stackAddr>; hlt                   -> (just to use it)
    // Second program:
    //   mov rcx, 10; mov rdx, 20; mov r8, 30; mov r9, 40;
    //   mov rax, <stackAddr>; push 100; call rax; hlt -> host_stack_arg(10,20,30,40,100) = 100
    constexpr std::size_t kCodeSize = 256;
    std::vector<uint8_t> code(kCodeSize, 0);
    std::size_t pc = 0;

    // Helper to emit
    auto emit = [&](uint8_t b) { code[pc++] = b; };

    // mov rcx, 3
    emit(0x48); emit(0xb9);
    for (int i = 0; i < 8; ++i) emit((uint8_t)(((uint64_t)3 >> (i * 8)) & 0xff));
    // mov rdx, 4
    emit(0x48); emit(0xba);
    for (int i = 0; i < 8; ++i) emit((uint8_t)(((uint64_t)4 >> (i * 8)) & 0xff));
    // mov rax, addAddr
    emit(0x48); emit(0xb8);
    for (int i = 0; i < 8; ++i) emit((uint8_t)((addAddr >> (i * 8)) & 0xff));
    // call rax (FF D0)
    emit(0xff); emit(0xd0);
    // hlt
    emit(0xf4);

    // mov rcx, 10
    emit(0x48); emit(0xb9);
    for (int i = 0; i < 8; ++i) emit((uint8_t)(((uint64_t)10 >> (i * 8)) & 0xff));
    // mov rdx, 20
    emit(0x48); emit(0xba);
    for (int i = 0; i < 8; ++i) emit((uint8_t)(((uint64_t)20 >> (i * 8)) & 0xff));
    // mov r8, 30
    emit(0x49); emit(0xb8);
    for (int i = 0; i < 8; ++i) emit((uint8_t)(((uint64_t)30 >> (i * 8)) & 0xff));
    // mov r9, 40
    emit(0x49); emit(0xb9);
    for (int i = 0; i < 8; ++i) emit((uint8_t)(((uint64_t)40 >> (i * 8)) & 0xff));
    // push 100 (stack arg #5)
    emit(0x6a); emit((uint8_t)100);
    // mov rax, stackAddr
    emit(0x48); emit(0xb8);
    for (int i = 0; i < 8; ++i) emit((uint8_t)((stackAddr >> (i * 8)) & 0xff));
    // call rax
    emit(0xff); emit(0xd0);
    // hlt
    emit(0xf4);

    // Copy code into guest memory.
    rottr_guest_write(mem, base, code.data(), pc);

    rottr_cpu cpu{};
    rottr_cpu_reset(&cpu, base, base + 4096);

    rottr::X64GuestMemory guest{
        base, reinterpret_cast<uint8_t*>(rottr_guest_base(mem)), 0x4000, true, true};
    rottr_mem_result r;
    rottr_guest_translate(mem, base, 4096, &r);
    guest.data = static_cast<uint8_t*>(r.host);

    auto result = runX64ForTest(cpu, guest, 64);
    std::fprintf(stderr, "DEBUG: reason=%d rax=0x%llx rip=0x%llx executed=%llu\n",
            (int)result.reason,
            (unsigned long long)cpu.gpr[0],
            (unsigned long long)cpu.rip,
            (unsigned long long)result.instructionsExecuted);
    if (!expect(result.reason == rottr::X64StopReason::Halted,
                "guest-to-host bridge call must reach HLT") ||
        !expect(cpu.gpr[0] == 7, "host_add(3,4) must return 7 in RAX") ||
        !expect(result.instructionsExecuted > 0, "instructions must be counted")) {
        return 1;
    }

    // ---- Step 30: host-to-guest callback ----
    // Write guest add code into guest memory.
    uintptr_t cbAddr = base + 0x2000;
    rottr_guest_write(mem, cbAddr, kGuestAddCode.data(), kGuestAddCode.size());

    uint64_t cbHandle = rottr::registerGuestCallback(cbAddr);
    if (!expect(cbHandle != 0, "callback registration must return handle")) return 1;
    if (!expect(rottr::resolveGuestCallback(cbHandle) == cbAddr,
                "callback resolution must return guest addr")) {
        return 1;
    }

    // Set up CPU and invoke the guest callback via the bridge.
    rottr_cpu_reset(&cpu, base, base + 4096);
    cpu.gpr[4] = base + 0x3000; // stack
    uint64_t cbResult = 0;
    bool ok = rottr::callGuestProc(cbHandle, cpu, guest, 7, 8, 0, 0, cbResult);
    if (!expect(ok, "callGuestProc must succeed") ||
        !expect(cbResult == 15, "guest callback 7+8 must return 15 in RAX")) {
        return 1;
    }

    rottr_guest_release(mem);
    std::puts("OK");
    return 0;
}
