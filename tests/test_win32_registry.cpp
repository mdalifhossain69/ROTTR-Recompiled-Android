// Roadmap step 41: Implement registry and user-folder APIs.
// Pass: values persist after restart. The test writes in the parent process,
// then execs a fresh child process that reads the value back from disk.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

extern "C" {
namespace rottr {
uint64_t host_RegOpenKeyExA(uint64_t hkey, uint64_t subKey, uint64_t options,
                            uint64_t sam, uint64_t result, uint64_t, uint64_t,
                            uint64_t);
uint64_t host_RegCreateKeyExA(uint64_t hkey, uint64_t subKey, uint64_t reserved,
                              uint64_t classPtr, uint64_t options, uint64_t sam,
                              uint64_t sa, uint64_t result, uint64_t disposition);
uint64_t host_RegSetValueExA(uint64_t hkey, uint64_t valueName, uint64_t reserved,
                             uint64_t type, uint64_t data, uint64_t dataSize,
                             uint64_t, uint64_t);
uint64_t host_RegQueryValueExA(uint64_t hkey, uint64_t valueName, uint64_t reserved,
                               uint64_t type, uint64_t data, uint64_t size,
                               uint64_t, uint64_t);
uint64_t host_RegCloseKey(uint64_t hkey, uint64_t, uint64_t, uint64_t, uint64_t,
                          uint64_t, uint64_t, uint64_t);
uint64_t host_RegDeleteValueA(uint64_t hkey, uint64_t valueName, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t host_RegEnumKeyExA(uint64_t hkey, uint64_t index, uint64_t name,
                            uint64_t nameLen, uint64_t reserved, uint64_t classPtr,
                            uint64_t classLen, uint64_t time);
uint64_t host_RegEnumValueA(uint64_t hkey, uint64_t index, uint64_t name,
                            uint64_t nameLen, uint64_t reserved, uint64_t type,
                            uint64_t data, uint64_t dataSize);
uint64_t host_RegFlushKey(uint64_t hkey, uint64_t, uint64_t, uint64_t, uint64_t,
                          uint64_t, uint64_t, uint64_t);
uint64_t host_GetEnvironmentVariableA(uint64_t name, uint64_t buffer, uint64_t size,
                                      uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t);
uint64_t host_SHGetFolderPathA(uint64_t hwnd, uint64_t csidl, uint64_t token,
                               uint64_t flags, uint64_t path, uint64_t, uint64_t,
                               uint64_t);
}  // namespace rottr
}

namespace {

constexpr uint64_t kHkeyCurrentUser = 0x80000001ULL;
constexpr uint32_t kRegSz = 1;
constexpr uint32_t kRegDword = 4;
constexpr uint32_t kSuccess = 0;
constexpr uint32_t kFileNotFound = 2;
constexpr uint32_t kMoreData = 234;
constexpr uint32_t kNoMoreItems = 259;

bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

int child(const char* path, bool expectDeleted) {
    setenv("ROTTR_REGISTRY_FILE", path, 1);
    uint64_t key = 0;
    if (rottr::host_RegOpenKeyExA(kHkeyCurrentUser,
                                  reinterpret_cast<uint64_t>("Software\\ROTTR"),
                                  0, 0, reinterpret_cast<uint64_t>(&key), 0, 0,
                                  0) != kSuccess)
        return 10;
    uint32_t value = 0;
    uint32_t size = sizeof(value);
    uint32_t type = 0;
    if (rottr::host_RegQueryValueExA(key, reinterpret_cast<uint64_t>("Answer"), 0,
                                     reinterpret_cast<uint64_t>(&type),
                                     reinterpret_cast<uint64_t>(&value),
                                     reinterpret_cast<uint64_t>(&size), 0,
                                     0) != kSuccess)
        return 11;
    if (type != kRegDword || value != 0x12345678U) return 12;
    char name[32] = {};
    size = sizeof(name);
    const uint64_t nameStatus = rottr::host_RegQueryValueExA(
        key, reinterpret_cast<uint64_t>("Name"), 0,
        reinterpret_cast<uint64_t>(&type), reinterpret_cast<uint64_t>(name),
        reinterpret_cast<uint64_t>(&size), 0, 0);
    if (expectDeleted) {
        if (nameStatus != kFileNotFound) return 15;
    } else {
        if (nameStatus != kSuccess) return 13;
        if (type != kRegSz || std::strcmp(name, "Lara") != 0) return 14;
        rottr::host_RegDeleteValueA(key, reinterpret_cast<uint64_t>("Name"), 0, 0,
                                    0, 0, 0, 0);
    }
    rottr::host_RegCloseKey(key, 0, 0, 0, 0, 0, 0, 0);
    return 0;
}

bool runChild(const char* executable, const char* mode, const char* path) {
    const pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        char* const args[] = {const_cast<char*>(executable),
                              const_cast<char*>(mode),
                              const_cast<char*>(path), nullptr};
        execv(executable, args);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) != pid) return false;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        std::fprintf(stderr, "  %s child exit status: %d\n", mode,
                     WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--child") == 0) {
        return child(argv[2], false);
    }
    if (argc == 3 && std::strcmp(argv[1], "--check-deleted") == 0) {
        return child(argv[2], true);
    }

    char path[512] = {};
    std::snprintf(path, sizeof(path), "/tmp/rottr_step41_%ld.bin",
                  static_cast<long>(getpid()));
    std::remove(path);
    setenv("ROTTR_REGISTRY_FILE", path, 1);

    uint64_t key = 0;
    uint32_t disposition = 0;
    uint64_t status = rottr::host_RegCreateKeyExA(
        kHkeyCurrentUser, reinterpret_cast<uint64_t>("Software\\ROTTR"), 0, 0, 0,
        0, 0, reinterpret_cast<uint64_t>(&key),
        reinterpret_cast<uint64_t>(&disposition));
    if (!expect(status == kSuccess && key != 0, "RegCreateKeyExA must create key"))
        return 1;
    if (!expect(disposition == 1, "new key must report REG_CREATED_NEW_KEY"))
        return 1;

    uint32_t answer = 0x12345678U;
    if (!expect(rottr::host_RegSetValueExA(
                    key, reinterpret_cast<uint64_t>("Answer"), 0, kRegDword,
                    reinterpret_cast<uint64_t>(&answer), sizeof(answer), 0, 0) ==
                    kSuccess,
                "RegSetValueExA(REG_DWORD) must succeed"))
        return 1;
    const char* lara = "Lara";
    if (!expect(rottr::host_RegSetValueExA(
                    key, reinterpret_cast<uint64_t>("Name"), 0, kRegSz,
                    reinterpret_cast<uint64_t>(lara), std::strlen(lara) + 1, 0,
                    0) == kSuccess,
                "RegSetValueExA(REG_SZ) must succeed"))
        return 1;

    // ---- Read back in this process ----
    uint32_t value = 0;
    uint32_t size = sizeof(value);
    uint32_t type = 0;
    if (!expect(rottr::host_RegQueryValueExA(
                    key, reinterpret_cast<uint64_t>("Answer"), 0,
                    reinterpret_cast<uint64_t>(&type),
                    reinterpret_cast<uint64_t>(&value),
                    reinterpret_cast<uint64_t>(&size), 0, 0) == kSuccess &&
                    type == kRegDword && value == 0x12345678U,
                "RegQueryValueExA must return the stored DWORD"))
        return 1;

    // Missing value reports ERROR_FILE_NOT_FOUND, never fake success.
    if (!expect(rottr::host_RegQueryValueExA(
                    key, reinterpret_cast<uint64_t>("Missing"), 0, 0, 0, 0, 0,
                    0) == kFileNotFound,
                "missing value must return ERROR_FILE_NOT_FOUND"))
        return 1;

    // Too-small buffer reports ERROR_MORE_DATA and the required size.
    char tiny[4] = {};
    size = sizeof(tiny);
    if (!expect(rottr::host_RegQueryValueExA(
                    key, reinterpret_cast<uint64_t>("Name"), 0, 0,
                    reinterpret_cast<uint64_t>(tiny),
                    reinterpret_cast<uint64_t>(&size), 0, 0) == kMoreData &&
                    size >= 5,
                "small buffer must return ERROR_MORE_DATA with required size"))
        return 1;

    // ---- Enumeration ----
    char valueName[64] = {};
    uint32_t valueNameLength = sizeof(valueName);
    if (!expect(rottr::host_RegEnumValueA(
                    key, 0, reinterpret_cast<uint64_t>(valueName),
                    reinterpret_cast<uint64_t>(&valueNameLength), 0, 0, 0, 0) ==
                    kSuccess &&
                    std::strcmp(valueName, "Answer") == 0,
                "RegEnumValueA must enumerate values in order"))
        return 1;
    valueNameLength = sizeof(valueName);
    if (!expect(rottr::host_RegEnumValueA(
                    key, 2, reinterpret_cast<uint64_t>(valueName),
                    reinterpret_cast<uint64_t>(&valueNameLength), 0, 0, 0, 0) ==
                    kNoMoreItems,
                "enumeration past the end must return ERROR_NO_MORE_ITEMS"))
        return 1;

    uint64_t software = 0;
    if (!expect(rottr::host_RegOpenKeyExA(
                    kHkeyCurrentUser, reinterpret_cast<uint64_t>("Software"), 0,
                    0, reinterpret_cast<uint64_t>(&software), 0, 0, 0) ==
                    kSuccess,
                "RegOpenKeyExA must open Software"))
        return 1;
    char childName[64] = {};
    uint32_t childLength = sizeof(childName);
    if (!expect(rottr::host_RegEnumKeyExA(
                    software, 0, reinterpret_cast<uint64_t>(childName),
                    reinterpret_cast<uint64_t>(&childLength), 0, 0, 0, 0) ==
                    kSuccess &&
                    std::strcmp(childName, "rottr") == 0,
                "RegEnumKeyExA must list the child key"))
        return 1;
    rottr::host_RegCloseKey(software, 0, 0, 0, 0, 0, 0, 0);

    // ---- User folders ----
    char profile[512] = {};
    if (!expect(rottr::host_GetEnvironmentVariableA(
                    reinterpret_cast<uint64_t>("USERPROFILE"),
                    reinterpret_cast<uint64_t>(profile), sizeof(profile), 0, 0, 0,
                    0, 0) > 0 &&
                    profile[0] != '\0',
                "GetEnvironmentVariableA(USERPROFILE) must return a path"))
        return 1;
    char appdata[512] = {};
    if (!expect(rottr::host_SHGetFolderPathA(
                    0, 0x1a, 0, 0, reinterpret_cast<uint64_t>(appdata), 0, 0, 0) ==
                    0 &&
                    appdata[0] != '\0',
                "SHGetFolderPathA(CSIDL_APPDATA) must return a path"))
        return 1;

    // ---- Persist and restart: a fresh child process reads the values ----
    if (!expect(rottr::host_RegFlushKey(key, 0, 0, 0, 0, 0, 0, 0) == kSuccess,
                "RegFlushKey must succeed"))
        return 1;
    rottr::host_RegCloseKey(key, 0, 0, 0, 0, 0, 0, 0);

    char executable[512] = {};
    if (realpath(argv[0], executable) == nullptr) {
        std::snprintf(executable, sizeof(executable), "%s", argv[0]);
    }
    if (!expect(runChild(executable, "--child", path),
                "fresh child process must read the persisted registry values"))
        return 1;
    // A second fresh process must observe the first child's deletion, proving
    // both writes and deletes survive restarts.
    if (!expect(runChild(executable, "--check-deleted", path),
                "deletion must persist across the restart"))
        return 1;

    std::remove(path);
    std::puts("OK (registry persists across exec, folders resolve)");
    return 0;
}
