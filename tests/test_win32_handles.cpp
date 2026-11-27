// Roadmap step 32: Implement handles.
// Pass: invalid and closed handles return correct errors.
#include <cstdio>
#include <cstdint>

extern "C" {
uint64_t host_CreateFileA(uint64_t namePtr, uint64_t access,
                          uint64_t share, uint64_t sa,
                          uint64_t disp, uint64_t flags,
                          uint64_t templateFile, uint64_t);
uint64_t host_CloseHandle(uint64_t handle, uint64_t, uint64_t,
                          uint64_t, uint64_t, uint64_t,
                          uint64_t, uint64_t);
}

namespace {
[[maybe_unused]] bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}
}  // namespace

int main() {
    uint64_t h = 0;

    // Invalid handle: CloseHandle on a handle that was never created.
    if (!expect(host_CloseHandle(0xdeadbeef, 0, 0, 0, 0, 0, 0, 0) == 0,
                "CloseHandle on invalid handle must return FALSE"))
        return 1;

    // Create a handle.
    h = host_CreateFileA(0, 0, 0, 0, 0, 0, 0, 0);
    if (!expect(h != 0 && h != static_cast<uint64_t>(-1),
                "CreateFileA must return a valid handle"))
        return 1;

    // Close it: must succeed.
    if (!expect(host_CloseHandle(h, 0, 0, 0, 0, 0, 0, 0) == 1,
                "CloseHandle on valid handle must return TRUE"))
        return 1;

    // Closed handle: CloseHandle again must fail.
    if (!expect(host_CloseHandle(h, 0, 0, 0, 0, 0, 0, 0) == 0,
                "CloseHandle on closed handle must return FALSE"))
        return 1;

    // Another invalid handle: 0 is never a valid handle.
    if (!expect(host_CloseHandle(0, 0, 0, 0, 0, 0, 0, 0) == 0,
                "CloseHandle on handle 0 must return FALSE"))
        return 1;

    std::puts("OK");
    return 0;
}
