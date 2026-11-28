// Roadmap step 35: Implement memory-mapped files.
// Pass: multiple views show correct data.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
namespace rottr {
uint64_t host_CreateFileA(uint64_t namePtr, uint64_t access, uint64_t share,
                          uint64_t sa, uint64_t disp, uint64_t flags,
                          uint64_t templateFile, uint64_t);
uint64_t host_CreateFileMappingA(uint64_t file, uint64_t sa, uint64_t protect,
                                 uint64_t maxHigh, uint64_t maxLow,
                                 uint64_t name, uint64_t, uint64_t);
uint64_t host_MapViewOfFile(uint64_t mapping, uint64_t access,
                            uint64_t offsetHigh, uint64_t offsetLow,
                            uint64_t bytes, uint64_t, uint64_t, uint64_t);
uint64_t host_UnmapViewOfFile(uint64_t address, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t host_CloseHandle(uint64_t handle, uint64_t, uint64_t, uint64_t,
                          uint64_t, uint64_t, uint64_t, uint64_t);
}  // namespace rottr
}

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}
constexpr uint64_t kGenericReadWrite = 0x80000000ULL | 0x40000000ULL;
constexpr uint64_t kPageReadWrite = 0x04;
constexpr uint64_t kFileMapAllAccess = 0x000F001FULL;
}  // namespace

int main() {
    constexpr size_t kSize = 8192;
    const char* path = "/tmp/rottr_step35_map.bin";
    {
        std::vector<uint8_t> pattern(kSize);
        for (size_t i = 0; i < kSize; ++i) pattern[i] = static_cast<uint8_t>(i & 0xff);
        FILE* f = std::fopen(path, "wb");
        if (!f) { std::fprintf(stderr, "cannot create test file\n"); return 1; }
        std::fwrite(pattern.data(), 1, pattern.size(), f);
        std::fclose(f);
    }

    uint64_t file = rottr::host_CreateFileA(reinterpret_cast<uint64_t>(path),
                                            kGenericReadWrite, 0, 0, 3, 0, 0, 0);
    if (!expect(file != 0 && file != static_cast<uint64_t>(-1),
                "CreateFileA must succeed"))
        return 1;

    uint64_t mapping = rottr::host_CreateFileMappingA(file, 0, kPageReadWrite,
                                                      0, 0, 0, 0, 0);
    if (!expect(mapping != 0, "CreateFileMappingA must succeed")) return 1;

    // ---- Two views over the same mapping ----
    uint64_t view1 = rottr::host_MapViewOfFile(mapping, kFileMapAllAccess,
                                               0, 0, 0, 0, 0, 0);
    uint64_t view2 = rottr::host_MapViewOfFile(mapping, kFileMapAllAccess,
                                               0, 0, 0, 0, 0, 0);
    if (!expect(view1 != 0 && view2 != 0, "MapViewOfFile must return two views"))
        return 1;
    if (!expect(view1 != view2, "two views must be distinct mappings"))
        return 1;

    const uint8_t* p1 = reinterpret_cast<const uint8_t*>(view1);
    const uint8_t* p2 = reinterpret_cast<const uint8_t*>(view2);
    bool dataOk = true;
    for (size_t i = 0; i < kSize; i += 97) {
        if (p1[i] != static_cast<uint8_t>(i & 0xff) ||
            p2[i] != static_cast<uint8_t>(i & 0xff)) {
            dataOk = false;
            std::fprintf(stderr, "  mismatch at %zu: view1=%u view2=%u\n",
                         i, p1[i], p2[i]);
            break;
        }
    }
    if (!expect(dataOk, "both views must show the file contents")) return 1;

    // ---- Writes through one view are visible through the other ----
    uint8_t* w1 = reinterpret_cast<uint8_t*>(view1);
    w1[100] = 0xAA;
    if (!expect(p2[100] == 0xAA,
                "a write through one view must be visible through the other"))
        return 1;

    // ---- A page-offset view shows the right data ----
    uint64_t viewOffset = rottr::host_MapViewOfFile(mapping, kFileMapAllAccess,
                                                    0, 4096, 4096, 0, 0, 0);
    if (!expect(viewOffset != 0, "MapViewOfFile at a page offset must succeed"))
        return 1;
    const uint8_t* pOff = reinterpret_cast<const uint8_t*>(viewOffset);
    if (!expect(pOff[0] == static_cast<uint8_t>(4096 & 0xff) &&
                    pOff[255] == static_cast<uint8_t>((4096 + 255) & 0xff),
                "offset view must show data from the requested file offset"))
        return 1;

    if (!expect(rottr::host_UnmapViewOfFile(view1, 0, 0, 0, 0, 0, 0, 0) != 0,
                "UnmapViewOfFile must succeed"))
        return 1;
    if (!expect(rottr::host_UnmapViewOfFile(view2, 0, 0, 0, 0, 0, 0, 0) != 0,
                "UnmapViewOfFile must succeed"))
        return 1;
    if (!expect(rottr::host_UnmapViewOfFile(viewOffset, 0, 0, 0, 0, 0, 0, 0) != 0,
                "UnmapViewOfFile must succeed"))
        return 1;

    rottr::host_CloseHandle(mapping, 0, 0, 0, 0, 0, 0, 0);
    rottr::host_CloseHandle(file, 0, 0, 0, 0, 0, 0, 0);

    std::puts("OK (two views, shared writes, offset view)");
    return 0;
}
