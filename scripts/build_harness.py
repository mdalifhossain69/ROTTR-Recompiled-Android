#!/usr/bin/env python3
"""Compile lifted IR plus the runtime shim into one shared library.

Usage: python3 scripts/build_harness.py [--lift-dir generated/lift] [--out generated/harness]

The IR is compiled for the machine this runs on (ARM64 on a phone). Functions that
the sample calls but does not contain get stubs that mark the run as 'external'.
"""
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")


def find_tool(names):
    for name in names:
        path = shutil.which(name)
        if path:
            return path
    raise SystemExit(f"error: none of {names} found in PATH")


def run(cmd):
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(f"command failed: {' '.join(cmd[:4])} ...\n{result.stderr[-2000:]}\n")
        raise SystemExit(1)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--lift-dir", default=os.path.join(ROOT, "generated/lift"))
    p.add_argument("--out", default=os.path.join(ROOT, "generated/harness"))
    p.add_argument("--remill-include", default=os.path.join(ROOT, "tools/remill-src/include"))
    p.add_argument("--clang")
    p.add_argument("--cxx")
    args = p.parse_args(argv)

    clang = args.clang or find_tool(["clang-17", "clang"])
    cxx = args.cxx or find_tool(["clang++-17", "clang++"])
    if not os.path.isfile(os.path.join(args.remill_include, "remill/Arch/X86/Runtime/State.h")):
        raise SystemExit(f"error: Remill headers not found in {args.remill_include}")

    sources = sorted(glob.glob(os.path.join(args.lift_dir, "*.ll")))
    if not sources:
        raise SystemExit(f"error: no .ll files in {args.lift_dir}")
    ir_dir = os.path.join(args.out, "ir")
    os.makedirs(ir_dir, exist_ok=True)

    defined, declared, objects = set(), set(), []
    used_helpers = set()
    for src in sources:
        with open(src) as f:
            text = f.read()
        defined.update(re.findall(r"^define\b[^\n]*@(sub_[0-9a-f]+)\(", text, re.M))
        declared.update(re.findall(r"^declare\b[^\n]*@(sub_[0-9a-f]+)\(", text, re.M))
        used_helpers.update(re.findall(r"@(__remill_\w+)\(", text))
        # Drop the x86 triple and data layout so the compiler uses this machine's.
        text = re.sub(r"^target (datalayout|triple) = [^\n]*\n", "", text, flags=re.M)
        name = os.path.splitext(os.path.basename(src))[0]
        patched = os.path.join(ir_dir, name + ".ll")
        with open(patched, "w") as f:
            f.write(text)
        obj = os.path.join(ir_dir, name + ".o")
        run([clang, "-c", "-x", "ir", "-O1", "-fPIC", "-w", patched, "-o", obj])
        objects.append(obj)

    with open(os.path.join(ROOT, "harness/shim.cpp")) as f:
        shim_text = f.read()
    shim_helpers = set(re.findall(r"\b(__remill_\w+)\s*\(", shim_text))
    missing_helpers = used_helpers - shim_helpers
    if missing_helpers:
        print(f"note: stubbing helpers the shim does not implement: {sorted(missing_helpers)}")

    stubs = os.path.join(args.out, "stubs.cpp")
    with open(stubs, "w") as f:
        f.write('#include <cstdint>\nextern "C" void harness_note_external(void);\n')
        for sym in sorted(missing_helpers):
            f.write(f'extern "C" void *{sym}(void *, uint64_t, void *m) '
                    f'{{ harness_note_external(); return m; }}\n')
        for sym in sorted(declared - defined):
            f.write(f'extern "C" void *{sym}(void *, uint64_t, void *m) '
                    f'{{ harness_note_external(); return m; }}\n')

    lib = os.path.join(args.out, "libharness.so")
    run([cxx, "-std=c++17", "-O2", "-fPIC", "-shared", "-w",
         f"-I{args.remill_include}", "-DADDRESS_SIZE_BITS=64",
         os.path.join(ROOT, "harness/shim.cpp"), stubs, *objects, "-o", lib])
    print(f"built {lib}: {len(defined)} lifted functions, {len(declared - defined)} stubbed callees")
    return 0


if __name__ == "__main__":
    sys.exit(main())
