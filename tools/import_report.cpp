// Roadmap step 45: command-line import coverage report.
#include "rottr/import_report.h"
#include "rottr/x64_bridge.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <windows-executable>\n", argv[0]);
        return 2;
    }
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)),
                                    std::istreambuf_iterator<char>());
    rottr::registerHostModules();
    const rottr::ImportReport report = rottr::buildImportReport(data.data(), data.size());
    const std::string text = rottr::formatImportReport(report);
    std::fputs(text.c_str(), stdout);
    return report.ok() ? 0 : 1;
}
