// Roadmap step 43: Add COM basics.
// Pass: reference counting and interface queries work. The test calls the
// object's real vtable (QueryInterface/AddRef/Release) and checks the exact
// documented success and failure behaviour.
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
namespace rottr {
uint64_t host_CoInitializeEx(uint64_t reserved, uint64_t flags, uint64_t, uint64_t,
                             uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t host_CoUninitialize(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                             uint64_t, uint64_t, uint64_t);
uint64_t host_CoCreateInstance(uint64_t clsid, uint64_t outer, uint64_t context,
                               uint64_t riid, uint64_t out, uint64_t, uint64_t,
                               uint64_t);
uint64_t host_CoTaskMemAlloc(uint64_t size, uint64_t, uint64_t, uint64_t, uint64_t,
                             uint64_t, uint64_t, uint64_t);
uint64_t host_CoTaskMemFree(uint64_t pointer, uint64_t, uint64_t, uint64_t,
                            uint64_t, uint64_t, uint64_t, uint64_t);
}  // namespace rottr
}

namespace {

constexpr uint64_t kSOk = 0;
constexpr uint64_t kENoInterface = 0x80004002ULL;
constexpr uint64_t kClassNotRegistered = 0x80040154ULL;

const uint8_t kClassId[16] = {0xdd, 0xcc, 0xbb, 0xaa, 0x11, 0x22, 0x33, 0x44,
                              0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
const uint8_t kIidUnknown[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46};
const uint8_t kIidTest[16] = {0x55, 0x55, 0x55, 0x55, 0x66, 0x66, 0x77, 0x77,
                              0x88, 0x88, 0x99, 0x99, 0xaa, 0xaa, 0xbb, 0xbb};
const uint8_t kIidUnsupported[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                     0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};

using QueryInterfaceFn = uint64_t (*)(uint64_t self, uint64_t riid, uint64_t out);
using RefFn = uint64_t (*)(uint64_t self);

bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

}  // namespace

int main() {
    if (!expect(rottr::host_CoInitializeEx(0, 2, 0, 0, 0, 0, 0, 0) == kSOk,
                "CoInitializeEx must succeed"))
        return 1;

    uint64_t object = 0;
    if (!expect(rottr::host_CoCreateInstance(
                    reinterpret_cast<uint64_t>(kClassId), 0, 1,
                    reinterpret_cast<uint64_t>(kIidUnknown),
                    reinterpret_cast<uint64_t>(&object), 0, 0, 0) == kSOk &&
                    object != 0,
                "CoCreateInstance must create the registered class"))
        return 1;

    auto** vtbl = *reinterpret_cast<uint64_t***>(object);
    if (!expect(vtbl != nullptr && vtbl[0] != 0 && vtbl[1] != 0 && vtbl[2] != 0,
                "object must expose a real QueryInterface/AddRef/Release vtable"))
        return 1;
    auto queryInterface = reinterpret_cast<QueryInterfaceFn>(vtbl[0]);
    auto addRef = reinterpret_cast<RefFn>(vtbl[1]);
    auto release = reinterpret_cast<RefFn>(vtbl[2]);

    // ---- Interface query for a supported interface ----
    uint64_t second = 0;
    if (!expect(queryInterface(object, reinterpret_cast<uint64_t>(kIidTest),
                               reinterpret_cast<uint64_t>(&second)) == kSOk &&
                    second == object,
                "QueryInterface(IID_Test) must return the same object"))
        return 1;

    // ---- Unsupported interface: E_NOINTERFACE, null out, no ref change ----
    uint64_t third = 0xdeadbeefULL;
    if (!expect(queryInterface(object, reinterpret_cast<uint64_t>(kIidUnsupported),
                               reinterpret_cast<uint64_t>(&third)) == kENoInterface &&
                    third == 0,
                "QueryInterface(unsupported) must return E_NOINTERFACE and null"))
        return 1;

    // ---- Reference counting: initial 1, QI added 1, then AddRef/Release ----
    if (!expect(addRef(object) == 3, "AddRef must return 3 after the QI")) return 1;
    if (!expect(release(object) == 2, "Release must return 2")) return 1;
    if (!expect(release(object) == 1, "Release must return 1")) return 1;
    if (!expect(release(object) == 0, "final Release must return 0 and destroy"))
        return 1;

    // ---- Failure paths ----
    uint64_t bad = 0xdeadbeefULL;
    if (!expect(rottr::host_CoCreateInstance(
                    reinterpret_cast<uint64_t>(kIidUnsupported), 0, 1,
                    reinterpret_cast<uint64_t>(kIidUnknown),
                    reinterpret_cast<uint64_t>(&bad), 0, 0, 0) ==
                    kClassNotRegistered &&
                    bad == 0,
                "unregistered CLSID must return REGDB_E_CLASSNOTREG"))
        return 1;
    bad = 0xdeadbeefULL;
    if (!expect(rottr::host_CoCreateInstance(
                    reinterpret_cast<uint64_t>(kClassId), 0, 1,
                    reinterpret_cast<uint64_t>(kIidUnsupported),
                    reinterpret_cast<uint64_t>(&bad), 0, 0, 0) == kENoInterface &&
                    bad == 0,
                "unsupported IID must return E_NOINTERFACE"))
        return 1;

    // ---- CoTaskMem ----
    uint64_t memory = rottr::host_CoTaskMemAlloc(64, 0, 0, 0, 0, 0, 0, 0);
    if (!expect(memory != 0, "CoTaskMemAlloc must return memory")) return 1;
    std::memset(reinterpret_cast<void*>(memory), 0x5a, 64);
    if (!expect(*reinterpret_cast<uint8_t*>(memory) == 0x5a,
                "CoTaskMemAlloc memory must be writable"))
        return 1;
    rottr::host_CoTaskMemFree(memory, 0, 0, 0, 0, 0, 0, 0);

    rottr::host_CoUninitialize(0, 0, 0, 0, 0, 0, 0, 0);
    std::puts("OK (vtable QueryInterface, AddRef/Release counts, E_NOINTERFACE)");
    return 0;
}
