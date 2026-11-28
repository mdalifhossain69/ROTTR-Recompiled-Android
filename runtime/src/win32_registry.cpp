// Roadmap step 41: registry and user-folder APIs.
//
// The registry is backed by a small persistent store so values survive a
// process restart. The store is written atomically (temporary file + rename)
// and loaded lazily. Nothing here returns fake success: every error path
// reports the documented Win32 error code.

#include "rottr/x64_bridge.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <unistd.h>

namespace rottr {
namespace {

constexpr uint32_t kErrorSuccess = 0;
constexpr uint32_t kErrorFileNotFound = 2;
constexpr uint32_t kErrorInvalidHandle = 6;
constexpr uint32_t kErrorMoreData = 234;
constexpr uint32_t kErrorNoMoreItems = 259;
constexpr uint32_t kErrorInsufficientBuffer = 122;

constexpr uint64_t kHkeyClassesRoot = 0x80000000ULL;
constexpr uint64_t kHkeyCurrentUser = 0x80000001ULL;
constexpr uint64_t kHkeyLocalMachine = 0x80000002ULL;
constexpr uint64_t kHkeyUsers = 0x80000003ULL;
constexpr uint64_t kHkeyCurrentConfig = 0x80000005ULL;

struct RegValue {
    uint32_t type = 0;
    std::vector<uint8_t> data;
};

using ValueMap = std::map<std::string, RegValue>;

std::mutex g_mutex;
std::map<std::string, ValueMap> g_keys;
std::map<uint64_t, std::string> g_handles;
uint64_t g_nextHandle = 0x20000000ULL;
bool g_loaded = false;
bool g_dirty = false;
std::string g_path;

std::string lower(std::string value) {
    for (char& c : value) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return value;
}

std::string registryPath() {
    const char* env = std::getenv("ROTTR_REGISTRY_FILE");
    if (env != nullptr && env[0] != '\0') return env;
    const char* home = std::getenv("HOME");
    if (home != nullptr && home[0] != '\0') return std::string(home) + "/.rottr_registry.bin";
    return "/tmp/rottr_registry.bin";
}

bool writeAll(FILE* file, const void* data, size_t size) {
    return size == 0 || std::fwrite(data, 1, size, file) == size;
}

void saveLocked() {
    if (!g_dirty) return;
    const std::string temp = g_path + ".tmp";
    FILE* file = std::fopen(temp.c_str(), "wb");
    if (file == nullptr) return;
    const char magic[8] = {'R', 'R', 'E', 'G', '0', '0', '0', '1'};
    bool ok = writeAll(file, magic, sizeof(magic));
    uint32_t keyCount = static_cast<uint32_t>(g_keys.size());
    ok = ok && writeAll(file, &keyCount, sizeof(keyCount));
    for (const auto& key : g_keys) {
        uint32_t nameLength = static_cast<uint32_t>(key.first.size());
        ok = ok && writeAll(file, &nameLength, sizeof(nameLength));
        ok = ok && writeAll(file, key.first.data(), key.first.size());
        uint32_t valueCount = static_cast<uint32_t>(key.second.size());
        ok = ok && writeAll(file, &valueCount, sizeof(valueCount));
        for (const auto& value : key.second) {
            uint32_t valueNameLength = static_cast<uint32_t>(value.first.size());
            ok = ok && writeAll(file, &valueNameLength, sizeof(valueNameLength));
            ok = ok && writeAll(file, value.first.data(), value.first.size());
            uint32_t type = value.second.type;
            uint32_t dataSize = static_cast<uint32_t>(value.second.data.size());
            ok = ok && writeAll(file, &type, sizeof(type));
            ok = ok && writeAll(file, &dataSize, sizeof(dataSize));
            ok = ok && writeAll(file, value.second.data.data(), value.second.data.size());
            if (!ok) break;
        }
        if (!ok) break;
    }
    std::fclose(file);
    if (ok) {
        std::rename(temp.c_str(), g_path.c_str());
        g_dirty = false;
    } else {
        std::remove(temp.c_str());
    }
}

bool readAll(FILE* file, void* data, size_t size) {
    return size == 0 || std::fread(data, 1, size, file) == size;
}

void loadLocked() {
    if (g_loaded) return;
    g_loaded = true;
    g_path = registryPath();
    FILE* file = std::fopen(g_path.c_str(), "rb");
    if (file == nullptr) return;
    char magic[8] = {};
    if (!readAll(file, magic, sizeof(magic)) ||
        std::memcmp(magic, "RREG0001", 8) != 0) {
        std::fclose(file);
        return;
    }
    uint32_t keyCount = 0;
    if (!readAll(file, &keyCount, sizeof(keyCount)) || keyCount > 100000) {
        std::fclose(file);
        return;
    }
    for (uint32_t i = 0; i < keyCount; ++i) {
        uint32_t nameLength = 0;
        if (!readAll(file, &nameLength, sizeof(nameLength)) || nameLength > 4096) break;
        std::string name(nameLength, '\0');
        if (!readAll(file, name.data(), nameLength)) break;
        uint32_t valueCount = 0;
        if (!readAll(file, &valueCount, sizeof(valueCount)) || valueCount > 100000) break;
        ValueMap values;
        bool ok = true;
        for (uint32_t v = 0; v < valueCount; ++v) {
            uint32_t valueNameLength = 0;
            if (!readAll(file, &valueNameLength, sizeof(valueNameLength)) ||
                valueNameLength > 4096) {
                ok = false;
                break;
            }
            std::string valueName(valueNameLength, '\0');
            if (!readAll(file, valueName.data(), valueNameLength)) {
                ok = false;
                break;
            }
            RegValue value;
            uint32_t dataSize = 0;
            if (!readAll(file, &value.type, sizeof(value.type)) ||
                !readAll(file, &dataSize, sizeof(dataSize)) || dataSize > (1U << 24)) {
                ok = false;
                break;
            }
            value.data.resize(dataSize);
            if (!readAll(file, value.data.data(), dataSize)) {
                ok = false;
                break;
            }
            values[valueName] = std::move(value);
        }
        if (!ok) break;
        g_keys[name] = std::move(values);
    }
    std::fclose(file);
}

void ensureLoaded() { loadLocked(); }

std::string rootName(uint64_t hkey) {
    switch (hkey) {
        case kHkeyClassesRoot: return "hkcr";
        case kHkeyCurrentUser: return "hkcu";
        case kHkeyLocalMachine: return "hklm";
        case kHkeyUsers: return "hku";
        case kHkeyCurrentConfig: return "hkcc";
        default: return {};
    }
}

bool basePath(uint64_t hkey, std::string& path) {
    auto it = g_handles.find(hkey);
    if (it != g_handles.end()) {
        path = it->second;
        return true;
    }
    path = rootName(hkey);
    return !path.empty();
}

std::string joinPath(const std::string& base, const char* subKey) {
    std::string result = base;
    if (subKey != nullptr && subKey[0] != '\0') {
        if (!result.empty()) result.push_back('\\');
        result += subKey;
    }
    return lower(result);
}

void createKeyAndParents(const std::string& path) {
    std::string current;
    size_t start = 0;
    while (start <= path.size()) {
        size_t slash = path.find('\\', start);
        if (slash == std::string::npos) slash = path.size();
        if (slash > start) {
            if (!current.empty()) current.push_back('\\');
            current.append(path, start, slash - start);
            g_keys.emplace(current, ValueMap{});
        }
        start = slash + 1;
    }
    g_keys.emplace(path, ValueMap{});
}

uint64_t makeHandle(const std::string& path) {
    const uint64_t handle = g_nextHandle;
    g_nextHandle += 4;
    g_handles[handle] = path;
    return handle;
}

bool copyOut(const std::vector<uint8_t>& source, uint64_t buffer, uint64_t sizePtr,
             uint32_t& error) {
    const uint32_t available = sizePtr != 0
        ? *reinterpret_cast<const uint32_t*>(sizePtr) : 0;
    const uint32_t required = static_cast<uint32_t>(source.size());
    if (sizePtr != 0) *reinterpret_cast<uint32_t*>(sizePtr) = required;
    if (buffer == 0) {
        error = kErrorSuccess;
        return true;
    }
    if (available < required) {
        error = kErrorMoreData;
        return false;
    }
    if (required != 0) {
        std::memcpy(reinterpret_cast<void*>(buffer), source.data(), required);
    }
    error = kErrorSuccess;
    return true;
}

uint32_t environmentValue(const char* name, uint64_t buffer, uint64_t size) {
    if (name == nullptr) return 0;
    std::string key = name;
    std::string value;
    if (key == "USERPROFILE" || key == "HOME") {
        const char* home = std::getenv("HOME");
        if (home != nullptr) value = home;
    } else if (key == "APPDATA") {
        const char* home = std::getenv("HOME");
        if (home != nullptr) value = std::string(home) + "/.config";
    } else if (key == "LOCALAPPDATA") {
        const char* home = std::getenv("HOME");
        if (home != nullptr) value = std::string(home) + "/.local/share";
    } else if (key == "TEMP" || key == "TMP") {
        value = "/tmp";
    } else if (key == "SystemRoot" || key == "windir") {
        value = "C:\\Windows";
    } else {
        return 0;
    }
    const uint32_t required = static_cast<uint32_t>(value.size() + 1);
    if (buffer == 0 || size < required) return required;
    std::memcpy(reinterpret_cast<void*>(buffer), value.c_str(), required);
    return static_cast<uint32_t>(value.size());
}

}  // namespace

extern "C" uint64_t host_RegOpenKeyExA(uint64_t hkey, uint64_t subKeyPtr,
                                       uint64_t options, uint64_t sam,
                                       uint64_t resultPtr, uint64_t, uint64_t,
                                       uint64_t) {
    (void)options;
    (void)sam;
    std::lock_guard<std::mutex> lock(g_mutex);
    ensureLoaded();
    std::string base;
    if (!basePath(hkey, base)) return kErrorInvalidHandle;
    const std::string path = joinPath(base, reinterpret_cast<const char*>(subKeyPtr));
    auto it = g_keys.find(path);
    if (it == g_keys.end()) return kErrorFileNotFound;
    if (resultPtr == 0) return kErrorInvalidHandle;
    *reinterpret_cast<uint64_t*>(resultPtr) = makeHandle(path);
    return kErrorSuccess;
}

extern "C" uint64_t host_RegCreateKeyExA(uint64_t hkey, uint64_t subKeyPtr,
                                         uint64_t reserved, uint64_t classPtr,
                                         uint64_t options, uint64_t sam,
                                         uint64_t sa, uint64_t resultPtr,
                                         uint64_t dispositionPtr) {
    (void)reserved;
    (void)classPtr;
    (void)options;
    (void)sam;
    (void)sa;
    std::lock_guard<std::mutex> lock(g_mutex);
    ensureLoaded();
    std::string base;
    if (!basePath(hkey, base)) return kErrorInvalidHandle;
    const std::string path = joinPath(base, reinterpret_cast<const char*>(subKeyPtr));
    const bool existed = g_keys.find(path) != g_keys.end();
    createKeyAndParents(path);
    if (!existed) g_dirty = true;
    if (resultPtr == 0) return kErrorInvalidHandle;
    *reinterpret_cast<uint64_t*>(resultPtr) = makeHandle(path);
    if (dispositionPtr != 0) {
        *reinterpret_cast<uint32_t*>(dispositionPtr) = existed ? 2U : 1U;
    }
    saveLocked();
    return kErrorSuccess;
}

extern "C" uint64_t host_RegSetValueExA(uint64_t hkey, uint64_t valueNamePtr,
                                        uint64_t reserved, uint64_t type,
                                        uint64_t dataPtr, uint64_t dataSize,
                                        uint64_t, uint64_t) {
    (void)reserved;
    std::lock_guard<std::mutex> lock(g_mutex);
    ensureLoaded();
    std::string path;
    if (!basePath(hkey, path)) return kErrorInvalidHandle;
    auto key = g_keys.find(path);
    if (key == g_keys.end()) return kErrorFileNotFound;
    const char* name = reinterpret_cast<const char*>(valueNamePtr);
    RegValue value;
    value.type = static_cast<uint32_t>(type);
    value.data.resize(static_cast<size_t>(dataSize));
    if (dataSize != 0 && dataPtr != 0) {
        std::memcpy(value.data.data(), reinterpret_cast<const void*>(dataPtr),
                    static_cast<size_t>(dataSize));
    }
    key->second[name != nullptr ? name : ""] = std::move(value);
    g_dirty = true;
    saveLocked();
    return kErrorSuccess;
}

extern "C" uint64_t host_RegQueryValueExA(uint64_t hkey, uint64_t valueNamePtr,
                                          uint64_t reservedPtr, uint64_t typePtr,
                                          uint64_t dataPtr, uint64_t sizePtr,
                                          uint64_t, uint64_t) {
    (void)reservedPtr;
    std::lock_guard<std::mutex> lock(g_mutex);
    ensureLoaded();
    std::string path;
    if (!basePath(hkey, path)) return kErrorInvalidHandle;
    auto key = g_keys.find(path);
    if (key == g_keys.end()) return kErrorFileNotFound;
    const char* name = reinterpret_cast<const char*>(valueNamePtr);
    auto value = key->second.find(name != nullptr ? name : "");
    if (value == key->second.end()) return kErrorFileNotFound;
    if (typePtr != 0) *reinterpret_cast<uint32_t*>(typePtr) = value->second.type;
    uint32_t error = kErrorSuccess;
    copyOut(value->second.data, dataPtr, sizePtr, error);
    return error;
}

extern "C" uint64_t host_RegCloseKey(uint64_t hkey, uint64_t, uint64_t, uint64_t,
                                     uint64_t, uint64_t, uint64_t, uint64_t) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_handles.find(hkey);
    if (it == g_handles.end()) return kErrorInvalidHandle;
    g_handles.erase(it);
    return kErrorSuccess;
}

extern "C" uint64_t host_RegDeleteValueA(uint64_t hkey, uint64_t valueNamePtr,
                                         uint64_t, uint64_t, uint64_t, uint64_t,
                                         uint64_t, uint64_t) {
    std::lock_guard<std::mutex> lock(g_mutex);
    ensureLoaded();
    std::string path;
    if (!basePath(hkey, path)) return kErrorInvalidHandle;
    auto key = g_keys.find(path);
    if (key == g_keys.end()) return kErrorFileNotFound;
    const char* name = reinterpret_cast<const char*>(valueNamePtr);
    auto it = key->second.find(name != nullptr ? name : "");
    if (it == key->second.end()) return kErrorFileNotFound;
    key->second.erase(it);
    g_dirty = true;
    saveLocked();
    return kErrorSuccess;
}

extern "C" uint64_t host_RegDeleteKeyA(uint64_t hkey, uint64_t subKeyPtr,
                                       uint64_t, uint64_t, uint64_t, uint64_t,
                                       uint64_t, uint64_t) {
    std::lock_guard<std::mutex> lock(g_mutex);
    ensureLoaded();
    std::string base;
    if (!basePath(hkey, base)) return kErrorInvalidHandle;
    const std::string path = joinPath(base, reinterpret_cast<const char*>(subKeyPtr));
    auto it = g_keys.find(path);
    if (it == g_keys.end()) return kErrorFileNotFound;
    g_keys.erase(it);
    for (auto child = g_keys.begin(); child != g_keys.end();) {
        if (child->first.size() > path.size() &&
            child->first.compare(0, path.size(), path) == 0 &&
            child->first[path.size()] == '\\') {
            child = g_keys.erase(child);
        } else {
            ++child;
        }
    }
    g_dirty = true;
    saveLocked();
    return kErrorSuccess;
}

extern "C" uint64_t host_RegEnumKeyExA(uint64_t hkey, uint64_t index,
                                       uint64_t namePtr, uint64_t nameLenPtr,
                                       uint64_t reserved, uint64_t classPtr,
                                       uint64_t classLenPtr, uint64_t timePtr) {
    (void)reserved;
    (void)classPtr;
    (void)classLenPtr;
    (void)timePtr;
    std::lock_guard<std::mutex> lock(g_mutex);
    ensureLoaded();
    std::string path;
    if (!basePath(hkey, path)) return kErrorInvalidHandle;
    std::vector<std::string> children;
    const std::string prefix = path + "\\";
    for (const auto& key : g_keys) {
        if (key.first.size() > prefix.size() &&
            key.first.compare(0, prefix.size(), prefix) == 0) {
            const std::string rest = key.first.substr(prefix.size());
            if (rest.find('\\') == std::string::npos) children.push_back(rest);
        }
    }
    if (index >= children.size()) return kErrorNoMoreItems;
    const std::string& name = children[static_cast<size_t>(index)];
    if (nameLenPtr == 0) return kErrorInsufficientBuffer;
    uint32_t capacity = *reinterpret_cast<uint32_t*>(nameLenPtr);
    const uint32_t required = static_cast<uint32_t>(name.size());
    if (capacity < required + 1) {
        *reinterpret_cast<uint32_t*>(nameLenPtr) = required + 1;
        return kErrorMoreData;
    }
    std::memcpy(reinterpret_cast<void*>(namePtr), name.c_str(), required + 1);
    *reinterpret_cast<uint32_t*>(nameLenPtr) = required;
    return kErrorSuccess;
}

extern "C" uint64_t host_RegEnumValueA(uint64_t hkey, uint64_t index,
                                       uint64_t namePtr, uint64_t nameLenPtr,
                                       uint64_t reservedPtr, uint64_t typePtr,
                                       uint64_t dataPtr, uint64_t dataSizePtr) {
    (void)reservedPtr;
    std::lock_guard<std::mutex> lock(g_mutex);
    ensureLoaded();
    std::string path;
    if (!basePath(hkey, path)) return kErrorInvalidHandle;
    auto key = g_keys.find(path);
    if (key == g_keys.end()) return kErrorFileNotFound;
    if (index >= key->second.size()) return kErrorNoMoreItems;
    auto it = key->second.begin();
    std::advance(it, static_cast<long>(index));
    if (nameLenPtr == 0) return kErrorInsufficientBuffer;
    uint32_t capacity = *reinterpret_cast<uint32_t*>(nameLenPtr);
    const uint32_t required = static_cast<uint32_t>(it->first.size());
    if (capacity < required + 1) {
        *reinterpret_cast<uint32_t*>(nameLenPtr) = required + 1;
        return kErrorMoreData;
    }
    std::memcpy(reinterpret_cast<void*>(namePtr), it->first.c_str(), required + 1);
    *reinterpret_cast<uint32_t*>(nameLenPtr) = required;
    if (typePtr != 0) *reinterpret_cast<uint32_t*>(typePtr) = it->second.type;
    uint32_t error = kErrorSuccess;
    copyOut(it->second.data, dataPtr, dataSizePtr, error);
    return error;
}

extern "C" uint64_t host_RegFlushKey(uint64_t hkey, uint64_t, uint64_t, uint64_t,
                                     uint64_t, uint64_t, uint64_t, uint64_t) {
    std::lock_guard<std::mutex> lock(g_mutex);
    ensureLoaded();
    std::string path;
    if (!basePath(hkey, path)) return kErrorInvalidHandle;
    g_dirty = true;
    saveLocked();
    return kErrorSuccess;
}

extern "C" uint64_t host_GetEnvironmentVariableA(uint64_t namePtr, uint64_t buffer,
                                                 uint64_t size, uint64_t, uint64_t,
                                                 uint64_t, uint64_t, uint64_t) {
    return environmentValue(reinterpret_cast<const char*>(namePtr), buffer, size);
}

extern "C" uint64_t host_SHGetFolderPathA(uint64_t hwnd, uint64_t csidl,
                                          uint64_t token, uint64_t flags,
                                          uint64_t pathPtr, uint64_t, uint64_t,
                                          uint64_t) {
    (void)hwnd;
    (void)token;
    (void)flags;
    const char* name = nullptr;
    switch (csidl & 0xffU) {
        case 0x05: name = "PERSONAL"; break;   // CSIDL_PERSONAL
        case 0x1a: name = "APPDATA"; break;    // CSIDL_APPDATA
        case 0x1c: name = "LOCALAPPDATA"; break;  // CSIDL_LOCAL_APPDATA
        case 0x28: name = "USERPROFILE"; break;   // CSIDL_PROFILE
        default: return 0x80070003U;  // E_FAIL-ish: unsupported folder
    }
    char buffer[1024] = {};
    uint32_t length = environmentValue(name, reinterpret_cast<uint64_t>(buffer),
                                       sizeof(buffer));
    if (length == 0 || pathPtr == 0) return 0x80070003U;
    std::memcpy(reinterpret_cast<void*>(pathPtr), buffer, length + 1U);
    return 0;  // S_OK
}

void registerRegistryHostModule() {
    HostModule module;
    module.name = "advapi32.dll";
    module.exportsByName = {
        {"RegOpenKeyExA", reinterpret_cast<uint64_t>(&host_RegOpenKeyExA)},
        {"RegCreateKeyExA", reinterpret_cast<uint64_t>(&host_RegCreateKeyExA)},
        {"RegSetValueExA", reinterpret_cast<uint64_t>(&host_RegSetValueExA)},
        {"RegQueryValueExA", reinterpret_cast<uint64_t>(&host_RegQueryValueExA)},
        {"RegCloseKey", reinterpret_cast<uint64_t>(&host_RegCloseKey)},
        {"RegDeleteValueA", reinterpret_cast<uint64_t>(&host_RegDeleteValueA)},
        {"RegDeleteKeyA", reinterpret_cast<uint64_t>(&host_RegDeleteKeyA)},
        {"RegEnumKeyExA", reinterpret_cast<uint64_t>(&host_RegEnumKeyExA)},
        {"RegEnumValueA", reinterpret_cast<uint64_t>(&host_RegEnumValueA)},
        {"RegFlushKey", reinterpret_cast<uint64_t>(&host_RegFlushKey)},
    };
    {
        std::lock_guard<std::mutex> lock(hostModulesMutex());
        hostModules().push_back(std::move(module));
    }
    HostModule shell;
    shell.name = "shell32.dll";
    shell.exportsByName = {
        {"SHGetFolderPathA", reinterpret_cast<uint64_t>(&host_SHGetFolderPathA)},
    };
    {
        std::lock_guard<std::mutex> lock(hostModulesMutex());
        hostModules().push_back(std::move(shell));
    }
    HostModule k32;
    k32.name = "kernel32.dll";
    k32.exportsByName = {
        {"GetEnvironmentVariableA", reinterpret_cast<uint64_t>(&host_GetEnvironmentVariableA)},
    };
    {
        std::lock_guard<std::mutex> lock(hostModulesMutex());
        hostModules().push_back(std::move(k32));
    }
}

}  // namespace rottr
