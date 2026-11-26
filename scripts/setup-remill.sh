#!/usr/bin/env bash
# Builds Remill, the x86-64 to LLVM lifter, into tools/remill.
# Check the README at https://github.com/lifting-bits/remill for the LLVM version it needs
# before running this. Requires git, cmake, ninja, and an LLVM build matching that version.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
src="$here/tools/remill-src"
if [ ! -d "$src/.git" ]; then
  git clone --depth 1 https://github.com/lifting-bits/remill "$src"
fi
cmake -S "$src" -B "$here/tools/remill-build" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$here/tools/remill-build"
echo "Remill built in $here/tools/remill-build"
