#include "rottr/x64_bridge.h"

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
    nextHostCallAddr += 0x1000; // space each out
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

}  // namespace rottr
