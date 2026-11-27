#include "rottr/x64_runtime.h"

namespace rottr {

bool hasWindowsX64ExecutionBackend() {
    // The CPU interpreter is intentionally only a limited instruction subset.
    // There is still no PE loader, Windows ABI, DLL loader or full game runtime.
    return false;
}

std::string describeWindowsX64ExecutionBackend() {
#if defined(__aarch64__)
    const char* host = "Android ARM64";
#elif defined(__x86_64__)
    const char* host = "x86-64 host";
#else
    const char* host = "unsupported host architecture";
#endif

    std::string report = std::string("CPU host: ") + host +
            "\nGame CPU: Windows x86-64 (PE32+)\n";
    report += hasX64InstructionInterpreter()
            ? "x86-64 interpreter: available (limited instruction subset)\n"
            : "x86-64 interpreter: NOT LINKED\n";
    report += "Windows x86-64 game runtime: NOT AVAILABLE\n"
              "Missing: production ISA coverage, PE loader, Windows ABI/API, DLL loader.\n"
              "ROTTR.exe cannot execute yet.\n";
    return report;
}

}  // namespace rottr
