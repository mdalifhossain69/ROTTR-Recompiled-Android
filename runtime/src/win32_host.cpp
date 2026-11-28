// Host implementations of Windows API functions callable from guest code
// via the x64 call bridge. These run on the host and follow the HostProc
// signature: (rcx, rdx, r8, r9, a5, a6, a7, a8) -> rax.

#include "rottr/x64_bridge.h"
#include "rottr/guest_mem.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <memory>
#include <mutex>
#include <limits>
#include <pthread.h>
#include <semaphore.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <ctime>

namespace rottr {

struct EventEntry;
struct MutexEntry;
struct SemEntry;
extern std::map<uint64_t, std::shared_ptr<EventEntry>> g_events;
extern std::mutex g_eventsMutex;
extern std::map<uint64_t, std::shared_ptr<MutexEntry>> g_mutexes;
extern std::mutex g_mutexesMutex;
extern std::map<uint64_t, std::shared_ptr<SemEntry>> g_semaphores;
extern std::mutex g_semaphoresMutex;

namespace {

// ---- Minimal handle table ----
struct HandleEntry {
    uint64_t type;
    int32_t refCount = 1;
    bool signaled = false;
    FILE* file = nullptr;     // for file handles
    uint64_t offset = 0;      // current file offset
    std::string path;         // file path for handles backed by a file
};

std::map<uint64_t, HandleEntry> g_handles;
std::mutex g_handlesMutex;
uint64_t g_nextHandle = 0x1000;

uint64_t allocHandle(uint64_t type) {
    std::lock_guard<std::mutex> lock(g_handlesMutex);
    uint64_t h = g_nextHandle += 4;
    HandleEntry entry{};
    entry.type = type;
    g_handles.emplace(h, std::move(entry));
    return h;
}

uint64_t allocFileHandle(const char* path, const char* mode) {
    std::lock_guard<std::mutex> lock(g_handlesMutex);
    uint64_t h = g_nextHandle += 4;
    FILE* f = std::fopen(path, mode);
    if (!f) return 0;
    HandleEntry entry{};
    entry.type = 1;
    entry.file = f;
    entry.path = path;
    g_handles.emplace(h, std::move(entry));
    return h;
}

[[maybe_unused]] HandleEntry* findHandle(uint64_t h) {
    auto it = g_handles.find(h);
    return it == g_handles.end() ? nullptr : &it->second;
}
}  // namespace

// ---- Critical sections ----
std::map<uint64_t, pthread_mutex_t*> g_criticalSections;
std::mutex g_criticalSectionsMutex;

// ---- Events ----
struct EventEntry {
    bool manualReset = false;
    bool signaled = false;
    pthread_mutex_t mutex{};
    pthread_cond_t cond{};
    bool mutexInitialized = false;
    bool condInitialized = false;

    ~EventEntry() {
        if (condInitialized) pthread_cond_destroy(&cond);
        if (mutexInitialized) pthread_mutex_destroy(&mutex);
    }
};

std::map<uint64_t, std::shared_ptr<EventEntry>> g_events;
std::mutex g_eventsMutex;

// ---- Mutexes ----
struct MutexEntry {
    pthread_mutex_t native{};
    std::mutex stateMutex;
    pthread_t owner{};
    unsigned recursion = 0;
    bool owned = false;
    bool initialized = false;

    ~MutexEntry() {
        if (initialized) pthread_mutex_destroy(&native);
    }
};

std::map<uint64_t, std::shared_ptr<MutexEntry>> g_mutexes;
std::mutex g_mutexesMutex;

// ---- Semaphores ----
struct SemEntry {
    sem_t sem{};
    std::mutex releaseMutex;
    unsigned maxCount = 0;
    bool initialized = false;

    ~SemEntry() {
        if (initialized) sem_destroy(&sem);
    }
};

std::map<uint64_t, std::shared_ptr<SemEntry>> g_semaphores;
std::mutex g_semaphoresMutex;

// ---- Threads ----
struct ThreadInfo {
    uint64_t startAddr;
    uint64_t param;
};

std::map<uint64_t, pthread_t> g_threads;
std::mutex g_threadsMutex;

static void* threadTrampoline(void* arg) {
    ThreadInfo* info = static_cast<ThreadInfo*>(arg);
    if (info->startAddr) {
        typedef uint64_t (*ThreadProc)(uint64_t);
        ThreadProc proc = reinterpret_cast<ThreadProc>(info->startAddr);
        proc(info->param);
    }
    delete info;
    return nullptr;
}

// ---- Thread-local last error ----
thread_local uint32_t g_lastError = 0;

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

// ---- Monotonic clock helpers ----
namespace {
uint64_t steadyNanoseconds() {
    return static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
}
}  // namespace

extern "C" uint64_t host_GetTickCount(uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t, uint64_t) {
    return static_cast<uint32_t>(steadyNanoseconds() / 1000000ULL);
}

extern "C" uint64_t host_GetTickCount64(uint64_t, uint64_t, uint64_t, uint64_t,
                                        uint64_t, uint64_t, uint64_t, uint64_t) {
    return steadyNanoseconds() / 1000000ULL;
}

extern "C" uint64_t host_QueryPerformanceCounter(uint64_t lpCount, uint64_t, uint64_t,
                                                 uint64_t, uint64_t, uint64_t,
                                                 uint64_t, uint64_t) {
    if (!lpCount) return 0;
    uint64_t ticks = steadyNanoseconds();
    std::memcpy(reinterpret_cast<void*>(lpCount), &ticks, sizeof(ticks));
    return 1;
}

extern "C" uint64_t host_QueryPerformanceFrequency(uint64_t lpFreq, uint64_t, uint64_t,
                                                   uint64_t, uint64_t, uint64_t,
                                                   uint64_t, uint64_t) {
    if (!lpFreq) return 0;
    const uint64_t frequency = 1000000000ULL;  // nanoseconds
    std::memcpy(reinterpret_cast<void*>(lpFreq), &frequency, sizeof(frequency));
    return 1;
}

extern "C" uint64_t host_Sleep(uint64_t ms, uint64_t, uint64_t, uint64_t,
                               uint64_t, uint64_t, uint64_t, uint64_t) {
    struct timespec req;
    req.tv_sec = static_cast<time_t>(ms / 1000ULL);
    req.tv_nsec = static_cast<long>((ms % 1000ULL) * 1000000ULL);
    while (nanosleep(&req, &req) != 0) {
        // Resume after EINTR with the remaining time.
    }
    return 0;
}

extern "C" uint64_t host_GetCurrentProcessId(uint64_t, uint64_t, uint64_t, uint64_t,
                                            uint64_t, uint64_t, uint64_t, uint64_t) {
    return static_cast<uint64_t>(getpid());
}

extern "C" uint64_t host_GetCurrentThreadId(uint64_t, uint64_t, uint64_t, uint64_t,
                                           uint64_t, uint64_t, uint64_t, uint64_t) {
    return reinterpret_cast<uint64_t>(pthread_self());
}

extern "C" uint64_t host_CreateThread(uint64_t sa, uint64_t stackSize,
                                      uint64_t startAddr, uint64_t param,
                                      uint64_t flags, uint64_t threadIdPtr,
                                      uint64_t, uint64_t) {
    (void)sa; (void)stackSize; (void)flags; (void)threadIdPtr;
    uint64_t h = allocHandle(5); // thread handle
    ThreadInfo* info = new ThreadInfo{startAddr, param};
    pthread_t t;
    if (pthread_create(&t, nullptr, threadTrampoline, info) != 0) {
        delete info;
        return static_cast<uint64_t>(-1);
    }
    std::lock_guard<std::mutex> lock(g_threadsMutex);
    g_threads.emplace(h, t);
    return h;
}

extern "C" uint64_t host_ExitThread(uint64_t exitCode, uint64_t, uint64_t, uint64_t,
                                    uint64_t, uint64_t, uint64_t, uint64_t) {
    (void)exitCode;
    pthread_exit(nullptr);
    return 0; // unreachable
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
    (void)access; (void)share; (void)sa;
    (void)disp; (void)flags; (void)templateFile;
    const char* path = reinterpret_cast<const char*>(namePtr);
    if (!path) return allocHandle(1); // file handle without opening a file

    // Case-insensitive open: try exact path first, then scan directory.
    FILE* f = std::fopen(path, "rb");
    if (f) return allocFileHandle(path, "rb");

    // Split path into directory + filename.
    std::string fullPath(path);
    size_t lastSlash = fullPath.find_last_of("/\\");
    std::string dir = (lastSlash != std::string::npos)
        ? fullPath.substr(0, lastSlash + 1) : "./";
    std::string fileName = (lastSlash != std::string::npos)
        ? fullPath.substr(lastSlash + 1) : fullPath;

    // Scan directory for case-insensitive match.
    DIR* dirp = opendir(dir.c_str());
    if (!dirp) return static_cast<uint64_t>(-1);
    struct dirent* entry;
    std::string matchedPath;
    while ((entry = readdir(dirp)) != nullptr) {
        if (entry->d_name[0] == 0) continue;
        if (strcasecmp(entry->d_name, fileName.c_str()) == 0) {
            matchedPath = dir + entry->d_name;
            break;
        }
    }
    closedir(dirp);
    if (matchedPath.empty()) return static_cast<uint64_t>(-1);

    f = std::fopen(matchedPath.c_str(), "rb");
    if (!f) return static_cast<uint64_t>(-1);
    return allocFileHandle(matchedPath.c_str(), "rb");
}

extern "C" uint64_t host_ReadFile(uint64_t handle, uint64_t buffer,
                                  uint64_t bytesToRead, uint64_t bytesRead,
                                  uint64_t overlapped, uint64_t, uint64_t,
                                  uint64_t) {
    (void)bytesRead; (void)overlapped;
    std::lock_guard<std::mutex> lock(g_handlesMutex);
    auto it = g_handles.find(handle);
    if (it == g_handles.end() || !it->second.file) return 0; // FALSE
    HandleEntry& e = it->second;
    std::fseek(e.file, static_cast<long>(e.offset), SEEK_SET);
    uint8_t* dst = reinterpret_cast<uint8_t*>(buffer);
    size_t got = std::fread(dst, 1, static_cast<size_t>(bytesToRead), e.file);
    e.offset += got;
    if (bytesRead) {
        // write bytesRead count back to guest memory via buffer pointer
        // (simplified: caller reads from return value)
    }
    return static_cast<uint64_t>(got > 0 ? 1 : 0); // TRUE/FALSE
}

extern "C" uint64_t host_WriteFile(uint64_t handle, uint64_t buffer,
                                   uint64_t bytesToWrite, uint64_t bytesWritten,
                                   uint64_t overlapped, uint64_t, uint64_t,
                                   uint64_t) {
    (void)bytesWritten; (void)overlapped;
    std::lock_guard<std::mutex> lock(g_handlesMutex);
    auto it = g_handles.find(handle);
    if (it == g_handles.end() || !it->second.file) return 0; // FALSE
    HandleEntry& e = it->second;
    std::fseek(e.file, static_cast<long>(e.offset), SEEK_SET);
    const uint8_t* src = reinterpret_cast<const uint8_t*>(buffer);
    size_t wrote = std::fwrite(src, 1, static_cast<size_t>(bytesToWrite), e.file);
    e.offset += wrote;
    return static_cast<uint64_t>(wrote > 0 ? 1 : 0); // TRUE/FALSE
}

// ---- Memory-mapped file bookkeeping (declared early: CloseHandle uses it) ----
struct MappingEntry {
    int fd = -1;
    size_t size = 0;
    bool writable = false;
    bool anonymous = false;
};

std::map<uint64_t, MappingEntry> g_mappings;
std::mutex g_mappingsMutex;
uint64_t g_nextMapping = 0x2000000;

struct ViewEntry {
    void* address = nullptr;
    size_t size = 0;
    uint64_t mapping = 0;
};

std::map<void*, ViewEntry> g_views;
std::mutex g_viewsMutex;

extern "C" uint64_t host_CloseHandle(uint64_t handle, uint64_t, uint64_t,
                                     uint64_t, uint64_t, uint64_t,
                                     uint64_t, uint64_t) {
    // File mappings live in their own table: unmap their views, close the fd.
    {
        std::lock_guard<std::mutex> lock(g_mappingsMutex);
        auto mit = g_mappings.find(handle);
        if (mit != g_mappings.end()) {
            {
                std::lock_guard<std::mutex> vlock(g_viewsMutex);
                for (auto vit = g_views.begin(); vit != g_views.end();) {
                    if (vit->second.mapping == handle) {
                        munmap(vit->second.address, vit->second.size);
                        vit = g_views.erase(vit);
                    } else {
                        ++vit;
                    }
                }
            }
            if (mit->second.fd >= 0) close(mit->second.fd);
            g_mappings.erase(mit);
            return 1;
        }
    }

    uint64_t type = 0;
    {
        std::lock_guard<std::mutex> lock(g_handlesMutex);
        auto it = g_handles.find(handle);
        if (it == g_handles.end()) return 0; // FALSE: invalid handle
        type = it->second.type;
        if (it->second.file) std::fclose(it->second.file);
        g_handles.erase(it);
    }

    if (type == 2) {
        std::lock_guard<std::mutex> lock(g_eventsMutex);
        if (g_events.erase(handle) == 0) return 0;
    } else if (type == 3) {
        std::lock_guard<std::mutex> lock(g_mutexesMutex);
        if (g_mutexes.erase(handle) == 0) return 0;
    } else if (type == 4) {
        std::lock_guard<std::mutex> lock(g_semaphoresMutex);
        if (g_semaphores.erase(handle) == 0) return 0;
    }

    // If this was a thread handle, detach the pthread if still running.
    {
        std::lock_guard<std::mutex> tlock(g_threadsMutex);
        auto tit = g_threads.find(handle);
        if (tit != g_threads.end()) {
            pthread_detach(tit->second);
            g_threads.erase(tit);
        }
    }

    return 1; // TRUE
}

extern "C" uint64_t host_SetFilePointer(uint64_t handle, uint64_t distance,
                                        uint64_t high, uint64_t method,
                                        uint64_t, uint64_t, uint64_t,
                                        uint64_t) {
    (void)handle; (void)distance; (void)high; (void)method;
    std::lock_guard<std::mutex> lock(g_handlesMutex);
    auto it = g_handles.find(handle);
    if (it == g_handles.end() || !it->second.file)
        return static_cast<uint64_t>(-1); // INVALID_SET_FILE_POINTER
    HandleEntry& e = it->second;
    // Combine high and low for 64-bit distance.
    int64_t dist = static_cast<int64_t>(distance) |
                   (static_cast<int64_t>(high) << 32);
    int origin = SEEK_SET;
    switch (method) {
        case 0: origin = SEEK_SET; break; // FILE_BEGIN
        case 1: origin = SEEK_CUR; break; // FILE_CURRENT
        case 2: origin = SEEK_END; break; // FILE_END
        default: return static_cast<uint64_t>(-1);
    }
    if (std::fseek(e.file, static_cast<long>(dist), origin) != 0)
        return static_cast<uint64_t>(-1);
    long pos = std::ftell(e.file);
    e.offset = static_cast<uint64_t>(pos);
    return e.offset;
}

// ---- Directory listing (FindFirstFileA / FindNextFileA / FindClose) ----
extern "C" uint64_t host_FindNextFileA(uint64_t handle, uint64_t findDataPtr,
                                       uint64_t, uint64_t, uint64_t,
                                       uint64_t, uint64_t, uint64_t);
extern "C" uint64_t host_FindClose(uint64_t handle, uint64_t, uint64_t,
                                   uint64_t, uint64_t, uint64_t,
                                   uint64_t, uint64_t);

struct FindEntry {
    DIR* dir = nullptr;
    std::string currentName;
};

std::map<uint64_t, FindEntry> g_findHandles;
std::mutex g_findMutex;
uint64_t g_nextFindHandle = 0x1000000;

extern "C" uint64_t host_FindFirstFileA(uint64_t pathPtr, uint64_t findDataPtr,
                                        uint64_t, uint64_t, uint64_t,
                                        uint64_t, uint64_t, uint64_t) {
    const char* path = reinterpret_cast<const char*>(pathPtr);
    if (!path) return static_cast<uint64_t>(-1);
    DIR* dir = opendir(path);
    if (!dir) return static_cast<uint64_t>(-1);
    std::lock_guard<std::mutex> lock(g_findMutex);
    uint64_t h = g_nextFindHandle += 4;
    g_findHandles.emplace(h, FindEntry{dir, {}});
    // Read first entry directly (avoid calling FindNextFileA which would
    // deadlock on g_findMutex). Skip "." and "..".
    struct dirent* entry = readdir(dir);
    while (entry && (std::strcmp(entry->d_name, ".") == 0 ||
                     std::strcmp(entry->d_name, "..") == 0))
        entry = readdir(dir);
    if (entry) {
        FindEntry& fe = g_findHandles[h];
        fe.currentName = entry->d_name;
        if (findDataPtr) {
            uint8_t* fd = reinterpret_cast<uint8_t*>(findDataPtr);
            uint32_t attrs = (entry->d_type == DT_DIR) ? 0x10u : 0x20u;
            std::memcpy(fd, &attrs, 4);
            std::memset(fd + 4, 0, 24);
            std::memset(fd + 28, 0, 8);
            size_t nameLen = fe.currentName.size();
            std::memcpy(fd + 36, fe.currentName.c_str(),
                        nameLen < 260 ? nameLen : 259);
            fd[36 + (nameLen < 260 ? nameLen : 259)] = 0;
            std::memset(fd + 296, 0, 14);
        }
    }
    return h;
}

extern "C" uint64_t host_FindNextFileA(uint64_t handle, uint64_t findDataPtr,
                                       uint64_t, uint64_t, uint64_t,
                                       uint64_t, uint64_t, uint64_t) {
    std::lock_guard<std::mutex> lock(g_findMutex);
    auto it = g_findHandles.find(handle);
    if (it == g_findHandles.end()) return 0;
    FindEntry& fe = it->second;
    struct dirent* entry = readdir(fe.dir);
    // Skip "." and ".." entries.
    while (entry && (std::strcmp(entry->d_name, ".") == 0 ||
                     std::strcmp(entry->d_name, "..") == 0))
        entry = readdir(fe.dir);
    if (!entry) return 0; // no more entries
    fe.currentName = entry->d_name;
    if (!findDataPtr) return 1;
    uint8_t* fd = reinterpret_cast<uint8_t*>(findDataPtr);
    // dwFileAttributes: FILE_ATTRIBUTE_DIRECTORY if dir, else FILE_ATTRIBUTE_ARCHIVE
    uint32_t attrs = (entry->d_type == DT_DIR) ? 0x10u : 0x20u;
    std::memcpy(fd, &attrs, 4);
    // ftCreationTime, ftLastAccessTime, ftLastWriteTime: 24 bytes zero
    std::memset(fd + 4, 0, 24);
    // nFileSizeHigh (4), nFileSizeLow (4): zero
    std::memset(fd + 28, 0, 8);
    // cFileName [260] at offset 36
    size_t nameLen = fe.currentName.size();
    std::memcpy(fd + 36, fe.currentName.c_str(),
                nameLen < 260 ? nameLen : 259);
    fd[36 + (nameLen < 260 ? nameLen : 259)] = 0;
    // cAlternateFileName [14] at offset 296: zero
    std::memset(fd + 296, 0, 14);
    return 1;
}

extern "C" uint64_t host_FindClose(uint64_t handle, uint64_t, uint64_t,
                                   uint64_t, uint64_t, uint64_t,
                                   uint64_t, uint64_t) {
    std::lock_guard<std::mutex> lock(g_findMutex);
    auto it = g_findHandles.find(handle);
    if (it == g_findHandles.end()) return 0;
    if (it->second.dir) closedir(it->second.dir);
    g_findHandles.erase(it);
    return 1;
}

// ---- Memory-mapped files (CreateFileMappingA / MapViewOfFile) ----
extern "C" uint64_t host_CreateFileMappingA(uint64_t file, uint64_t, uint64_t protect,
                                            uint64_t maxHigh, uint64_t maxLow,
                                            uint64_t, uint64_t, uint64_t) {
    // PAGE_READWRITE=0x04, PAGE_WRITECOPY=0x08, PAGE_EXECUTE_READWRITE=0x40
    const bool writable = (protect & 0x04U) || (protect & 0x08U) || (protect & 0x40U);
    const uint64_t requested = (maxHigh << 32) | (maxLow & 0xffffffffULL);
    const bool anonymous = (file == 0 || file == static_cast<uint64_t>(-1));

    std::string path;
    if (!anonymous) {
        std::lock_guard<std::mutex> lock(g_handlesMutex);
        auto it = g_handles.find(file);
        if (it == g_handles.end() || it->second.path.empty()) return 0;
        path = it->second.path;
    }

    int fd = -1;
    size_t size = static_cast<size_t>(requested);
    if (!anonymous) {
        fd = open(path.c_str(), writable ? O_RDWR : O_RDONLY);
        if (fd < 0) return 0;
        struct stat st{};
        if (fstat(fd, &st) != 0) { close(fd); return 0; }
        if (size == 0) size = static_cast<size_t>(st.st_size);
        if (writable && size > static_cast<size_t>(st.st_size)) {
            if (ftruncate(fd, static_cast<off_t>(size)) != 0) { close(fd); return 0; }
        }
    }
    if (size == 0) size = 4096;

    std::lock_guard<std::mutex> lock(g_mappingsMutex);
    uint64_t h = g_nextMapping += 4;
    MappingEntry entry;
    entry.fd = fd;
    entry.size = size;
    entry.writable = writable;
    entry.anonymous = anonymous;
    g_mappings.emplace(h, entry);
    return h;
}

extern "C" uint64_t host_MapViewOfFile(uint64_t mapping, uint64_t,
                                       uint64_t offsetHigh, uint64_t offsetLow,
                                       uint64_t bytes, uint64_t, uint64_t,
                                       uint64_t) {
    MappingEntry entry;
    {
        std::lock_guard<std::mutex> lock(g_mappingsMutex);
        auto it = g_mappings.find(mapping);
        if (it == g_mappings.end()) return 0;
        entry = it->second;
    }
    const uint64_t offset = (offsetHigh << 32) | (offsetLow & 0xffffffffULL);
    long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) pageSize = 4096;
    if (offset % static_cast<uint64_t>(pageSize) != 0) return 0;
    if (offset >= entry.size) return 0;
    const size_t length = bytes ? static_cast<size_t>(bytes)
                                : entry.size - static_cast<size_t>(offset);
    const int prot = PROT_READ | (entry.writable ? PROT_WRITE : 0);
    void* addr = nullptr;
    if (entry.anonymous) {
        addr = mmap(nullptr, length, prot, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    } else {
        addr = mmap(nullptr, length, prot, MAP_SHARED, entry.fd,
                    static_cast<off_t>(offset));
    }
    if (addr == MAP_FAILED) return 0;
    std::lock_guard<std::mutex> lock(g_viewsMutex);
    g_views.emplace(addr, ViewEntry{addr, length, mapping});
    return reinterpret_cast<uint64_t>(addr);
}

extern "C" uint64_t host_UnmapViewOfFile(uint64_t address, uint64_t, uint64_t,
                                         uint64_t, uint64_t, uint64_t,
                                         uint64_t, uint64_t) {
    void* addr = reinterpret_cast<void*>(address);
    std::lock_guard<std::mutex> lock(g_viewsMutex);
    auto it = g_views.find(addr);
    if (it == g_views.end()) return 0;
    munmap(addr, it->second.size);
    g_views.erase(it);
    return 1;
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
                                                   uint64_t, uint64_t, uint64_t,
                                                   uint64_t) {
    pthread_mutex_t* m = new pthread_mutex_t();
    pthread_mutex_init(m, nullptr);
    std::lock_guard<std::mutex> lock(g_criticalSectionsMutex);
    g_criticalSections.emplace(csPtr, m);
    return 1;
}

extern "C" uint64_t host_EnterCriticalSection(uint64_t csPtr, uint64_t,
                                              uint64_t, uint64_t,
                                              uint64_t, uint64_t, uint64_t,
                                              uint64_t) {
    pthread_mutex_t* m = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_criticalSectionsMutex);
        auto it = g_criticalSections.find(csPtr);
        if (it == g_criticalSections.end()) return 0;
        m = it->second;
    }
    pthread_mutex_lock(m);
    return 1;
}

extern "C" uint64_t host_LeaveCriticalSection(uint64_t csPtr, uint64_t,
                                              uint64_t, uint64_t,
                                              uint64_t, uint64_t, uint64_t,
                                              uint64_t) {
    pthread_mutex_t* m = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_criticalSectionsMutex);
        auto it = g_criticalSections.find(csPtr);
        if (it == g_criticalSections.end()) return 0;
        m = it->second;
    }
    pthread_mutex_unlock(m);
    return 1;
}

extern "C" uint64_t host_DeleteCriticalSection(uint64_t csPtr, uint64_t,
                                               uint64_t, uint64_t,
                                               uint64_t, uint64_t, uint64_t,
                                               uint64_t) {
    std::lock_guard<std::mutex> lock(g_criticalSectionsMutex);
    auto it = g_criticalSections.find(csPtr);
    if (it == g_criticalSections.end()) return 0;
    pthread_mutex_destroy(it->second);
    delete it->second;
    g_criticalSections.erase(it);
    return 1;
}

extern "C" uint64_t host_CreateEventA(uint64_t sa, uint64_t manualReset,
                                      uint64_t initialState, uint64_t name,
                                      uint64_t, uint64_t, uint64_t,
                                      uint64_t) {
    (void)sa; (void)name;
    auto event = std::make_shared<EventEntry>();
    event->manualReset = manualReset != 0;
    event->signaled = initialState != 0;
    if (pthread_mutex_init(&event->mutex, nullptr) != 0) return 0;
    event->mutexInitialized = true;
    if (pthread_cond_init(&event->cond, nullptr) != 0) return 0;
    event->condInitialized = true;
    const uint64_t h = allocHandle(2);
    std::lock_guard<std::mutex> lock(g_eventsMutex);
    g_events.emplace(h, std::move(event));
    return h;
}

extern "C" uint64_t host_SetEvent(uint64_t event, uint64_t, uint64_t, uint64_t,
                                  uint64_t, uint64_t, uint64_t, uint64_t) {
    std::shared_ptr<EventEntry> entry;
    {
        std::lock_guard<std::mutex> lock(g_eventsMutex);
        auto it = g_events.find(event);
        if (it == g_events.end()) return 0;
        entry = it->second;
    }
    pthread_mutex_lock(&entry->mutex);
    entry->signaled = true;
    pthread_cond_broadcast(&entry->cond);
    pthread_mutex_unlock(&entry->mutex);
    return 1;
}

extern "C" uint64_t host_WaitForSingleObject(uint64_t handle, uint64_t timeout,
                                             uint64_t, uint64_t, uint64_t,
                                             uint64_t, uint64_t, uint64_t) {
    constexpr uint64_t kInfinite = 0xffffffffULL;
    constexpr uint64_t kWaitTimeout = 0x102ULL;
    constexpr uint64_t kWaitFailed = 0xffffffffULL;

    timespec deadline{};
    if (timeout != 0 && timeout != kInfinite) {
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += static_cast<time_t>(timeout / 1000U);
        deadline.tv_nsec += static_cast<long>((timeout % 1000U) * 1000000U);
        if (deadline.tv_nsec >= 1000000000L) {
            ++deadline.tv_sec;
            deadline.tv_nsec -= 1000000000L;
        }
    }

    // Try event first.
    {
        std::shared_ptr<EventEntry> entry;
        {
            std::lock_guard<std::mutex> lock(g_eventsMutex);
            auto it = g_events.find(handle);
            if (it != g_events.end()) entry = it->second;
        }
        if (entry) {
            pthread_mutex_lock(&entry->mutex);
            while (!entry->signaled) {
                const int waitResult = timeout == kInfinite
                        ? pthread_cond_wait(&entry->cond, &entry->mutex)
                        : (timeout == 0
                                ? ETIMEDOUT
                                : pthread_cond_timedwait(&entry->cond,
                                                         &entry->mutex,
                                                         &deadline));
                if (waitResult == ETIMEDOUT) {
                    pthread_mutex_unlock(&entry->mutex);
                    return kWaitTimeout;
                }
                if (waitResult != 0) {
                    pthread_mutex_unlock(&entry->mutex);
                    return kWaitFailed;
                }
            }
            if (!entry->manualReset) entry->signaled = false;
            pthread_mutex_unlock(&entry->mutex);
            return 0; // WAIT_OBJECT_0
        }
    }

    // Try mutex
    {
        std::shared_ptr<MutexEntry> entry;
        {
            std::lock_guard<std::mutex> lock(g_mutexesMutex);
            auto it = g_mutexes.find(handle);
            if (it != g_mutexes.end()) entry = it->second;
        }
        if (entry) {
            int lockResult = 0;
            if (timeout == 0) {
                lockResult = pthread_mutex_trylock(&entry->native);
            } else if (timeout == kInfinite) {
                lockResult = pthread_mutex_lock(&entry->native);
            } else {
                lockResult = pthread_mutex_timedlock(&entry->native, &deadline);
            }
            if (lockResult == EBUSY || lockResult == ETIMEDOUT)
                return kWaitTimeout;
            if (lockResult != 0) return kWaitFailed;
            {
                std::lock_guard<std::mutex> lock(entry->stateMutex);
                const pthread_t self = pthread_self();
                if (entry->owned && pthread_equal(entry->owner, self)) {
                    ++entry->recursion;
                } else {
                    entry->owned = true;
                    entry->owner = self;
                    entry->recursion = 1;
                }
            }
            return 0; // WAIT_OBJECT_0
        }
    }

    // Try semaphore
    {
        std::shared_ptr<SemEntry> entry;
        {
            std::lock_guard<std::mutex> lock(g_semaphoresMutex);
            auto it = g_semaphores.find(handle);
            if (it != g_semaphores.end()) entry = it->second;
        }
        if (entry) {
            int waitResult = 0;
            if (timeout == 0) {
                waitResult = sem_trywait(&entry->sem);
            } else if (timeout == kInfinite) {
                waitResult = sem_wait(&entry->sem);
            } else {
                waitResult = sem_timedwait(&entry->sem, &deadline);
            }
            if (waitResult == 0) return 0;
            if (errno == EAGAIN || errno == ETIMEDOUT) return kWaitTimeout;
            return kWaitFailed;
        }
    }

    // Try thread — join until it exits
    {
        pthread_t* t = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_threadsMutex);
            auto it = g_threads.find(handle);
            if (it != g_threads.end()) t = &it->second;
        }
        if (t) {
            pthread_join(*t, nullptr);
            std::lock_guard<std::mutex> lock(g_threadsMutex);
            g_threads.erase(handle);
            return 0; // WAIT_OBJECT_0
        }
    }

    // File handle — always signaled
    {
        std::lock_guard<std::mutex> lock(g_handlesMutex);
        if (g_handles.count(handle)) return 0;
    }

    return kWaitFailed;
}

extern "C" uint64_t host_CreateMutexA(uint64_t sa, uint64_t initialOwner,
                                      uint64_t name, uint64_t,
                                      uint64_t, uint64_t, uint64_t, uint64_t) {
    (void)sa; (void)name;
    auto entry = std::make_shared<MutexEntry>();
    pthread_mutexattr_t attributes;
    if (pthread_mutexattr_init(&attributes) != 0) return 0;
    const int typeResult =
            pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);
    const int initResult = typeResult == 0
            ? pthread_mutex_init(&entry->native, &attributes)
            : typeResult;
    pthread_mutexattr_destroy(&attributes);
    if (initResult != 0) return 0;
    entry->initialized = true;
    if (initialOwner != 0) {
        if (pthread_mutex_lock(&entry->native) != 0) return 0;
        entry->owner = pthread_self();
        entry->recursion = 1;
        entry->owned = true;
    }
    const uint64_t h = allocHandle(3);
    std::lock_guard<std::mutex> lock(g_mutexesMutex);
    g_mutexes.emplace(h, std::move(entry));
    return h;
}

extern "C" uint64_t host_ReleaseMutex(uint64_t mutex, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t) {
    std::shared_ptr<MutexEntry> entry;
    {
        std::lock_guard<std::mutex> lock(g_mutexesMutex);
        auto it = g_mutexes.find(mutex);
        if (it == g_mutexes.end()) {
            host_SetLastError(6, 0, 0, 0, 0, 0, 0, 0);
            return 0;
        }
        entry = it->second;
    }
    std::lock_guard<std::mutex> lock(entry->stateMutex);
    if (!entry->owned || !pthread_equal(entry->owner, pthread_self())) {
        host_SetLastError(288, 0, 0, 0, 0, 0, 0, 0);
        return 0;
    }
    --entry->recursion;
    if (entry->recursion == 0) entry->owned = false;
    if (pthread_mutex_unlock(&entry->native) != 0) {
        host_SetLastError(288, 0, 0, 0, 0, 0, 0, 0);
        return 0;
    }
    return 1;
}

extern "C" uint64_t host_CreateSemaphoreA(uint64_t sa, uint64_t initialCount,
                                          uint64_t maxCount, uint64_t name,
                                          uint64_t, uint64_t, uint64_t,
                                          uint64_t) {
    (void)sa; (void)name;
    if (maxCount == 0 || initialCount > maxCount ||
        maxCount > std::numeric_limits<unsigned>::max())
        return 0;
    auto entry = std::make_shared<SemEntry>();
    if (sem_init(&entry->sem, 0, static_cast<unsigned>(initialCount)) != 0)
        return 0;
    entry->initialized = true;
    entry->maxCount = static_cast<unsigned>(maxCount);
    const uint64_t h = allocHandle(4);
    std::lock_guard<std::mutex> lock(g_semaphoresMutex);
    g_semaphores.emplace(h, std::move(entry));
    return h;
}

extern "C" uint64_t host_ReleaseSemaphore(uint64_t sem, uint64_t count,
                                          uint64_t prevCount, uint64_t,
                                          uint64_t, uint64_t, uint64_t,
                                          uint64_t) {
    std::shared_ptr<SemEntry> entry;
    {
        std::lock_guard<std::mutex> lock(g_semaphoresMutex);
        auto it = g_semaphores.find(sem);
        if (it == g_semaphores.end()) return 0;
        entry = it->second;
    }
    if (count == 0 || count > entry->maxCount) return 0;
    std::lock_guard<std::mutex> lock(entry->releaseMutex);
    int currentCount = 0;
    if (sem_getvalue(&entry->sem, &currentCount) != 0 ||
        currentCount < 0 ||
        static_cast<uint64_t>(currentCount) + count > entry->maxCount)
        return 0;
    if (prevCount != 0)
        *reinterpret_cast<uint32_t*>(prevCount) =
                static_cast<uint32_t>(currentCount);
    for (uint64_t i = 0; i < count; ++i) {
        if (sem_post(&entry->sem) != 0) return 0;
    }
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

// =====================================================================
// Register host modules with their exports
// =====================================================================

namespace rottr {

bool isExitRequested() { return g_exitRequested.load(); }
uint32_t getExitCode() { return g_exitCode; }

void registerRegistryHostModule();
void registerWindowHostModule();
void registerComHostModule();
void registerNetHostModule();
void registerSystemHostModule();

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
        {"CreateThread", reinterpret_cast<uint64_t>(&host_CreateThread)},
        {"ExitThread", reinterpret_cast<uint64_t>(&host_ExitThread)},
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
        {"FindFirstFileA", reinterpret_cast<uint64_t>(&host_FindFirstFileA)},
        {"FindNextFileA", reinterpret_cast<uint64_t>(&host_FindNextFileA)},
        {"FindClose", reinterpret_cast<uint64_t>(&host_FindClose)},
        {"CreateFileMappingA", reinterpret_cast<uint64_t>(&host_CreateFileMappingA)},
        {"MapViewOfFile", reinterpret_cast<uint64_t>(&host_MapViewOfFile)},
        {"UnmapViewOfFile", reinterpret_cast<uint64_t>(&host_UnmapViewOfFile)},
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
    registerRegistryHostModule();
    registerWindowHostModule();
    registerComHostModule();
    registerNetHostModule();
    registerSystemHostModule();
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
