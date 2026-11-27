#include "rottr/x64_bridge.h"

#include <cstdio>
#include <cstring>
#include <mutex>

namespace rottr {

namespace {

struct HostCallEntry {
    uint64_t guestAddr;
    HostProc proc;
};
std::vector<HostCallEntry> g_hostCallProcs;
uint64_t nextHostCallAddr = 0x7fff00000000ull;

// Guest callback registry: host call handle -> guest function address.
struct GuestCallbackEntry {
    uint64_t handle;
    uint64_t guestAddr;
};
std::vector<GuestCallbackEntry> g_guestCallbacks;
uint64_t nextGuestCallbackHandle = 0x8000000000000000ull;

}  // namespace

std::mutex& hostModulesMutex() {
    static std::mutex m;
    return m;
}

std::vector<HostModule>& hostModules() {
    static std::vector<HostModule> mods;
    return mods;
}

void registerHostModule(HostModule module) {
    std::lock_guard<std::mutex> lock(hostModulesMutex());
    hostModules().push_back(std::move(module));
}

uint64_t resolveHostExport(const std::string& module, const std::string& name,
                           uint16_t ordinal, bool byOrdinal) {
    std::lock_guard<std::mutex> lock(hostModulesMutex());
    for (const HostModule& mod : hostModules()) {
        if (mod.name != module) continue;
        if (byOrdinal) {
            auto it = mod.exportsByOrdinal.find(ordinal);
            if (it != mod.exportsByOrdinal.end()) return it->second;
        } else if (!name.empty()) {
            auto it = mod.exportsByName.find(name);
            if (it != mod.exportsByName.end()) return it->second;
        }
    }
    return 0;
}

uint64_t addHostCallProc(HostProc proc) {
    std::lock_guard<std::mutex> lock(hostModulesMutex());
    uint64_t guestAddr = nextHostCallAddr;
    nextHostCallAddr += 0x1000;
    g_hostCallProcs.push_back({guestAddr, proc});
    return guestAddr;
}

HostProc resolveHostCallProc(uint64_t guestAddr) {
    std::lock_guard<std::mutex> lock(hostModulesMutex());
    for (const HostCallEntry& entry : g_hostCallProcs) {
        if (entry.guestAddr == guestAddr) return entry.proc;
    }
    return nullptr;
}

uint64_t callHostProc(uint64_t guestProcAddr, rottr_cpu& cpu,
                      const X64GuestMemory& memory) {
    HostProc proc = resolveHostCallProc(guestProcAddr);
    if (!proc) return 0;

    // Windows x64 args: RCX, RDX, R8, R9, then stack.
    uint64_t arg1 = cpu.gpr[1];  // RCX
    uint64_t arg2 = cpu.gpr[2];  // RDX
    uint64_t arg3 = cpu.gpr[9];  // R8
    uint64_t arg4 = cpu.gpr[10]; // R9

    uint64_t stackArgs[kHostCallStackSlots] = {};
    uint64_t rsp = cpu.gpr[4];
    for (int i = 0; i < kHostCallStackSlots; ++i) {
        uint64_t addr = rsp + static_cast<uint64_t>(i) * 8ULL;
        if (addr < memory.base || addr - memory.base + 8 > memory.size) break;
        std::memcpy(&stackArgs[i], memory.data + (addr - memory.base), 8);
    }

    uint64_t result = proc(arg1, arg2, arg3, arg4,
                           stackArgs[0], stackArgs[1],
                           stackArgs[2], stackArgs[3]);
    return result;
}

uint64_t registerGuestCallback(uint64_t guestAddr) {
    std::lock_guard<std::mutex> lock(hostModulesMutex());
    uint64_t handle = nextGuestCallbackHandle;
    nextGuestCallbackHandle += 0x1000;
    g_guestCallbacks.push_back({handle, guestAddr});
    return handle;
}

uint64_t resolveGuestCallback(uint64_t handle) {
    std::lock_guard<std::mutex> lock(hostModulesMutex());
    for (const GuestCallbackEntry& entry : g_guestCallbacks) {
        if (entry.handle == handle) return entry.guestAddr;
    }
    return 0;
}

bool callGuestProc(uint64_t handle, rottr_cpu& cpu,
                   const X64GuestMemory& memory,
                   uint64_t rcx, uint64_t rdx, uint64_t r8, uint64_t r9,
                   uint64_t& outResult) {
    uint64_t guestAddr = resolveGuestCallback(handle);
    if (!guestAddr) return false;

    // Set up Windows x64 calling convention args.
    cpu.gpr[1] = rcx;   // RCX
    cpu.gpr[2] = rdx;   // RDX
    cpu.gpr[9] = r8;    // R8
    cpu.gpr[10] = r9;   // R9

    // Push a sentinel return address onto the guest stack.
    const uint64_t kReturnSentinel = 0xdeadbeefdeadbeefull;
    cpu.gpr[4] -= 8; // RSP
    if (cpu.gpr[4] < memory.base ||
        cpu.gpr[4] - memory.base + 8 > memory.size) return false;
    std::memcpy(memory.data + (cpu.gpr[4] - memory.base),
                &kReturnSentinel, 8);

    // Set guest RIP to the callback entry and run until it returns to the
    // sentinel (or faults/halts).
    cpu.rip = guestAddr;
    const uint64_t kBudget = 4096;
    while (true) {
        auto result = runX64Interpreter(cpu, memory, kBudget);
        if (result.reason == rottr::X64StopReason::Halted) {
            outResult = cpu.gpr[0];
            return true;
        }
        if (result.reason == rottr::X64StopReason::InvalidMemory) {
            // RET to sentinel: RET jumped RIP to the sentinel address, then
            // the next instruction fetch faulted. RSP was already popped by
            // RET; capture RAX.
            if (cpu.rip == kReturnSentinel) {
                outResult = cpu.gpr[0];
                return true;
            }
            return false;
        }
        if (result.reason == rottr::X64StopReason::UnsupportedInstruction) {
            return false;
        }
        // Budget exhausted; continue running.
        (void)result;
    }
}

}  // namespace rottr
