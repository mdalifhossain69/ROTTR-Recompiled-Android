// Roadmap step 33: Implement heaps.
// Pass: a million random allocate and free operations show no corruption.
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>

extern "C" {
uint64_t host_HeapAlloc(uint64_t heap, uint64_t flags, uint64_t bytes,
                        uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t host_HeapFree(uint64_t heap, uint64_t flags, uint64_t mem,
                       uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t host_GetProcessHeap(uint64_t, uint64_t, uint64_t, uint64_t,
                             uint64_t, uint64_t, uint64_t, uint64_t);
}

namespace {
[[maybe_unused]] bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}
}  // namespace

int main() {
    uint64_t heap = host_GetProcessHeap(0, 0, 0, 0, 0, 0, 0, 0);
    if (!expect(heap != 0, "GetProcessHeap must return a valid heap"))
        return 1;

    const int kMaxAllocs = 4096;
    const int kOps = 1000000;
    std::vector<uint64_t> allocs(kMaxAllocs, 0);
    std::vector<uint64_t> sizes(kMaxAllocs, 0);
    std::vector<uint8_t> patterns(kMaxAllocs, 0);

    uint32_t seed = 0x12345678;
    auto rng = [&]() -> uint32_t {
        seed = seed * 1103515245u + 12345u;
        return seed >> 16;
    };

    int live = 0;
    for (int op = 0; op < kOps; ++op) {
        uint32_t choice = rng() % 3;
        int idx = rng() % kMaxAllocs;

        if (choice == 0 && live < kMaxAllocs && allocs[idx] == 0) {
            uint64_t size = 1 + (rng() % 256);
            uint64_t ptr = host_HeapAlloc(heap, 0, size, 0, 0, 0, 0, 0);
            if (!expect(ptr != 0, "HeapAlloc must return non-null"))
                return 1;
            uint8_t* p = reinterpret_cast<uint8_t*>(ptr);
            uint8_t pat = static_cast<uint8_t>(rng() & 0xff);
            for (uint64_t i = 0; i < size; ++i) p[i] = pat;
            allocs[idx] = ptr;
            sizes[idx] = size;
            patterns[idx] = pat;
            ++live;
        } else if (choice == 1 && allocs[idx] != 0) {
            host_HeapFree(heap, 0, allocs[idx], 0, 0, 0, 0, 0);
            allocs[idx] = 0;
            sizes[idx] = 0;
            patterns[idx] = 0;
            --live;
        } else if (choice == 2 && allocs[idx] != 0) {
            uint8_t* p = reinterpret_cast<uint8_t*>(allocs[idx]);
            bool ok = true;
            for (uint64_t i = 0; i < sizes[idx]; ++i) {
                if (p[i] != patterns[idx]) { ok = false; break; }
            }
            if (!expect(ok, "Heap allocation must preserve written data"))
                return 1;
        }
    }

    for (int i = 0; i < kMaxAllocs; ++i) {
        if (allocs[i] != 0) {
            host_HeapFree(heap, 0, allocs[i], 0, 0, 0, 0, 0);
        }
    }

    std::printf("OK (%d operations, %d live at end)\n", kOps, live);
    return 0;
}
