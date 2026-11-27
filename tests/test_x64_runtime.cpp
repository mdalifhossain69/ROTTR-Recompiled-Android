#include "rottr/x64_runtime.h"

#include <cstdio>
#include <string>

int main() {
    if (rottr::hasWindowsX64ExecutionBackend()) {
        std::puts("Unexpected: no x64 execution backend is linked in this build");
        return 1;
    }
    const std::string report = rottr::describeWindowsX64ExecutionBackend();
    if (report.find("Game CPU: Windows x86-64") == std::string::npos ||
        report.find("x86-64 backend: NOT LINKED") == std::string::npos) {
        std::puts("Missing or misleading x64 backend status");
        return 1;
    }
    std::puts("OK");
    return 0;
}
