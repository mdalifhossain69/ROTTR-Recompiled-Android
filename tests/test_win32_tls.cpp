// Roadmap step 16: Set up static TLS and TLS callbacks.
// Pass: callback runs before entry; TLS variable has its initial value.
#include "rottr/pe_loader.h"
#include "rottr/x64_bridge.h"

#include "pe_fixture.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}
}  // namespace

using namespace pe_fixture;

// Build a DLL with a TLS directory and one TLS callback.
// Layout (file offset == RVA, single .text section):
//   RVA 0x1000: entry point = ret (c3)
//   RVA 0x2000: TLS callback:
//     mov dword ptr [rip + 0xff6], 0x42   ; write marker at RVA 0x3000
//     ret                                 ; c3
//   RVA 0x3000: TLS data (4 bytes, initial value 0)
//   RVA 0x4000: TLS directory (48 bytes)
//   RVA 0x4030: TLS index DWORD (0)
//   RVA 0x4800: TLS callback array (16 bytes: callback RVA, 0)
static std::vector<uint8_t> buildTlsDll() {
    Builder builder(0x140000000ULL, 0x6000, 0x6000);
    builder.dll(true);
    builder.entry(0x1000);
    // rawOffset == rva so file offset == RVA, writable so TLS callback can store marker
    builder.section(".text", 0x1000, 0x4000, 0x1000, 0x4000, 0xC0000020U);

    // Entry point at RVA 0x1000 (file off 0x1000): ret
    builder.raw(0x1000, {0xc3});

    // TLS callback at RVA 0x2000 (file off 0x2000):
    //   C7 05 F6 0F 00 00 42 00 00 00   mov [rip+0xff6], 0x42  (target = RVA 0x3000)
    //   C3                              ret
    // RIP after mov = 0x200A; target = 0x200A + 0xff6 = 0x3000
    builder.raw(0x2000, {0xc7, 0x05, 0xf6, 0x0f, 0x00, 0x00,
                         0x42, 0x00, 0x00, 0x00, 0xc3});

    // TLS data at RVA 0x3000 (file off 0x3000): initial value 0
    put<uint32_t>(builder.file, 0x3000, 0u);

    // TLS directory at RVA 0x4000 (file off 0x4000), 48 bytes:
    // All fields are image-relative RVAs.
    put<uint64_t>(builder.file, 0x4000 + 0,  0x3000ULL);   // StartAddressOfRawData
    put<uint64_t>(builder.file, 0x4000 + 8,  0x3010ULL);   // EndAddressOfRawData
    put<uint64_t>(builder.file, 0x4000 + 16, 0x4030ULL);   // AddressOfIndex
    put<uint64_t>(builder.file, 0x4000 + 24, 0x4800ULL);   // AddressOfCallBacks (in-section)
    put<uint32_t>(builder.file, 0x4000 + 32, 0u);          // SizeOfZeroFill
    put<uint32_t>(builder.file, 0x4000 + 36, 0u);          // Characteristics

    // TLS index DWORD at RVA 0x4030 (file off 0x4030)
    put<uint32_t>(builder.file, 0x4030, 0u);

    // TLS callback array at RVA 0x4800 (file off 0x4800), within .text section:
    put<uint64_t>(builder.file, 0x4800, 0x2000ULL);        // callback RVA
    put<uint64_t>(builder.file, 0x4808, 0ULL);             // terminator

    // Data directory index 9 (TLS): RVA 0x4000, size 48
    builder.tlsDirectory(0x4000, 48);
    return builder.build();
}

int main() {
    rottr_guest_mem* memory = rottr_guest_reserve(32ULL << 20U);
    if (!expect(memory != nullptr, "guest reserve")) return 1;
    uintptr_t base = (uintptr_t)rottr_guest_base(memory);

    std::vector<uint8_t> dll = buildTlsDll();
    rottr::PeModuleRegistry registry(memory);
    registry.setFileOpener([&](const std::string& name,
                               std::vector<uint8_t>& bytes) -> bool {
        if (name == "tls.dll") { bytes = dll; return true; }
        return false;
    });

    // ---- Step 16a: Load tls.dll ----
    std::string error;
    rottr::PeImage* image = registry.loadModule("tls.dll", &error);
    if (!expect(image != nullptr, "tls.dll must load")) {
        std::fprintf(stderr, "  %s\n", error.c_str());
        return 1;
    }
    if (!expect(image->isDll(), "tls.dll must be a DLL")) return 1;
    if (!expect(image->hasTls(), "tls.dll must have a TLS directory")) return 1;

    // Verify the callback array was parsed
    std::vector<uint64_t> cbs = image->tlsCallbackAddresses();
    if (!expect(cbs.size() == 1, "exactly one TLS callback expected")) return 1;

    // ---- Step 16b: Verify TLS data is at its initial value (0) ----
    uint32_t tlsData = 0;
    if (!image->read(image->base() + 0x3000, &tlsData, sizeof(tlsData))) {
        std::fprintf(stderr, "FAIL: cannot read TLS data\n");
        return 1;
    }
    if (!expect(tlsData == 0, "TLS data must start at initial value 0")) return 1;

    // ---- Step 16c: Set up CPU and run TLS callbacks ----
    rottr_cpu cpu{};
    rottr_cpu_reset(&cpu, image->entryPoint(), base + 0x100000);
    rottr_guest_commit_pages(memory, base + 0x100000 - 4096, 4096);
    // Commit the page containing TLS data (RVA 0x3000).
    rottr_guest_commit_pages(memory, image->base() + 0x3000, 4096);
    rottr::X64GuestMemory guest{
        base, reinterpret_cast<uint8_t*>(rottr_guest_base(memory)),
        32ULL << 20U, true, true};
    if (!image->runTlsCallbacks(cpu, guest)) {
        std::fprintf(stderr, "FAIL: TLS callback execution failed\n");
        return 1;
    }

    // ---- Step 16d: Verify TLS callback ran (data now 0x42) ----
    if (!image->read(image->base() + 0x3000, &tlsData, sizeof(tlsData))) {
        std::fprintf(stderr, "FAIL: cannot read TLS data after callback\n");
        return 1;
    }
    if (!expect(tlsData == 0x42, "TLS callback must have written 0x42")) return 1;

    std::printf("OK (tls_callbacks=%zu)\n", cbs.size());
    rottr_guest_release(memory);
    return 0;
}
