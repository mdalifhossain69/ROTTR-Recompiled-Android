// Uses a made-up PE32+ image only; no game data is needed.
#include "rottr/pe_inspect.h"

#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::printf("FAILED line %d: %s\n", __LINE__, #x); return 1; } } while (0)

template <typename T>
static void put(std::vector<uint8_t>& d, size_t at, T v) { std::memcpy(d.data() + at, &v, sizeof(v)); }

static std::vector<uint8_t> makePe(const char* section) {
    std::vector<uint8_t> d(0x600, 0);
    d[0] = 'M'; d[1] = 'Z';
    put<uint32_t>(d, 0x3C, 0x80);
    std::memcpy(&d[0x80], "PE\0\0", 4);
    put<uint16_t>(d, 0x84, 0x8664);
    put<uint16_t>(d, 0x86, 1);
    put<uint16_t>(d, 0x94, 240);
    const size_t opt = 0x98, head = opt + 240;
    put<uint16_t>(d, opt, 0x20B);
    put<uint32_t>(d, opt + 16, 0x1000);
    put<uint64_t>(d, opt + 24, 0x140000000ULL);
    put<uint32_t>(d, opt + 56, 0x2000);
    put<uint32_t>(d, opt + 108, 16);
    put<uint32_t>(d, opt + 120, 0x1000);
    std::strncpy(reinterpret_cast<char*>(&d[head]), section, 8);
    put<uint32_t>(d, head + 8, 0x200);
    put<uint32_t>(d, head + 12, 0x1000);
    put<uint32_t>(d, head + 16, 0x200);
    put<uint32_t>(d, head + 20, 0x400);
    put<uint32_t>(d, head + 36, 0x60000020);
    put<uint32_t>(d, 0x400, 0x1080);        // lookup table
    put<uint32_t>(d, 0x400 + 12, 0x1040);   // DLL name
    std::memcpy(&d[0x440], "KERNEL32.dll", 13);
    put<uint64_t>(d, 0x480, 0x10C0);        // two imported functions
    put<uint64_t>(d, 0x488, 0x10D0);
    return d;
}

int main() {
    const auto good = makePe(".text");
    const auto report = rottr::inspectPe(good.data(), good.size());
    CHECK(report.passed());
    CHECK(report.entryRva == 0x1000 && report.imageBase == 0x140000000ULL);
    CHECK(report.sections.size() == 1 && report.sections[0].executable);
    CHECK(report.imports.size() == 1 && report.imports[0].dll == "KERNEL32.dll");
    CHECK(report.imports[0].functions == 2);

    const auto bound = makePe(".bind");
    CHECK(!rottr::inspectPe(bound.data(), bound.size()).passed());

    auto packed = makePe(".text");
    for (size_t i = 0x500; i < 0x600; ++i) packed[i] = static_cast<uint8_t>(i);
    for (size_t i = 0x400; i < 0x500; ++i) packed[i] = static_cast<uint8_t>(i);
    CHECK(!rottr::inspectPe(packed.data(), packed.size()).passed());

    CHECK(!rottr::inspectPe(nullptr, 0).error.empty());
    CHECK(!rottr::inspectPe(good.data(), 0x90).error.empty());

    // Truncations and random damage must never read out of bounds (run under sanitizers).
    for (size_t n = 0; n <= good.size(); ++n) {
        const std::vector<uint8_t> cut(good.begin(), good.begin() + static_cast<long>(n));
        rottr::describePe(rottr::inspectPe(cut.data(), cut.size()));
    }
    std::mt19937 random(1);
    for (int round = 0; round < 100000; ++round) {
        auto damaged = good;
        for (int k = 0; k < 8; ++k)
            damaged[random() % damaged.size()] = static_cast<uint8_t>(random());
        if (round % 2) put<uint32_t>(damaged, random() % (damaged.size() - 4), 0xFFFFFFFFU);
        rottr::describePe(rottr::inspectPe(damaged.data(), damaged.size()));
    }
    std::puts("OK");
    return 0;
}
