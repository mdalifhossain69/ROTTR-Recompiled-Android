#pragma once

#include "rottr/guest.h"
#include "rottr/x64_runtime.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace rottr {

// A host-implemented Windows API function.
// Args follow Windows x64 convention: RCX, RDX, R8, R9, then stack.
// Returns value in RAX.
using HostProc = uint64_t (*)(uint64_t rcx, uint64_t rdx, uint64_t r8,
                              uint64_t r9, uint64_t a5, uint64_t a6,
                              uint64_t a7, uint64_t a8);

struct HostModule {
    std::string name;
    std::unordered_map<std::string, uint64_t> exportsByName;
    std::unordered_map<uint16_t, uint64_t> exportsByOrdinal;
};

void registerHostModule(HostModule module);
uint64_t resolveHostExport(const std::string& module, const std::string& name,
                           uint16_t ordinal, bool byOrdinal);

// Initialize host modules (Windows API implementations). Call once before
// resolving imports.
void registerHostModules();

// Internal: access to host module registry (used by win32_host.cpp).
std::mutex& hostModulesMutex();
std::vector<HostModule>& hostModules();

// Reserve a guest call address for a host function and record the mapping.
// Returns the guest address that guest code should call.
uint64_t addHostCallProc(HostProc proc);

// Look up a host function pointer by guest call address.
HostProc resolveHostCallProc(uint64_t guestAddr);

// Call a host function from guest context. Reads stack args from guest memory.
// Returns the host function's return value (to be placed in guest RAX).
uint64_t callHostProc(uint64_t guestProcAddr, rottr_cpu& cpu,
                      const X64GuestMemory& memory);

constexpr int kHostCallStackSlots = 8;

}  // namespace rottr
