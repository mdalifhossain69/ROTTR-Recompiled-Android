// Roadmap step 44: Add the network and system APIs the game calls.
// Pass: offline calls return documented errors without hanging.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
namespace rottr {
uint64_t host_WSAStartup(uint64_t version, uint64_t data, uint64_t, uint64_t,
                         uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t host_WSACleanup(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                         uint64_t, uint64_t);
uint64_t host_WSAGetLastError(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t);
uint64_t host_socket(uint64_t family, uint64_t type, uint64_t protocol, uint64_t,
                     uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t host_closesocket(uint64_t socket, uint64_t, uint64_t, uint64_t, uint64_t,
                          uint64_t, uint64_t, uint64_t);
uint64_t host_connect(uint64_t socket, uint64_t address, uint64_t length, uint64_t,
                      uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t host_recv(uint64_t socket, uint64_t buffer, uint64_t length, uint64_t flags,
                   uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t host_getaddrinfo(uint64_t node, uint64_t service, uint64_t hints,
                          uint64_t result, uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t host_freeaddrinfo(uint64_t info, uint64_t, uint64_t, uint64_t, uint64_t,
                           uint64_t, uint64_t, uint64_t);
uint64_t host_htons(uint64_t value, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                    uint64_t, uint64_t);
uint64_t host_ntohs(uint64_t value, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                    uint64_t, uint64_t);
uint64_t host_inet_addr(uint64_t name, uint64_t, uint64_t, uint64_t, uint64_t,
                        uint64_t, uint64_t, uint64_t);
uint64_t host_GetSystemInfo(uint64_t info, uint64_t, uint64_t, uint64_t, uint64_t,
                            uint64_t, uint64_t, uint64_t);
uint64_t host_GetVersionExA(uint64_t version, uint64_t, uint64_t, uint64_t,
                            uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t host_GetComputerNameA(uint64_t buffer, uint64_t size, uint64_t, uint64_t,
                               uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t host_GetSystemTime(uint64_t time, uint64_t, uint64_t, uint64_t, uint64_t,
                            uint64_t, uint64_t, uint64_t);
uint64_t host_GetDiskFreeSpaceExA(uint64_t path, uint64_t freeBytes, uint64_t total,
                                  uint64_t totalFree, uint64_t, uint64_t, uint64_t,
                                  uint64_t);
uint64_t host_GetSystemDirectoryA(uint64_t buffer, uint64_t size, uint64_t, uint64_t,
                                  uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t host_GlobalMemoryStatusEx(uint64_t status, uint64_t, uint64_t, uint64_t,
                                   uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t host_GetSystemPowerStatus(uint64_t status, uint64_t, uint64_t, uint64_t,
                                   uint64_t, uint64_t, uint64_t, uint64_t);
}  // namespace rottr
}

namespace {

constexpr uint64_t kInvalidSocket = ~0ULL;
constexpr uint32_t kWsaEnotconn = 10057;
constexpr uint32_t kWsaEconnrefused = 10061;
constexpr uint32_t kWsaEtimeout = 10060;
constexpr uint32_t kWsaEhostNotFound = 11001;

// Windows ADDRINFOA layout on x64.
struct WinAddrInfo {
    int32_t ai_flags;
    int32_t ai_family;
    int32_t ai_socktype;
    int32_t ai_protocol;
    size_t ai_addrlen;
    char* ai_canonname;
    void* ai_addr;
    WinAddrInfo* ai_next;
};

bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

}  // namespace

int main() {
    uint8_t wsaData[400] = {};
    if (!expect(rottr::host_WSAStartup(0x0202, reinterpret_cast<uint64_t>(wsaData),
                                       0, 0, 0, 0, 0, 0) == 0,
                "WSAStartup must succeed"))
        return 1;
    if (!expect(wsaData[0] == 0x02 && wsaData[1] == 0x02,
                "WSAStartup must report version 2.2"))
        return 1;

    const uint64_t socketHandle = rottr::host_socket(2, 1, 6, 0, 0, 0, 0, 0);
    if (!expect(socketHandle != kInvalidSocket, "socket(AF_INET, SOCK_STREAM) must succeed"))
        return 1;

    // ---- recv on an unconnected socket returns WSAENOTCONN, not fake data ----
    char buffer[8] = {};
    if (!expect(rottr::host_recv(socketHandle, reinterpret_cast<uint64_t>(buffer), 4,
                                 0, 0, 0, 0, 0) == kInvalidSocket &&
                    rottr::host_WSAGetLastError(0, 0, 0, 0, 0, 0, 0, 0) ==
                        kWsaEnotconn,
                "recv on an unconnected socket must fail with WSAENOTCONN"))
        return 1;

    // ---- Offline connect fails quickly with a documented error ----
    uint8_t address[16] = {};
    const uint16_t family = 2;
    const uint16_t port = static_cast<uint16_t>(
        rottr::host_htons(1, 0, 0, 0, 0, 0, 0, 0));
    const uint32_t loopback = static_cast<uint32_t>(
        rottr::host_inet_addr(reinterpret_cast<uint64_t>("127.0.0.1"), 0, 0, 0, 0,
                              0, 0, 0));
    std::memcpy(address + 0, &family, 2);
    std::memcpy(address + 2, &port, 2);
    std::memcpy(address + 4, &loopback, 4);
    const auto start = std::chrono::steady_clock::now();
    const uint64_t connectResult = rottr::host_connect(
        socketHandle, reinterpret_cast<uint64_t>(address), sizeof(address), 0, 0, 0,
        0, 0);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    const uint64_t connectError =
        rottr::host_WSAGetLastError(0, 0, 0, 0, 0, 0, 0, 0);
    if (!expect(connectResult == kInvalidSocket &&
                    (connectError == kWsaEconnrefused ||
                     connectError == kWsaEtimeout),
                "offline connect must return WSAECONNREFUSED or WSAETIMEDOUT"))
        return 1;
    if (!expect(elapsed < 2000, "offline connect must not hang")) {
        std::fprintf(stderr, "  connect took %lld ms\n", (long long)elapsed);
        return 1;
    }
    rottr::host_closesocket(socketHandle, 0, 0, 0, 0, 0, 0, 0);

    // ---- Numeric getaddrinfo succeeds without DNS ----
    uint64_t result = 0;
    if (!expect(rottr::host_getaddrinfo(reinterpret_cast<uint64_t>("127.0.0.1"),
                                        reinterpret_cast<uint64_t>("80"), 0,
                                        reinterpret_cast<uint64_t>(&result), 0, 0,
                                        0, 0) == 0 &&
                    result != 0,
                "numeric getaddrinfo must succeed"))
        return 1;
    const auto* info = reinterpret_cast<const WinAddrInfo*>(result);
    if (!expect(info->ai_family == 2 && info->ai_addr != nullptr &&
                    info->ai_addrlen >= 16,
                "getaddrinfo must return an AF_INET address"))
        return 1;
    rottr::host_freeaddrinfo(result, 0, 0, 0, 0, 0, 0, 0);

    // ---- The reserved .invalid TLD fails immediately, offline ----
    result = 0xdeadbeefULL;
    const auto dnsStart = std::chrono::steady_clock::now();
    const uint64_t dnsStatus = rottr::host_getaddrinfo(
        reinterpret_cast<uint64_t>("no-such-host.invalid"),
        reinterpret_cast<uint64_t>("80"), 0, reinterpret_cast<uint64_t>(&result), 0,
        0, 0, 0);
    const auto dnsElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - dnsStart)
                                .count();
    if (!expect(dnsStatus == kWsaEhostNotFound && result == 0,
                "invalid host must return WSAHOST_NOT_FOUND with no result"))
        return 1;
    if (!expect(dnsElapsed < 1000, "invalid host lookup must not hang")) return 1;

    if (!expect(rottr::host_ntohs(rottr::host_htons(0x1234, 0, 0, 0, 0, 0, 0, 0), 0,
                                  0, 0, 0, 0, 0, 0) == 0x1234,
                "htons/ntohs must round-trip"))
        return 1;

    // ---- System APIs return real host values ----
    uint8_t systemInfo[64] = {};
    rottr::host_GetSystemInfo(reinterpret_cast<uint64_t>(systemInfo), 0, 0, 0, 0, 0,
                              0, 0);
    uint32_t pageSize = 0;
    uint32_t granularity = 0;
    uint32_t processors = 0;
    std::memcpy(&pageSize, systemInfo + 4, 4);
    std::memcpy(&processors, systemInfo + 32, 4);
    std::memcpy(&granularity, systemInfo + 40, 4);
    if (!expect(pageSize == 4096 && granularity == 65536 && processors >= 1,
                "GetSystemInfo must report page size, granularity and CPUs"))
        return 1;

    uint8_t version[148] = {};
    if (!expect(rottr::host_GetVersionExA(reinterpret_cast<uint64_t>(version), 0, 0,
                                          0, 0, 0, 0, 0) == 1,
                "GetVersionExA must succeed"))
        return 1;
    uint32_t major = 0;
    std::memcpy(&major, version + 4, 4);
    if (!expect(major >= 6, "GetVersionExA must report a modern version")) return 1;

    uint8_t systemTime[16] = {};
    rottr::host_GetSystemTime(reinterpret_cast<uint64_t>(systemTime), 0, 0, 0, 0, 0,
                              0, 0);
    uint16_t year = 0;
    std::memcpy(&year, systemTime + 0, 2);
    if (!expect(year >= 2020, "GetSystemTime must report the current year")) return 1;

    char computer[128] = {};
    uint32_t computerLength = sizeof(computer);
    if (!expect(rottr::host_GetComputerNameA(reinterpret_cast<uint64_t>(computer),
                                             reinterpret_cast<uint64_t>(&computerLength),
                                             0, 0, 0, 0, 0, 0) == 1 &&
                    computer[0] != '\0',
                "GetComputerNameA must return a name"))
        return 1;

    uint64_t freeBytes = 0;
    uint64_t totalBytes = 0;
    if (!expect(rottr::host_GetDiskFreeSpaceExA(reinterpret_cast<uint64_t>("/"),
                                                reinterpret_cast<uint64_t>(&freeBytes),
                                                reinterpret_cast<uint64_t>(&totalBytes),
                                                0, 0, 0, 0, 0) == 1 &&
                    totalBytes > 0,
                "GetDiskFreeSpaceExA must report the disk size"))
        return 1;

    char systemDirectory[64] = {};
    if (!expect(rottr::host_GetSystemDirectoryA(
                    reinterpret_cast<uint64_t>(systemDirectory),
                    sizeof(systemDirectory), 0, 0, 0, 0, 0, 0) > 0 &&
                    std::strcmp(systemDirectory, "C:\\Windows\\System32") == 0,
                "GetSystemDirectoryA must return the Windows system directory"))
        return 1;

    uint8_t memoryStatus[64] = {};
    if (!expect(rottr::host_GlobalMemoryStatusEx(
                    reinterpret_cast<uint64_t>(memoryStatus), 0, 0, 0, 0, 0, 0, 0) ==
                    1,
                "GlobalMemoryStatusEx must succeed"))
        return 1;
    uint64_t totalMemory = 0;
    std::memcpy(&totalMemory, memoryStatus + 8, 8);
    if (!expect(totalMemory > 0, "GlobalMemoryStatusEx must report total memory"))
        return 1;

    uint8_t powerStatus[12] = {};
    if (!expect(rottr::host_GetSystemPowerStatus(
                    reinterpret_cast<uint64_t>(powerStatus), 0, 0, 0, 0, 0, 0, 0) ==
                    1,
                "GetSystemPowerStatus must succeed"))
        return 1;

    rottr::host_WSACleanup(0, 0, 0, 0, 0, 0, 0, 0);
    std::puts("OK (offline socket errors, bounded connect, system APIs)");
    return 0;
}
