// Synthetic PE32+ images for host tests. No game data is embedded here.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace pe_fixture {

template <typename T>
inline void put(std::vector<uint8_t>& file, size_t at, T value) {
    std::memcpy(file.data() + at, &value, sizeof(value));
}

inline void text(std::vector<uint8_t>& file, size_t at, const char* value) {
    std::memcpy(file.data() + at, value, std::strlen(value) + 1U);
}

class Builder {
public:
    explicit Builder(uint64_t imageBase = 0x140000000ULL, uint32_t imageSize = 0x5000,
                     size_t fileSize = 0x4000)
        : file(fileSize, 0), imageBase_(imageBase), imageSize_(imageSize),
          fileAlignment_(0x200) {
        put<uint16_t>(file, 0, 0x5a4dU);
        put<uint32_t>(file, 0x3c, 0x80U);
        std::memcpy(file.data() + 0x80, "PE\0\0", 4);
        put<uint16_t>(file, 0x84, 0x8664U);
        put<uint16_t>(file, 0x94, 240U);  // size of optional header
        const size_t opt = 0x98;
        put<uint16_t>(file, opt, 0x20bU);
        put<uint32_t>(file, opt + 16U, 0x1000U);  // entry RVA
        put<uint64_t>(file, opt + 24U, imageBase);
        put<uint32_t>(file, opt + 32U, 0x1000U);  // section alignment
        put<uint32_t>(file, opt + 36U, fileAlignment_);
        put<uint32_t>(file, opt + 56U, imageSize);
        put<uint32_t>(file, opt + 60U, 0x400U);  // size of headers
        put<uint32_t>(file, opt + 108U, 16U);    // number of directories
    }

    void entry(uint32_t rva) { put<uint32_t>(file, 0x98 + 16U, rva); }

    void dll(bool value) {
        put<uint16_t>(file, 0x86, value ? 0x2102U : 0x0102U);
        // COFF header Characteristics field at peOffset + 22 = 0x96
        put<uint16_t>(file, 0x96, value ? 0x2000U : 0x0000U);
    }

    void directory(uint32_t index, uint32_t rva, uint32_t size) {
        put<uint32_t>(file, 0x98 + 112U + index * 8U, rva);
        put<uint32_t>(file, 0x98 + 116U + index * 8U, size);
    }

    void raw(size_t offset, std::initializer_list<uint8_t> bytes) {
        for (uint8_t byte : bytes) file[offset++] = byte;
    }

    void section(const char* name, uint32_t rva, uint32_t virtualSize,
                 uint32_t rawOffset, uint32_t rawSize, uint32_t characteristics) {
        const size_t header = 0x98 + 240U + static_cast<size_t>(sectionCount_) * 40U;
        std::memset(file.data() + header, 0, 40);
        std::strncpy(reinterpret_cast<char*>(file.data() + header), name, 8);
        put<uint32_t>(file, header + 8U, virtualSize);
        put<uint32_t>(file, header + 12U, rva);
        put<uint32_t>(file, header + 16U, rawSize);
        put<uint32_t>(file, header + 20U, rawOffset);
        put<uint32_t>(file, header + 36U, characteristics);
        ++sectionCount_;
        put<uint16_t>(file, 0x86, static_cast<uint16_t>(sectionCount_));
    }

    uint32_t rawoffsetFor(uint32_t rva) const { return rva - 0x2000U + 0x1000U; }

    std::vector<uint8_t> build() const { return file; }

    // Set the TLS directory (data directory index 9).
    void tlsDirectory(uint32_t rva, uint32_t size) {
        directory(9, rva, size);
    }

    std::vector<uint8_t> file;

private:
    uint64_t imageBase_ = 0;
    uint32_t imageSize_ = 0;
    uint32_t fileAlignment_ = 0x200;
    uint32_t sectionCount_ = 0;
};

inline Builder simpleImage() {
    Builder builder;
    builder.entry(0x1000);
    builder.section(".text", 0x1000, 0x800, 0x400, 0x400, 0x60000020U);
    builder.section(".data", 0x2000, 0x1000, 0x800, 0x200, 0xc0000040U);
    for (size_t i = 0; i < 0x400; ++i) builder.file[0x400 + i] = static_cast<uint8_t>(i);
    for (size_t i = 0; i < 0x200; ++i) builder.file[0x800 + i] = static_cast<uint8_t>(i ^ 0xa5U);
    return builder;
}

}  // namespace pe_fixture
