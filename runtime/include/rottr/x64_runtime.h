#pragma once

#include <string>

namespace rottr {

// Reports whether this Android build has a usable Windows x86-64 CPU backend.
// This is deliberately separate from PE inspection: recognizing an x64 PE does
// not mean the device can execute its instructions.
bool hasWindowsX64ExecutionBackend();
std::string describeWindowsX64ExecutionBackend();

}  // namespace rottr
