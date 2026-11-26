#!/usr/bin/env bash
# Builds Remill (x86-64 to LLVM lifter) and its dependencies, following the upstream README:
# https://github.com/lifting-bits/remill
# Tested target: Ubuntu 22.04 (run inside proot-distro). Needs LLVM/Clang 15+.
#
# Usage: LLVM_VERSION=17 bash scripts/setup-remill.sh
# Set USE_SYSTEM_LLVM=0 to let the superbuild compile LLVM itself (many hours, much RAM).
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
src="$here/tools/remill-src"
llvm="${LLVM_VERSION:-17}"
use_system="${USE_SYSTEM_LLVM:-1}"

if [ ! -d "$src/.git" ]; then
  git clone --depth 1 https://github.com/lifting-bits/remill "$src"
fi
cd "$src"

if [ "$use_system" = "1" ]; then
  if ! command -v "clang-$llvm" >/dev/null; then
    echo "clang-$llvm not found. Install it with: apt install clang-$llvm llvm-$llvm-dev libclang-$llvm-dev" >&2
    exit 1
  fi
  cmake -G Ninja -S dependencies -B dependencies/build -DUSE_EXTERNAL_LLVM=ON \
    -DLLVM_DIR="/usr/lib/llvm-$llvm/lib/cmake/llvm" -DCLANG_DIR="/usr/lib/llvm-$llvm/lib/cmake/clang"
else
  cmake -G Ninja -S dependencies -B dependencies/build
fi
cmake --build dependencies/build

cmake -G Ninja -B build "-DCMAKE_PREFIX_PATH:PATH=$src/dependencies/install" -DCMAKE_BUILD_TYPE=Release
cmake --build build
echo "Remill built in $src/build"
