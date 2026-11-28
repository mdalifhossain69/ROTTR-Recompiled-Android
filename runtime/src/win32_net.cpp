// Roadmap step 44: network APIs the game calls.
//
// Winsock is implemented over POSIX sockets. Connect uses a bounded
// non-blocking wait so an offline/unroutable call returns a documented error
// instead of hanging. Name resolution short-circuits the reserved .invalid
// TLD (RFC 2606) and numeric hosts never touch DNS.

#include "rottr/x64_bridge.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace rottr {
namespace {

constexpr uint64_t kInvalidSocket = ~0ULL;
constexpr uint64_t kSocketError = ~0ULL;

constexpr uint32_t kWsaEwouldblock = 10035;
constexpr uint32_t kWsaEnotsock = 10038;
constexpr uint32_t kWsaEaddrInUse = 10048;
constexpr uint32_t kWsaEconnreset = 10054;
constexpr uint32_t kWsaEnotconn = 10057;
constexpr uint32_t kWsaEtimeout = 10060;
constexpr uint32_t kWsaEconnrefused = 10061;
constexpr uint32_t kWsaEhostNotFound = 11001;
constexpr uint32_t kWsaEinval = 10022;

thread_local uint32_t g_wsaError = 0;

uint32_t wsaFromErrno(int value) {
    if (value == EINPROGRESS || value == EAGAIN) return kWsaEwouldblock;
    if (value == ECONNREFUSED) return kWsaEconnrefused;
    if (value == ETIMEDOUT) return kWsaEtimeout;
    if (value == ENOTCONN) return kWsaEnotconn;
    if (value == ENOTSOCK) return kWsaEnotsock;
    if (value == EADDRINUSE) return kWsaEaddrInUse;
    if (value == ECONNRESET) return kWsaEconnreset;
    return kWsaEinval;
}

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

bool numericHost(const char* name) {
    in_addr address{};
    return name != nullptr && inet_pton(AF_INET, name, &address) == 1;
}

bool reservedInvalid(const char* name) {
    if (name == nullptr) return false;
    const size_t length = std::strlen(name);
    return length >= 8 && std::strcmp(name + length - 8, ".invalid") == 0;
}

}  // namespace

extern "C" uint64_t host_WSAStartup(uint64_t version, uint64_t dataPtr, uint64_t,
                                    uint64_t, uint64_t, uint64_t, uint64_t,
                                    uint64_t) {
    if (dataPtr != 0) {
        auto* data = reinterpret_cast<uint8_t*>(dataPtr);
        std::memset(data, 0, 400);
        const uint16_t requested = static_cast<uint16_t>(version);
        std::memcpy(data + 0, &requested, 2);
        std::memcpy(data + 2, &requested, 2);
    }
    g_wsaError = 0;
    return 0;
}

extern "C" uint64_t host_WSACleanup(uint64_t, uint64_t, uint64_t, uint64_t,
                                    uint64_t, uint64_t, uint64_t, uint64_t) {
    return 0;
}

extern "C" uint64_t host_WSAGetLastError(uint64_t, uint64_t, uint64_t, uint64_t,
                                         uint64_t, uint64_t, uint64_t, uint64_t) {
    return g_wsaError;
}

extern "C" uint64_t host_socket(uint64_t family, uint64_t type, uint64_t protocol,
                                uint64_t, uint64_t, uint64_t, uint64_t,
                                uint64_t) {
    const int fd = ::socket(static_cast<int>(family), static_cast<int>(type),
                            static_cast<int>(protocol));
    if (fd < 0) {
        g_wsaError = wsaFromErrno(errno);
        return kInvalidSocket;
    }
    g_wsaError = 0;
    return static_cast<uint64_t>(fd);
}

extern "C" uint64_t host_closesocket(uint64_t socketHandle, uint64_t, uint64_t,
                                     uint64_t, uint64_t, uint64_t, uint64_t,
                                     uint64_t) {
    if (socketHandle == kInvalidSocket) {
        g_wsaError = kWsaEnotsock;
        return kSocketError;
    }
    const int result = ::close(static_cast<int>(socketHandle));
    if (result != 0) g_wsaError = wsaFromErrno(errno);
    return result == 0 ? 0 : kSocketError;
}

extern "C" uint64_t host_connect(uint64_t socketHandle, uint64_t addressPtr,
                                 uint64_t addressLength, uint64_t, uint64_t,
                                 uint64_t, uint64_t, uint64_t) {
    if (socketHandle == kInvalidSocket || addressPtr == 0 ||
        addressLength < sizeof(sockaddr_in)) {
        g_wsaError = kWsaEinval;
        return kSocketError;
    }
    const auto* input = reinterpret_cast<const uint8_t*>(addressPtr);
    uint16_t family = 0;
    uint16_t port = 0;
    uint32_t address = 0;
    std::memcpy(&family, input + 0, 2);
    std::memcpy(&port, input + 2, 2);
    std::memcpy(&address, input + 4, 4);
    if (family != AF_INET) {
        g_wsaError = kWsaEinval;
        return kSocketError;
    }
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = port;
    target.sin_addr.s_addr = address;
    const int fd = static_cast<int>(socketHandle);
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        g_wsaError = wsaFromErrno(errno);
        return kSocketError;
    }
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int result = ::connect(fd, reinterpret_cast<const sockaddr*>(&target),
                           sizeof(target));
    uint32_t error = 0;
    if (result != 0) {
        if (errno == EINPROGRESS || errno == EALREADY) {
            pollfd descriptor{fd, POLLOUT, 0};
            const int ready = ::poll(&descriptor, 1, 300);
            if (ready == 0) {
                error = kWsaEtimeout;
            } else if (ready < 0) {
                error = wsaFromErrno(errno);
            } else {
                int socketError = 0;
                socklen_t length = sizeof(socketError);
                if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketError, &length) !=
                    0) {
                    error = wsaFromErrno(errno);
                } else if (socketError != 0) {
                    error = wsaFromErrno(socketError);
                }
            }
        } else {
            error = wsaFromErrno(errno);
        }
    }
    fcntl(fd, F_SETFL, flags);
    g_wsaError = error;
    return error == 0 ? 0 : kSocketError;
}

extern "C" uint64_t host_send(uint64_t socketHandle, uint64_t buffer,
                              uint64_t length, uint64_t flags, uint64_t, uint64_t,
                              uint64_t, uint64_t) {
    if (socketHandle == kInvalidSocket) {
        g_wsaError = kWsaEnotsock;
        return kSocketError;
    }
    const ssize_t sent = ::send(static_cast<int>(socketHandle),
                                reinterpret_cast<const void*>(buffer),
                                static_cast<size_t>(length), MSG_NOSIGNAL |
                                    static_cast<int>(flags));
    if (sent < 0) {
        g_wsaError = wsaFromErrno(errno);
        return kSocketError;
    }
    g_wsaError = 0;
    return static_cast<uint64_t>(sent);
}

extern "C" uint64_t host_recv(uint64_t socketHandle, uint64_t buffer,
                              uint64_t length, uint64_t flags, uint64_t, uint64_t,
                              uint64_t, uint64_t) {
    if (socketHandle == kInvalidSocket) {
        g_wsaError = kWsaEnotsock;
        return kSocketError;
    }
    const ssize_t received = ::recv(static_cast<int>(socketHandle),
                                    reinterpret_cast<void*>(buffer),
                                    static_cast<size_t>(length),
                                    static_cast<int>(flags));
    if (received < 0) {
        g_wsaError = wsaFromErrno(errno);
        return kSocketError;
    }
    g_wsaError = 0;
    return static_cast<uint64_t>(received);
}

extern "C" uint64_t host_shutdown(uint64_t socketHandle, uint64_t how, uint64_t,
                                  uint64_t, uint64_t, uint64_t, uint64_t,
                                  uint64_t) {
    if (socketHandle == kInvalidSocket) {
        g_wsaError = kWsaEnotsock;
        return kSocketError;
    }
    return ::shutdown(static_cast<int>(socketHandle), static_cast<int>(how)) == 0
               ? 0
               : kSocketError;
}

extern "C" uint64_t host_getaddrinfo(uint64_t nodePtr, uint64_t servicePtr,
                                     uint64_t hintsPtr, uint64_t resultPtr,
                                     uint64_t, uint64_t, uint64_t, uint64_t) {
    (void)hintsPtr;
    const char* node = reinterpret_cast<const char*>(nodePtr);
    const char* service = reinterpret_cast<const char*>(servicePtr);
    if (resultPtr != 0) *reinterpret_cast<uint64_t*>(resultPtr) = 0;
    if (reservedInvalid(node)) {
        g_wsaError = kWsaEhostNotFound;
        return kWsaEhostNotFound;
    }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (numericHost(node)) {
        hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    }
    addrinfo* results = nullptr;
    const int status = ::getaddrinfo(node, service, &hints, &results);
    if (status != 0 || results == nullptr) {
        g_wsaError = kWsaEhostNotFound;
        return kWsaEhostNotFound;
    }
    WinAddrInfo* head = nullptr;
    WinAddrInfo* tail = nullptr;
    for (addrinfo* entry = results; entry != nullptr; entry = entry->ai_next) {
        auto* nodeCopy = new WinAddrInfo();
        nodeCopy->ai_flags = entry->ai_flags;
        nodeCopy->ai_family = entry->ai_family;
        nodeCopy->ai_socktype = entry->ai_socktype;
        nodeCopy->ai_protocol = entry->ai_protocol;
        nodeCopy->ai_addrlen = entry->ai_addrlen;
        nodeCopy->ai_canonname = nullptr;
        nodeCopy->ai_addr = nullptr;
        nodeCopy->ai_next = nullptr;
        if (entry->ai_addr != nullptr && entry->ai_addrlen != 0) {
            nodeCopy->ai_addr = std::malloc(entry->ai_addrlen);
            if (nodeCopy->ai_addr != nullptr) {
                std::memcpy(nodeCopy->ai_addr, entry->ai_addr, entry->ai_addrlen);
            }
        }
        if (entry->ai_canonname != nullptr) {
            const size_t length = std::strlen(entry->ai_canonname) + 1;
            nodeCopy->ai_canonname = static_cast<char*>(std::malloc(length));
            if (nodeCopy->ai_canonname != nullptr) {
                std::memcpy(nodeCopy->ai_canonname, entry->ai_canonname, length);
            }
        }
        if (tail == nullptr) {
            head = nodeCopy;
        } else {
            tail->ai_next = nodeCopy;
        }
        tail = nodeCopy;
    }
    ::freeaddrinfo(results);
    if (resultPtr == 0) {
        WinAddrInfo* current = head;
        while (current != nullptr) {
            WinAddrInfo* next = current->ai_next;
            std::free(current->ai_addr);
            std::free(current->ai_canonname);
            delete current;
            current = next;
        }
        g_wsaError = kWsaEinval;
        return kWsaEinval;
    }
    *reinterpret_cast<uint64_t*>(resultPtr) = reinterpret_cast<uint64_t>(head);
    g_wsaError = 0;
    return 0;
}

extern "C" uint64_t host_freeaddrinfo(uint64_t infoPtr, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t) {
    auto* current = reinterpret_cast<WinAddrInfo*>(infoPtr);
    while (current != nullptr) {
        WinAddrInfo* next = current->ai_next;
        std::free(current->ai_addr);
        std::free(current->ai_canonname);
        delete current;
        current = next;
    }
    return 0;
}

extern "C" uint64_t host_htons(uint64_t value, uint64_t, uint64_t, uint64_t,
                               uint64_t, uint64_t, uint64_t, uint64_t) {
    return htons(static_cast<uint16_t>(value));
}

extern "C" uint64_t host_ntohs(uint64_t value, uint64_t, uint64_t, uint64_t,
                               uint64_t, uint64_t, uint64_t, uint64_t) {
    return ntohs(static_cast<uint16_t>(value));
}

extern "C" uint64_t host_htonl(uint64_t value, uint64_t, uint64_t, uint64_t,
                               uint64_t, uint64_t, uint64_t, uint64_t) {
    return htonl(static_cast<uint32_t>(value));
}

extern "C" uint64_t host_ntohl(uint64_t value, uint64_t, uint64_t, uint64_t,
                               uint64_t, uint64_t, uint64_t, uint64_t) {
    return ntohl(static_cast<uint32_t>(value));
}

extern "C" uint64_t host_inet_addr(uint64_t namePtr, uint64_t, uint64_t, uint64_t,
                                   uint64_t, uint64_t, uint64_t, uint64_t) {
    const char* name = reinterpret_cast<const char*>(namePtr);
    in_addr address{};
    if (name == nullptr || inet_pton(AF_INET, name, &address) != 1) {
        return 0xffffffffULL;  // INADDR_NONE
    }
    return address.s_addr;
}

extern "C" uint64_t host_inet_ntoa(uint64_t address, uint64_t, uint64_t, uint64_t,
                                   uint64_t, uint64_t, uint64_t, uint64_t) {
    static thread_local char buffer[32];
    in_addr value{};
    value.s_addr = static_cast<in_addr_t>(address);
    if (inet_ntop(AF_INET, &value, buffer, sizeof(buffer)) == nullptr) return 0;
    return reinterpret_cast<uint64_t>(buffer);
}

void registerNetHostModule() {
    HostModule module;
    module.name = "ws2_32.dll";
    module.exportsByName = {
        {"WSAStartup", reinterpret_cast<uint64_t>(&host_WSAStartup)},
        {"WSACleanup", reinterpret_cast<uint64_t>(&host_WSACleanup)},
        {"WSAGetLastError", reinterpret_cast<uint64_t>(&host_WSAGetLastError)},
        {"socket", reinterpret_cast<uint64_t>(&host_socket)},
        {"closesocket", reinterpret_cast<uint64_t>(&host_closesocket)},
        {"connect", reinterpret_cast<uint64_t>(&host_connect)},
        {"send", reinterpret_cast<uint64_t>(&host_send)},
        {"recv", reinterpret_cast<uint64_t>(&host_recv)},
        {"shutdown", reinterpret_cast<uint64_t>(&host_shutdown)},
        {"getaddrinfo", reinterpret_cast<uint64_t>(&host_getaddrinfo)},
        {"freeaddrinfo", reinterpret_cast<uint64_t>(&host_freeaddrinfo)},
        {"htons", reinterpret_cast<uint64_t>(&host_htons)},
        {"ntohs", reinterpret_cast<uint64_t>(&host_ntohs)},
        {"htonl", reinterpret_cast<uint64_t>(&host_htonl)},
        {"ntohl", reinterpret_cast<uint64_t>(&host_ntohl)},
        {"inet_addr", reinterpret_cast<uint64_t>(&host_inet_addr)},
        {"inet_ntoa", reinterpret_cast<uint64_t>(&host_inet_ntoa)},
    };
    std::lock_guard<std::mutex> lock(hostModulesMutex());
    hostModules().push_back(std::move(module));
}

}  // namespace rottr
