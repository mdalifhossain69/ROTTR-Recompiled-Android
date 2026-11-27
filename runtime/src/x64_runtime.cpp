#include "rottr/x64_runtime.h"

namespace rottr {

bool hasWindowsX64ExecutionBackend() {
    // The current APK contains neither Box64 nor FEXCore nor an ARM64EC/Wine
    // CPU bridge. Do not report readiness until a real backend is linked.
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
    if (hasWindowsX64ExecutionBackend()) {
        report += "x86-64 backend: available\n";
    } else {
        report += "x86-64 backend: NOT LINKED\n"
                  "ROTTR.exe cannot execute yet. Vulkan presentation does not "
                  "translate or execute x86-64 CPU instructions.\n";
    }
    return report;
}

}  // namespace rottr
