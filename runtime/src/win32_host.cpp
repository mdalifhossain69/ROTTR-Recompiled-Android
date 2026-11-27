// Host implementations of Windows API functions callable from guest code
// via the x64 call bridge. These run on the host and follow the HostProc
// signature: (rcx, rdx, r8, r9, a5, a6, a7, a8) -> rax.

#include "rottr/x64_bridge.h"
#include "rottr/guest_mem.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <atomic>

namespace rottr {

namespace {

// ---- Minimal handle table ----
struct HandleEntry {
    uint64_t type;
    int32_t refCount = 1;
    bool signaled = false;
};

std::map<uint64_t, HandleEntry> g_handles;
std::mutex g_handlesMutex;
uint64_t g_nextHandle = 0x1000;

uint64_t allocHandle(uint64_t type) {
    std::lock_guard<std::mutex> lock(g_handlesMutex);
    uint64_t h = g_nextHandle += 4;
    g_handles.emplace(h, HandleEntry{type});
    return h;
}

[[maybe_unused]] HandleEntry* findHandle(uint64_t h) {
    auto it = g_handles.find(h);
    return it == g_handles.end() ? nullptr : &it->second;
}

// ---- Thread-local last error ----
thread_local uint32_t g_lastError = 0;

}  // namespace

// ---- Dummy heap object ----
uint8_t g_heap[64] = {};

// =====================================================================
// kernel32.dll host exports
// =====================================================================

extern "C" uint64_t host_GetLastError(uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t, uint64_t) {
    return g_lastError;
}

extern "C" uint64_t host_SetLastError(uint64_t code, uint64_t, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t, uint64_t) {
    g_lastError = static_cast<uint32_t>(code);
    return 0;
}

extern "C" uint64_t host_GetTickCount(uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t, uint64_t) {
    return 0;
}

extern "C" uint64_t host_GetTickCount64(uint64_t, uint64_t, uint64_t, uint64_t,
                                        uint64_t, uint64_t, uint64_t, uint64_t) {
    return 0;
}

extern "C" uint64_t host_QueryPerformanceCounter(uint64_t lpCount, uint64_t, uint64_t,
                                                 uint64_t, uint64_t, uint64_t,
                                                 uint64_t, uint64_t) {
    if (lpCount) {
        (void)lpCount;
    }
    return 1;
}

extern "C" uint64_t host_QueryPerformanceFrequency(uint64_t lpFreq, uint64_t, uint64_t,
                                                   uint64_t, uint64_t, uint64_t,
                                                   uint64_t, uint64_t) {
    if (lpFreq) {
        (void)lpFreq;
    }
    return 1;
}

extern "C" uint64_t host_Sleep(uint64_t ms, uint64_t, uint64_t, uint64_t,
                               uint64_t, uint64_t, uint64_t, uint64_t) {
    (void)ms;
    return 0;
}

extern "C" uint64_t host_GetCurrentProcessId(uint64_t, uint64_t, uint64_t, uint64_t,
                                            uint64_t, uint64_t, uint64_t, uint64_t) {
    return 1;
}

extern "C" uint64_t host_GetCurrentThreadId(uint64_t, uint64_t, uint64_t, uint64_t,
                                           uint64_t, uint64_t, uint64_t, uint64_t) {
    return 1;
}

extern "C" uint64_t host_GetCurrentProcess(uint64_t, uint64_t, uint64_t, uint64_t,
                                          uint64_t, uint64_t, uint64_t, uint64_t) {
    return static_cast<uint64_t>(-1);
}

extern "C" uint64_t host_GetCurrentThread(uint64_t, uint64_t, uint64_t, uint64_t,
                                         uint64_t, uint64_t, uint64_t, uint64_t) {
    return static_cast<uint64_t>(-2);
}

extern "C" uint64_t host_GetCommandLineA(uint64_t, uint64_t, uint64_t, uint64_t,
                                        uint64_t, uint64_t, uint64_t, uint64_t) {
    // Return pointer to an empty command line in guest memory
    // Not implemented: would need to allocate guest memory
    return 0;
}

extern "C" uint64_t host_GetModuleHandleA(uint64_t namePtr, uint64_t, uint64_t,
                                          uint64_t, uint64_t, uint64_t,
                                          uint64_t, uint64_t) {
    (void)namePtr;
    return 0; // not implemented
}

extern "C" uint64_t host_GetProcAddress(uint64_t module, uint64_t procName,
                                        uint64_t, uint64_t, uint64_t,
                                        uint64_t, uint64_t, uint64_t) {
    (void)module; (void)procName;
    return 0; // not implemented
}

extern "C" uint64_t host_LoadLibraryA(uint64_t namePtr, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t,
                                      uint64_t, uint64_t) {
    (void)namePtr;
    return 0; // not implemented
}

extern "C" uint64_t host_VirtualAlloc(uint64_t addr, uint64_t size,
                                      uint64_t allocType, uint64_t prot,
                                      uint64_t, uint64_t, uint64_t, uint64_t) {
    // Route through the guest memory manager
    void* result = rottr_VirtualAlloc(reinterpret_cast<void*>(addr),
                                       static_cast<size_t>(size),
                                       static_cast<uint32_t>(allocType),
                                       static_cast<uint32_t>(prot));
    return reinterpret_cast<uint64_t>(result);
}

extern "C" uint64_t host_VirtualFree(uint64_t addr, uint64_t size,
                                     uint64_t freeType, uint64_t,
                                     uint64_t, uint64_t, uint64_t, uint64_t) {
    int result = rottr_VirtualFree(reinterpret_cast<void*>(addr),
                                    static_cast<size_t>(size),
                                    static_cast<uint32_t>(freeType));
    return static_cast<uint64_t>(result);
}

extern "C" uint64_t host_VirtualProtect(uint64_t addr, uint64_t size,
                                        uint64_t newProt, uint64_t oldProtPtr,
                                        uint64_t, uint64_t, uint64_t, uint64_t) {
    uint32_t old = 0;
    int result = rottr_VirtualProtect(reinterpret_cast<void*>(addr),
                                       static_cast<size_t>(size),
                                       static_cast<uint32_t>(newProt), &old);
    if (result && oldProtPtr) {
        // Write old protection back to guest memory - not possible without
        // guest memory context. Skip for now.
        (void)oldProtPtr;
    }
    return static_cast<uint64_t>(result);
}

extern "C" uint64_t host_VirtualQuery(uint64_t addr, uint64_t bufferPtr,
                                      uint64_t length, uint64_t,
                                      uint64_t, uint64_t, uint64_t, uint64_t) {
    rottr_mem_region_info info;
    int result = rottr_VirtualQuery(reinterpret_cast<const void*>(addr), &info,
                                     static_cast<size_t>(length));
    // Write back to guest memory - not possible without guest context
    (void)bufferPtr; (void)length;
    return static_cast<uint64_t>(result);
}

extern "C" uint64_t host_CreateFileA(uint64_t namePtr, uint64_t access,
                                     uint64_t share, uint64_t sa,
                                     uint64_t disp, uint64_t flags,
                                     uint64_t templateFile, uint64_t) {
    (void)namePtr; (void)access; (void)share; (void)sa;
    (void)disp; (void)flags; (void)templateFile;
    return allocHandle(1); // file handle
}

extern "C" uint64_t host_ReadFile(uint64_t handle, uint64_t buffer,
                                  uint64_t bytesToRead, uint64_t bytesRead,
                                  uint64_t overlapped, uint64_t, uint64_t,
                                  uint64_t) {
    (void)handle; (void)buffer; (void)bytesToRead; (void)bytesRead;
    (void)overlapped;
    return 0; // FALSE
}

extern "C" uint64_t host_WriteFile(uint64_t handle, uint64_t buffer,
                                   uint64_t bytesToWrite, uint64_t bytesWritten,
                                   uint64_t overlapped, uint64_t, uint64_t,
                                   uint64_t) {
    (void)handle; (void)buffer; (void)bytesToWrite; (void)bytesWritten;
    (void)overlapped;
    return 0; // FALSE
}

extern "C" uint64_t host_CloseHandle(uint64_t handle, uint64_t, uint64_t,
                                     uint64_t, uint64_t, uint64_t,
                                     uint64_t, uint64_t) {
    std::lock_guard<std::mutex> lock(g_handlesMutex);
    g_handles.erase(handle);
    return 1; // TRUE
}

extern "C" uint64_t host_SetFilePointer(uint64_t handle, uint64_t distance,
                                        uint64_t high, uint64_t method,
                                        uint64_t, uint64_t, uint64_t,
                                        uint64_t) {
    (void)handle; (void)distance; (void)high; (void)method;
    return static_cast<uint64_t>(-1); // INVALID_SET_FILE_POINTER
}

extern "C" uint64_t host_HeapAlloc(uint64_t heap, uint64_t flags,
                                   uint64_t bytes, uint64_t,
                                   uint64_t, uint64_t, uint64_t, uint64_t) {
    (void)heap; (void)flags;
    void* p = std::malloc(static_cast<size_t>(bytes));
    return reinterpret_cast<uint64_t>(p);
}

extern "C" uint64_t host_HeapFree(uint64_t heap, uint64_t flags,
                                  uint64_t mem, uint64_t,
                                  uint64_t, uint64_t, uint64_t, uint64_t) {
    (void)heap; (void)flags;
    if (mem) std::free(reinterpret_cast<void*>(mem));
    return 1;
}

extern "C" uint64_t host_HeapReAlloc(uint64_t heap, uint64_t flags,
                                     uint64_t mem, uint64_t bytes,
                                     uint64_t, uint64_t, uint64_t, uint64_t) {
    (void)heap; (void)flags;
    void* p = std::realloc(reinterpret_cast<void*>(mem),
                           static_cast<size_t>(bytes));
    return reinterpret_cast<uint64_t>(p);
}

extern "C" uint64_t host_GetProcessHeap(uint64_t, uint64_t, uint64_t, uint64_t,
                                        uint64_t, uint64_t, uint64_t,
                                        uint64_t) {
    return reinterpret_cast<uint64_t>(static_cast<void*>(&g_heap));
}

// ---- Synchronization ----
extern "C" uint64_t host_InitializeCriticalSection(uint64_t csPtr, uint64_t,
                                                   uint64_t, uint64_t,
                                                   uint64_t, uint64_t,
                                                   uint64_t, uint64_t) {
    (void)csPtr;
    return 0;
}

extern "C" uint64_t host_EnterCriticalSection(uint64_t csPtr, uint64_t,
                                              uint64_t, uint64_t,
                                              uint64_t, uint64_t,
                                              uint64_t, uint64_t) {
    (void)csPtr;
    return 0;
}

extern "C" uint64_t host_LeaveCriticalSection(uint64_t csPtr, uint64_t,
                                              uint64_t, uint64_t,
                                              uint64_t, uint64_t,
                                              uint64_t, uint64_t) {
    (void)csPtr;
    return 0;
}

extern "C" uint64_t host_DeleteCriticalSection(uint64_t csPtr, uint64_t,
                                               uint64_t, uint64_t,
                                               uint64_t, uint64_t,
                                               uint64_t, uint64_t) {
    (void)csPtr;
    return 0;
}

extern "C" uint64_t host_CreateEventA(uint64_t sa, uint64_t manualReset,
                                      uint64_t initialState, uint64_t name,
                                      uint64_t, uint64_t, uint64_t,
                                      uint64_t) {
    (void)sa; (void)manualReset; (void)initialState; (void)name;
    return allocHandle(2); // event handle
}

extern "C" uint64_t host_SetEvent(uint64_t event, uint64_t, uint64_t, uint64_t,
                                  uint64_t, uint64_t, uint64_t, uint64_t) {
    (void)event;
    return 1;
}

extern "C" uint64_t host_WaitForSingleObject(uint64_t handle, uint64_t timeout,
                                            uint64_t, uint64_t, uint64_t,
                                            uint64_t, uint64_t, uint64_t) {
    (void)handle; (void)timeout;
    return 0; // WAIT_OBJECT_0
}

extern "C" uint64_t host_CreateMutexA(uint64_t sa, uint64_t initialOwner,
                                      uint64_t name, uint64_t,
                                      uint64_t, uint64_t, uint64_t,
                                      uint64_t) {
    (void)sa; (void)initialOwner; (void)name;
    return allocHandle(3); // mutex handle
}

extern "C" uint64_t host_ReleaseMutex(uint64_t mutex, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t,
                                      uint64_t, uint64_t) {
    (void)mutex;
    return 1;
}

extern "C" uint64_t host_CreateSemaphoreA(uint64_t sa, uint64_t initialCount,
                                          uint64_t maxCount, uint64_t name,
                                          uint64_t, uint64_t, uint64_t,
                                          uint64_t) {
    (void)sa; (void)initialCount; (void)maxCount; (void)name;
    return allocHandle(4); // semaphore handle
}

extern "C" uint64_t host_ReleaseSemaphore(uint64_t sem, uint64_t count,
                                          uint64_t prevCount, uint64_t,
                                          uint64_t, uint64_t, uint64_t,
                                          uint64_t) {
    (void)sem; (void)count; (void)prevCount;
    return 1;
}

// ---- Dummy heap object ----
// (defined near top of file)

}  // namespace rottr

// ---- Process exit ----
std::atomic<bool> g_exitRequested{false};
uint32_t g_exitCode = 0;

extern "C" uint64_t host_ExitProcess(uint64_t exitCode, uint64_t, uint64_t,
                                     uint64_t, uint64_t, uint64_t,
                                     uint64_t, uint64_t) {
    g_exitCode = static_cast<uint32_t>(exitCode);
    g_exitRequested = true;
    return 0;
}

bool isExitRequested() { return g_exitRequested.load(); }
uint32_t getExitCode() { return g_exitCode; }

// =====================================================================
// Register host modules with their exports
// =====================================================================

namespace rottr {

void registerHostModules() {
    HostModule k32;
    k32.name = "kernel32.dll";
    k32.exportsByName = {
        {"GetLastError", reinterpret_cast<uint64_t>(&host_GetLastError)},
        {"SetLastError", reinterpret_cast<uint64_t>(&host_SetLastError)},
        {"ExitProcess", reinterpret_cast<uint64_t>(&host_ExitProcess)},
        {"GetTickCount", reinterpret_cast<uint64_t>(&host_GetTickCount)},
        {"GetTickCount64", reinterpret_cast<uint64_t>(&host_GetTickCount64)},
        {"QueryPerformanceCounter", reinterpret_cast<uint64_t>(&host_QueryPerformanceCounter)},
        {"QueryPerformanceFrequency", reinterpret_cast<uint64_t>(&host_QueryPerformanceFrequency)},
        {"Sleep", reinterpret_cast<uint64_t>(&host_Sleep)},
        {"GetCurrentProcessId", reinterpret_cast<uint64_t>(&host_GetCurrentProcessId)},
        {"GetCurrentThreadId", reinterpret_cast<uint64_t>(&host_GetCurrentThreadId)},
        {"GetCurrentProcess", reinterpret_cast<uint64_t>(&host_GetCurrentProcess)},
        {"GetCurrentThread", reinterpret_cast<uint64_t>(&host_GetCurrentThread)},
        {"GetCommandLineA", reinterpret_cast<uint64_t>(&host_GetCommandLineA)},
        {"GetModuleHandleA", reinterpret_cast<uint64_t>(&host_GetModuleHandleA)},
        {"GetProcAddress", reinterpret_cast<uint64_t>(&host_GetProcAddress)},
        {"LoadLibraryA", reinterpret_cast<uint64_t>(&host_LoadLibraryA)},
        {"VirtualAlloc", reinterpret_cast<uint64_t>(&host_VirtualAlloc)},
        {"VirtualFree", reinterpret_cast<uint64_t>(&host_VirtualFree)},
        {"VirtualProtect", reinterpret_cast<uint64_t>(&host_VirtualProtect)},
        {"VirtualQuery", reinterpret_cast<uint64_t>(&host_VirtualQuery)},
        {"CreateFileA", reinterpret_cast<uint64_t>(&host_CreateFileA)},
        {"ReadFile", reinterpret_cast<uint64_t>(&host_ReadFile)},
        {"WriteFile", reinterpret_cast<uint64_t>(&host_WriteFile)},
        {"CloseHandle", reinterpret_cast<uint64_t>(&host_CloseHandle)},
        {"SetFilePointer", reinterpret_cast<uint64_t>(&host_SetFilePointer)},
        {"HeapAlloc", reinterpret_cast<uint64_t>(&host_HeapAlloc)},
        {"HeapFree", reinterpret_cast<uint64_t>(&host_HeapFree)},
        {"HeapReAlloc", reinterpret_cast<uint64_t>(&host_HeapReAlloc)},
        {"GetProcessHeap", reinterpret_cast<uint64_t>(&host_GetProcessHeap)},
        {"InitializeCriticalSection", reinterpret_cast<uint64_t>(&host_InitializeCriticalSection)},
        {"EnterCriticalSection", reinterpret_cast<uint64_t>(&host_EnterCriticalSection)},
        {"LeaveCriticalSection", reinterpret_cast<uint64_t>(&host_LeaveCriticalSection)},
        {"DeleteCriticalSection", reinterpret_cast<uint64_t>(&host_DeleteCriticalSection)},
        {"CreateEventA", reinterpret_cast<uint64_t>(&host_CreateEventA)},
        {"SetEvent", reinterpret_cast<uint64_t>(&host_SetEvent)},
        {"WaitForSingleObject", reinterpret_cast<uint64_t>(&host_WaitForSingleObject)},
        {"CreateMutexA", reinterpret_cast<uint64_t>(&host_CreateMutexA)},
        {"ReleaseMutex", reinterpret_cast<uint64_t>(&host_ReleaseMutex)},
        {"CreateSemaphoreA", reinterpret_cast<uint64_t>(&host_CreateSemaphoreA)},
        {"ReleaseSemaphore", reinterpret_cast<uint64_t>(&host_ReleaseSemaphore)},
    };
    {
        std::lock_guard<std::mutex> lock(hostModulesMutex());
        hostModules().push_back(std::move(k32));
    }
    // Allocate guest call addresses for each host export so the IAT can
    // point to them.
    for (HostModule& mod : hostModules()) {
        for (auto& kv : mod.exportsByName) {
            uint64_t guestAddr = addHostCallProc(
                reinterpret_cast<HostProc>(kv.second));
            kv.second = guestAddr; // overwrite with guest call address
        }
        for (auto& kv : mod.exportsByOrdinal) {
            uint64_t guestAddr = addHostCallProc(
                reinterpret_cast<HostProc>(kv.second));
            kv.second = guestAddr;
        }
    }
}

}  // namespace rottr
