// Roadmap step 34: Implement files and paths.
// Pass: case-insensitive open, seek past 4 GB and directory listing work.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

extern "C" {
namespace rottr {
uint64_t host_CreateFileA(uint64_t namePtr, uint64_t access,
                          uint64_t share, uint64_t sa,
                          uint64_t disp, uint64_t flags,
                          uint64_t templateFile, uint64_t);
uint64_t host_ReadFile(uint64_t handle, uint64_t buffer,
                       uint64_t bytesToRead, uint64_t bytesRead,
                       uint64_t overlapped, uint64_t, uint64_t, uint64_t);
uint64_t host_WriteFile(uint64_t handle, uint64_t buffer,
                        uint64_t bytesToWrite, uint64_t bytesWritten,
                        uint64_t overlapped, uint64_t, uint64_t, uint64_t);
uint64_t host_CloseHandle(uint64_t handle, uint64_t, uint64_t,
                          uint64_t, uint64_t, uint64_t,
                          uint64_t, uint64_t);
uint64_t host_SetFilePointer(uint64_t handle, uint64_t distance,
                             uint64_t high, uint64_t method,
                             uint64_t, uint64_t, uint64_t,
                             uint64_t);
uint64_t host_FindFirstFileA(uint64_t pathPtr, uint64_t findDataPtr,
                             uint64_t, uint64_t, uint64_t, uint64_t,
                             uint64_t, uint64_t);
uint64_t host_FindNextFileA(uint64_t handle, uint64_t findDataPtr,
                            uint64_t, uint64_t, uint64_t, uint64_t,
                            uint64_t, uint64_t);
uint64_t host_FindClose(uint64_t handle, uint64_t, uint64_t,
                        uint64_t, uint64_t, uint64_t,
                        uint64_t, uint64_t);
}  // namespace rottr
}

namespace {
[[maybe_unused]] bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}
}  // namespace

static const size_t kFindDataSize = 310;

int main() {
    const char* testFile = "/tmp/rottr_step34_test/HelloWorld.txt";
    std::error_code directoryError;
    std::filesystem::create_directories("/tmp/rottr_step34_test", directoryError);
    if (directoryError) {
        std::fprintf(stderr, "cannot create test directory: %s\n",
                     directoryError.message().c_str());
        return 1;
    }

    {
        FILE* f = std::fopen(testFile, "wb");
        if (!f) { std::fprintf(stderr, "cannot create test file\n"); return 1; }
        const char* content = "Hello, ROTTR!";
        std::fwrite(content, 1, std::strlen(content), f);
        std::fclose(f);
    }

    // ---- Test 1: case-insensitive open ----
    const char* wrongCase = "/tmp/rottr_step34_test/helloworld.TXT";
    uint64_t h = rottr::host_CreateFileA(reinterpret_cast<uint64_t>(wrongCase),
                                         0, 0, 0, 0, 0, 0, 0);
    if (!expect(h != static_cast<uint64_t>(-1) && h != 0,
                "case-insensitive open must succeed"))
        return 1;

    uint8_t buf[64] = {};
    uint64_t readResult = rottr::host_ReadFile(h, reinterpret_cast<uint64_t>(buf),
                                               16, 0, 0, 0, 0, 0);
    if (!expect(readResult == 1, "ReadFile must return TRUE"))
        return 1;
    if (!expect(std::memcmp(buf, "Hello, ROTTR!", 14) == 0,
                "case-insensitive open must read correct content"))
        return 1;
    rottr::host_CloseHandle(h, 0, 0, 0, 0, 0, 0, 0);

    // ---- Test 2: seek past 4 GB ----
    uint64_t h2 = rottr::host_CreateFileA(reinterpret_cast<uint64_t>(testFile),
                                          0, 0, 0, 0, 0, 0, 0);
    if (!expect(h2 != static_cast<uint64_t>(-1) && h2 != 0,
                "CreateFileA must succeed for seek test"))
        return 1;
    uint64_t newPos = rottr::host_SetFilePointer(h2, 0, 5, 0, 0, 0, 0, 0);
    if (!expect(newPos != static_cast<uint64_t>(-1),
                "SetFilePointer past 4 GB must succeed"))
        return 1;
    if (!expect(newPos == static_cast<uint64_t>(5ULL << 32),
                "SetFilePointer must return correct 64-bit position"))
        return 1;
    rottr::host_CloseHandle(h2, 0, 0, 0, 0, 0, 0, 0);

    // ---- Test 3: directory listing ----
    const char* testDir = "/tmp/rottr_step34_test";
    uint8_t findData[kFindDataSize] = {};
    uint64_t findHandle = rottr::host_FindFirstFileA(
        reinterpret_cast<uint64_t>(testDir),
        reinterpret_cast<uint64_t>(findData), 0, 0, 0, 0, 0, 0);
    if (!expect(findHandle != static_cast<uint64_t>(-1) && findHandle != 0,
                "FindFirstFileA must succeed"))
        return 1;

    std::string fileName(reinterpret_cast<const char*>(findData + 36));
    bool foundHelloWorld = fileName.find("HelloWorld") != std::string::npos;
    if (!expect(foundHelloWorld, "directory listing must find HelloWorld.txt")) {
        std::fprintf(stderr, "  found entry: %s\n", fileName.c_str());
        rottr::host_FindClose(findHandle, 0, 0, 0, 0, 0, 0, 0);
        return 1;
    }
    rottr::host_FindClose(findHandle, 0, 0, 0, 0, 0, 0, 0);

    // ---- Test 4: ReadFile at EOF returns FALSE ----
    uint64_t h3 = rottr::host_CreateFileA(reinterpret_cast<uint64_t>(testFile),
                                          0, 0, 0, 0, 0, 0, 0);
    if (!expect(h3 != static_cast<uint64_t>(-1) && h3 != 0,
                "CreateFileA must succeed for EOF test"))
        return 1;
    uint8_t eofBuf[64] = {};
    rottr::host_ReadFile(h3, reinterpret_cast<uint64_t>(eofBuf), 64, 0, 0, 0, 0, 0);
    uint64_t eofResult2 = rottr::host_ReadFile(h3, reinterpret_cast<uint64_t>(eofBuf),
                                               64, 0, 0, 0, 0, 0);
    if (!expect(eofResult2 == 0, "ReadFile at EOF must return FALSE"))
        return 1;
    rottr::host_CloseHandle(h3, 0, 0, 0, 0, 0, 0, 0);

    std::puts("OK");
    return 0;
}
