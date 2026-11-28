// Roadmap step 43: COM basics.
//
// Implements a minimal in-process COM runtime: CoInitializeEx/CoUninitialize,
// CoCreateInstance for a registered class, CoTaskMemAlloc/Free, and objects
// whose first field is a real vtable of QueryInterface/AddRef/Release. Query
// Interface obeys the documented rules (AddRef on success, E_NOINTERFACE and
// a null out pointer on failure).

#include "rottr/x64_bridge.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <set>

namespace rottr {
namespace {

constexpr uint64_t kSOk = 0;
constexpr uint64_t kSFalse = 1;
constexpr uint64_t kENoInterface = 0x80004002ULL;
constexpr uint64_t kEInvalidArg = 0x80070057ULL;
constexpr uint64_t kClassNotRegistered = 0x80040154ULL;

constexpr uint8_t kClassId[16] = {0xdd, 0xcc, 0xbb, 0xaa, 0x11, 0x22, 0x33, 0x44,
                                  0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
constexpr uint8_t kIidUnknown[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                     0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46};
constexpr uint8_t kIidTest[16] = {0x55, 0x55, 0x55, 0x55, 0x66, 0x66, 0x77, 0x77,
                                  0x88, 0x88, 0x99, 0x99, 0xaa, 0xaa, 0xbb, 0xbb};

struct ComObject {
    uint64_t* vtbl;
    std::atomic<uint32_t> refs;
};

std::mutex g_mutex;
std::set<ComObject*> g_objects;
uint32_t g_initCount = 0;

bool sameGuid(uint64_t address, const uint8_t (&guid)[16]) {
    if (address == 0) return false;
    return std::memcmp(reinterpret_cast<const void*>(address), guid, 16) == 0;
}

bool supportedInterface(uint64_t riid) {
    return sameGuid(riid, kIidUnknown) || sameGuid(riid, kIidTest);
}

ComObject* fromThis(uint64_t self) {
    auto* object = reinterpret_cast<ComObject*>(self);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_objects.find(object) == g_objects.end()) return nullptr;
    return object;
}

extern "C" uint64_t comQueryInterface(uint64_t self, uint64_t riid, uint64_t out);
extern "C" uint64_t comAddRef(uint64_t self);
extern "C" uint64_t comRelease(uint64_t self);

uint64_t g_vtbl[3] = {
    reinterpret_cast<uint64_t>(&comQueryInterface),
    reinterpret_cast<uint64_t>(&comAddRef),
    reinterpret_cast<uint64_t>(&comRelease),
};

void destroy(ComObject* object) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_objects.erase(object);
    }
    delete object;
}

}  // namespace

extern "C" uint64_t comQueryInterface(uint64_t self, uint64_t riid, uint64_t out) {
    ComObject* object = fromThis(self);
    if (object == nullptr) return kEInvalidArg;
    if (out == 0) return kEInvalidArg;
    if (!supportedInterface(riid)) {
        *reinterpret_cast<uint64_t*>(out) = 0;
        return kENoInterface;
    }
    object->refs.fetch_add(1);
    *reinterpret_cast<uint64_t*>(out) = self;
    return kSOk;
}

extern "C" uint64_t comAddRef(uint64_t self) {
    ComObject* object = fromThis(self);
    if (object == nullptr) return 0;
    return object->refs.fetch_add(1) + 1;
}

extern "C" uint64_t comRelease(uint64_t self) {
    ComObject* object = fromThis(self);
    if (object == nullptr) return 0;
    const uint32_t remaining = object->refs.fetch_sub(1) - 1;
    if (remaining == 0) destroy(object);
    return remaining;
}

extern "C" uint64_t host_CoInitializeEx(uint64_t reserved, uint64_t flags, uint64_t,
                                        uint64_t, uint64_t, uint64_t, uint64_t,
                                        uint64_t) {
    (void)reserved;
    (void)flags;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_initCount += 1;
    return kSOk;
}

extern "C" uint64_t host_CoUninitialize(uint64_t, uint64_t, uint64_t, uint64_t,
                                        uint64_t, uint64_t, uint64_t, uint64_t) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_initCount > 0) g_initCount -= 1;
    return 0;
}

extern "C" uint64_t host_CoCreateInstance(uint64_t clsid, uint64_t outer,
                                          uint64_t context, uint64_t riid,
                                          uint64_t outPtr, uint64_t, uint64_t,
                                          uint64_t) {
    (void)outer;
    (void)context;
    if (outPtr != 0) *reinterpret_cast<uint64_t*>(outPtr) = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_initCount == 0) return 0x800401f0ULL;  // CO_E_NOTINITIALIZED
    }
    if (clsid == 0 || !sameGuid(clsid, kClassId)) return kClassNotRegistered;
    if (!supportedInterface(riid)) return kENoInterface;
    auto* object = new ComObject();
    object->vtbl = g_vtbl;
    object->refs.store(1);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_objects.insert(object);
    }
    if (outPtr == 0) {
        destroy(object);
        return kEInvalidArg;
    }
    *reinterpret_cast<uint64_t*>(outPtr) = reinterpret_cast<uint64_t>(object);
    return kSOk;
}

extern "C" uint64_t host_CoTaskMemAlloc(uint64_t size, uint64_t, uint64_t, uint64_t,
                                        uint64_t, uint64_t, uint64_t, uint64_t) {
    return reinterpret_cast<uint64_t>(std::malloc(static_cast<size_t>(size)));
}

extern "C" uint64_t host_CoTaskMemFree(uint64_t pointer, uint64_t, uint64_t,
                                       uint64_t, uint64_t, uint64_t, uint64_t,
                                       uint64_t) {
    std::free(reinterpret_cast<void*>(pointer));
    return 0;
}

void registerComHostModule() {
    HostModule module;
    module.name = "ole32.dll";
    module.exportsByName = {
        {"CoInitializeEx", reinterpret_cast<uint64_t>(&host_CoInitializeEx)},
        {"CoInitialize", reinterpret_cast<uint64_t>(&host_CoInitializeEx)},
        {"CoUninitialize", reinterpret_cast<uint64_t>(&host_CoUninitialize)},
        {"CoCreateInstance", reinterpret_cast<uint64_t>(&host_CoCreateInstance)},
        {"CoTaskMemAlloc", reinterpret_cast<uint64_t>(&host_CoTaskMemAlloc)},
        {"CoTaskMemFree", reinterpret_cast<uint64_t>(&host_CoTaskMemFree)},
    };
    std::lock_guard<std::mutex> lock(hostModulesMutex());
    hostModules().push_back(std::move(module));
}

}  // namespace rottr
