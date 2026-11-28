// Roadmap step 44: system APIs the game calls. All values come from the host
// or from documented defaults; no call blocks on the network.

#include "rottr/x64_bridge.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/statvfs.h>
#include <unistd.h>

namespace rottr {
namespace {

constexpr uint32_t kErrorSuccess = 0;
constexpr uint32_t kErrorFileNotFound = 2;

void writeU16(uint8_t* base, size_t offset, uint16_t value) {
    std::memcpy(base + offset, &value, sizeof(value));
}

void writeU32(uint8_t* base, size_t offset, uint32_t value) {
    std::memcpy(base + offset, &value, sizeof(value));
}

void writeU64(uint8_t* base, size_t offset, uint64_t value) {
    std::memcpy(base + offset, &value, sizeof(value));
}

void writeSystemTime(uint8_t* base, const struct tm& value) {
    writeU16(base, 0, static_cast<uint16_t>(value.tm_year + 1900));
    writeU16(base, 2, static_cast<uint16_t>(value.tm_mon + 1));
    writeU16(base, 4, static_cast<uint16_t>(value.tm_wday));
    writeU16(base, 6, static_cast<uint16_t>(value.tm_mday));
    writeU16(base, 8, static_cast<uint16_t>(value.tm_hour));
    writeU16(base, 10, static_cast<uint16_t>(value.tm_min));
    writeU16(base, 12, static_cast<uint16_t>(value.tm_sec));
    writeU16(base, 14, 0);
}

uint32_t copyName(const char* source, uint64_t buffer, uint64_t sizePtr) {
    if (sizePtr == 0) return 0;
    uint32_t capacity = *reinterpret_cast<uint32_t*>(sizePtr);
    const uint32_t required = static_cast<uint32_t>(std::strlen(source) + 1);
    if (buffer == 0 || capacity < required) {
        *reinterpret_cast<uint32_t*>(sizePtr) = required;
        return 0;
    }
    std::memcpy(reinterpret_cast<void*>(buffer), source, required);
    *reinterpret_cast<uint32_t*>(sizePtr) = required - 1;
    return 1;
}

}  // namespace

extern "C" uint64_t host_GetSystemInfo(uint64_t infoPtr, uint64_t, uint64_t,
                                       uint64_t, uint64_t, uint64_t, uint64_t,
                                       uint64_t) {
    if (infoPtr == 0) return 0;
    auto* info = reinterpret_cast<uint8_t*>(infoPtr);
    std::memset(info, 0, 48);
    writeU16(info, 0, 9);   // PROCESSOR_ARCHITECTURE_AMD64
    writeU32(info, 4, 4096);  // dwPageSize
    writeU64(info, 8, 0x10000ULL);
    writeU64(info, 16, 0x7ffffeffffULL);
    const long processors = sysconf(_SC_NPROCESSORS_ONLN);
    const uint32_t count = processors > 0 ? static_cast<uint32_t>(processors) : 1;
    writeU64(info, 24, count >= 64 ? ~0ULL : ((1ULL << count) - 1ULL));
    writeU32(info, 32, count);
    writeU32(info, 36, 8664);       // PROCESSOR_AMD_X8664
    writeU32(info, 40, 65536);      // dwAllocationGranularity
    writeU16(info, 44, 6);          // wProcessorLevel
    writeU16(info, 46, 0x3a09);     // wProcessorRevision
    return 0;
}

extern "C" uint64_t host_GetVersionExA(uint64_t versionPtr, uint64_t, uint64_t,
                                       uint64_t, uint64_t, uint64_t, uint64_t,
                                       uint64_t) {
    if (versionPtr == 0) return 0;
    auto* version = reinterpret_cast<uint8_t*>(versionPtr);
    writeU32(version, 4, 10);
    writeU32(version, 8, 0);
    writeU32(version, 12, 19045);
    writeU32(version, 16, 2);  // VER_PLATFORM_WIN32_NT
    return 1;
}

extern "C" uint64_t host_GetComputerNameA(uint64_t buffer, uint64_t sizePtr,
                                          uint64_t, uint64_t, uint64_t, uint64_t,
                                          uint64_t, uint64_t) {
    char name[256] = {};
    if (gethostname(name, sizeof(name) - 1) != 0 || name[0] == '\0') {
        std::snprintf(name, sizeof(name), "android");
    }
    return copyName(name, buffer, sizePtr);
}

extern "C" uint64_t host_GetUserNameA(uint64_t buffer, uint64_t sizePtr, uint64_t,
                                      uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t) {
    const char* user = getlogin();
    if (user == nullptr || user[0] == '\0') user = getenv("USER");
    if (user == nullptr || user[0] == '\0') user = "player";
    return copyName(user, buffer, sizePtr);
}

extern "C" uint64_t host_GetSystemTime(uint64_t timePtr, uint64_t, uint64_t,
                                       uint64_t, uint64_t, uint64_t, uint64_t,
                                       uint64_t) {
    if (timePtr == 0) return 0;
    const std::time_t now = std::time(nullptr);
    struct tm value{};
    gmtime_r(&now, &value);
    writeSystemTime(reinterpret_cast<uint8_t*>(timePtr), value);
    return 0;
}

extern "C" uint64_t host_GetLocalTime(uint64_t timePtr, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t) {
    if (timePtr == 0) return 0;
    const std::time_t now = std::time(nullptr);
    struct tm value{};
    localtime_r(&now, &value);
    writeSystemTime(reinterpret_cast<uint8_t*>(timePtr), value);
    return 0;
}

extern "C" uint64_t host_GetSystemTimeAsFileTime(uint64_t fileTimePtr, uint64_t,
                                                 uint64_t, uint64_t, uint64_t,
                                                 uint64_t, uint64_t, uint64_t) {
    if (fileTimePtr == 0) return 0;
    const uint64_t unixSeconds = static_cast<uint64_t>(std::time(nullptr));
    const uint64_t value = (unixSeconds + 11644473600ULL) * 10000000ULL;
    writeU64(reinterpret_cast<uint8_t*>(fileTimePtr), 0, value);
    return 0;
}

extern "C" uint64_t host_GetTimeZoneInformation(uint64_t infoPtr, uint64_t,
                                                uint64_t, uint64_t, uint64_t,
                                                uint64_t, uint64_t, uint64_t) {
    if (infoPtr != 0) std::memset(reinterpret_cast<void*>(infoPtr), 0, 172);
    return 0;  // TIME_ZONE_ID_UNKNOWN
}

extern "C" uint64_t host_GetDriveTypeA(uint64_t, uint64_t, uint64_t, uint64_t,
                                       uint64_t, uint64_t, uint64_t, uint64_t) {
    return 3;  // DRIVE_FIXED
}

extern "C" uint64_t host_GetDiskFreeSpaceExA(uint64_t pathPtr, uint64_t freePtr,
                                             uint64_t totalPtr, uint64_t totalFreePtr,
                                             uint64_t, uint64_t, uint64_t,
                                             uint64_t) {
    const char* path = reinterpret_cast<const char*>(pathPtr);
    struct statvfs stats{};
    if (statvfs(path != nullptr && path[0] != '\0' ? path : "/", &stats) != 0) {
        return 0;  // caller observes GetLastError from the host layer
    }
    const uint64_t total = static_cast<uint64_t>(stats.f_blocks) * stats.f_frsize;
    const uint64_t free = static_cast<uint64_t>(stats.f_bavail) * stats.f_frsize;
    if (freePtr != 0) writeU64(reinterpret_cast<uint8_t*>(freePtr), 0, free);
    if (totalPtr != 0) writeU64(reinterpret_cast<uint8_t*>(totalPtr), 0, total);
    if (totalFreePtr != 0) writeU64(reinterpret_cast<uint8_t*>(totalFreePtr), 0, free);
    return 1;
}

extern "C" uint64_t host_GetSystemDirectoryA(uint64_t buffer, uint64_t size,
                                             uint64_t, uint64_t, uint64_t,
                                             uint64_t, uint64_t, uint64_t) {
    const char* value = "C:\\Windows\\System32";
    const size_t length = std::strlen(value) + 1;
    if (buffer == 0 || size < length) return static_cast<uint64_t>(length);
    std::memcpy(reinterpret_cast<void*>(buffer), value, length);
    return length - 1;
}

extern "C" uint64_t host_GetWindowsDirectoryA(uint64_t buffer, uint64_t size,
                                              uint64_t, uint64_t, uint64_t,
                                              uint64_t, uint64_t, uint64_t) {
    const char* value = "C:\\Windows";
    const size_t length = std::strlen(value) + 1;
    if (buffer == 0 || size < length) return static_cast<uint64_t>(length);
    std::memcpy(reinterpret_cast<void*>(buffer), value, length);
    return length - 1;
}

extern "C" uint64_t host_GlobalMemoryStatusEx(uint64_t statusPtr, uint64_t,
                                              uint64_t, uint64_t, uint64_t,
                                              uint64_t, uint64_t, uint64_t) {
    if (statusPtr == 0) return 0;
    auto* status = reinterpret_cast<uint8_t*>(statusPtr);
    const uint64_t pageSize = static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
    const uint64_t pages = static_cast<uint64_t>(sysconf(_SC_PHYS_PAGES));
    uint64_t availablePages = pages / 2;
#ifdef _SC_AVPHYS_PAGES
    const long availableCount = sysconf(_SC_AVPHYS_PAGES);
    if (availableCount > 0) availablePages = static_cast<uint64_t>(availableCount);
#endif
    const uint64_t total = pages * pageSize;
    const uint64_t available = availablePages * pageSize;
    writeU32(status, 0, 64);
    writeU32(status, 4, total != 0
                            ? static_cast<uint32_t>((total - available) * 100 / total)
                            : 0);
    writeU64(status, 8, total);
    writeU64(status, 16, available);
    writeU64(status, 24, total);
    writeU64(status, 32, available);
    writeU64(status, 40, 0x7ffffffffffULL);
    writeU64(status, 48, 0x7ffffffffffULL);
    writeU64(status, 56, 0);
    return 1;
}

extern "C" uint64_t host_IsProcessorFeaturePresent(uint64_t, uint64_t, uint64_t,
                                                   uint64_t, uint64_t, uint64_t,
                                                   uint64_t, uint64_t) {
    return 0;  // no CPU feature is advertised before it is implemented
}

extern "C" uint64_t host_GetActiveProcessorCount(uint64_t, uint64_t, uint64_t,
                                                 uint64_t, uint64_t, uint64_t,
                                                 uint64_t, uint64_t) {
    const long processors = sysconf(_SC_NPROCESSORS_ONLN);
    return processors > 0 ? static_cast<uint64_t>(processors) : 1;
}

extern "C" uint64_t host_SetThreadAffinityMask(uint64_t, uint64_t, uint64_t,
                                               uint64_t, uint64_t, uint64_t,
                                               uint64_t, uint64_t) {
    return 1;
}

extern "C" uint64_t host_GetSystemPowerStatus(uint64_t statusPtr, uint64_t,
                                              uint64_t, uint64_t, uint64_t,
                                              uint64_t, uint64_t, uint64_t) {
    if (statusPtr == 0) return 0;
    auto* status = reinterpret_cast<uint8_t*>(statusPtr);
    std::memset(status, 0, 12);
    status[0] = 255;  // ACLineStatus unknown
    status[1] = 255;  // BatteryFlag unknown
    status[2] = 255;  // BatteryLifePercent unknown
    writeU32(status, 4, 0xffffffffU);
    writeU32(status, 8, 0xffffffffU);
    return 1;
}

void registerSystemHostModule() {
    HostModule module;
    module.name = "kernel32.dll";
    module.exportsByName = {
        {"GetSystemInfo", reinterpret_cast<uint64_t>(&host_GetSystemInfo)},
        {"GetVersionExA", reinterpret_cast<uint64_t>(&host_GetVersionExA)},
        {"GetComputerNameA", reinterpret_cast<uint64_t>(&host_GetComputerNameA)},
        {"GetUserNameA", reinterpret_cast<uint64_t>(&host_GetUserNameA)},
        {"GetSystemTime", reinterpret_cast<uint64_t>(&host_GetSystemTime)},
        {"GetLocalTime", reinterpret_cast<uint64_t>(&host_GetLocalTime)},
        {"GetSystemTimeAsFileTime", reinterpret_cast<uint64_t>(&host_GetSystemTimeAsFileTime)},
        {"GetTimeZoneInformation", reinterpret_cast<uint64_t>(&host_GetTimeZoneInformation)},
        {"GetDriveTypeA", reinterpret_cast<uint64_t>(&host_GetDriveTypeA)},
        {"GetDiskFreeSpaceExA", reinterpret_cast<uint64_t>(&host_GetDiskFreeSpaceExA)},
        {"GetSystemDirectoryA", reinterpret_cast<uint64_t>(&host_GetSystemDirectoryA)},
        {"GetWindowsDirectoryA", reinterpret_cast<uint64_t>(&host_GetWindowsDirectoryA)},
        {"GlobalMemoryStatusEx", reinterpret_cast<uint64_t>(&host_GlobalMemoryStatusEx)},
        {"IsProcessorFeaturePresent", reinterpret_cast<uint64_t>(&host_IsProcessorFeaturePresent)},
        {"GetActiveProcessorCount", reinterpret_cast<uint64_t>(&host_GetActiveProcessorCount)},
        {"SetThreadAffinityMask", reinterpret_cast<uint64_t>(&host_SetThreadAffinityMask)},
        {"GetSystemPowerStatus", reinterpret_cast<uint64_t>(&host_GetSystemPowerStatus)},
    };
    std::lock_guard<std::mutex> lock(hostModulesMutex());
    hostModules().push_back(std::move(module));
}

}  // namespace rottr
