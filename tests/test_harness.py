"""End-to-end check of the lift-and-compare harness with a synthetic executable.

Needs the 'unicorn' Python package, a C++ compiler and the Remill headers
(REMILL_INCLUDE, default tools/remill-src/include). Skips itself if any is missing.
"""
import io
import contextlib
import os
import struct
import sys
import tempfile
import unittest

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.join(here, "..")
sys.path[:0] = [here, os.path.join(root, "scripts")]
INCLUDE = os.environ.get("REMILL_INCLUDE", os.path.join(root, "tools/remill-src/include"))

try:
    import unicorn  # noqa: F401
    import build_harness
    import compare_lifted
    from test_recon import make_pe
    import shutil
    MISSING = None if (os.path.isfile(os.path.join(INCLUDE, "remill/Arch/X86/Runtime/State.h"))
                       and (shutil.which("clang++") or shutil.which("clang++-17"))) \
        else "Remill headers or clang++ not found"
except ImportError as error:
    MISSING = f"missing dependency: {error}"

HEADER = """target datalayout = "e-m:w-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-windows-msvc-coff"
declare i64 @__remill_read_memory_64(ptr, i64)
declare ptr @__remill_function_return(ptr, i64, ptr)
declare i8 @llvm.ctpop.i8(i8)
"""

# State offsets: gpr.rax 2216, rcx 2248, rsp 2312, rip 2472; flags cf 2065 pf 2067
# af 2069 zf 2071 sf 2073 of 2077.
ADD_RAX_RCX = """define ptr @sub_%(rva)x(ptr noalias %%state, i64 %%program_counter, ptr noalias %%memory) {
  %%rax.p = getelementptr inbounds i8, ptr %%state, i64 2216
  %%rcx.p = getelementptr inbounds i8, ptr %%state, i64 2248
  %%rsp.p = getelementptr inbounds i8, ptr %%state, i64 2312
  %%rip.p = getelementptr inbounds i8, ptr %%state, i64 2472
  %%rax = load i64, ptr %%rax.p
  %%rcx = load i64, ptr %%rcx.p
  %%sum = add i64 %%rax, %%rcx
  store i64 %%sum, ptr %%rax.p
  %%cf = icmp ult i64 %%sum, %%rax
  %%zf = icmp eq i64 %%sum, 0
  %%sf = icmp slt i64 %%sum, 0
  %%x1 = xor i64 %%rax, %%sum
  %%x2 = xor i64 %%rcx, %%sum
  %%x3 = and i64 %%x1, %%x2
  %%of = icmp slt i64 %%x3, 0
  %%lo = trunc i64 %%sum to i8
  %%pc8 = call i8 @llvm.ctpop.i8(i8 %%lo)
  %%pc1 = and i8 %%pc8, 1
  %%pf = xor i8 %%pc1, 1
  %%a1 = xor i64 %%rax, %%rcx
  %%a2 = xor i64 %%a1, %%sum
  %%a3 = lshr i64 %%a2, 4
  %%a4 = trunc i64 %%a3 to i8
  %%af = and i8 %%a4, 1
  %%cf8 = zext i1 %%cf to i8
  %%zf8 = zext i1 %%zf to i8
  %%sf8 = zext i1 %%sf to i8
  %%of8 = zext i1 %%of to i8
  %%cf.p = getelementptr inbounds i8, ptr %%state, i64 2065
  %%pf.p = getelementptr inbounds i8, ptr %%state, i64 2067
  %%af.p = getelementptr inbounds i8, ptr %%state, i64 2069
  %%zf.p = getelementptr inbounds i8, ptr %%state, i64 2071
  %%sf.p = getelementptr inbounds i8, ptr %%state, i64 2073
  %%of.p = getelementptr inbounds i8, ptr %%state, i64 2077
  store i8 %%cf8, ptr %%cf.p
  store i8 %%pf, ptr %%pf.p
  store i8 %%af, ptr %%af.p
  store i8 %%zf8, ptr %%zf.p
  store i8 %%sf8, ptr %%sf.p
  store i8 %%of8, ptr %%of.p
  %%rsp = load i64, ptr %%rsp.p
  %%ra = call i64 @__remill_read_memory_64(ptr %%memory, i64 %%rsp)
  %%nrsp = add i64 %%rsp, 8
  store i64 %%nrsp, ptr %%rsp.p
  store i64 %%ra, ptr %%rip.p
  %%m = call ptr @__remill_function_return(ptr %%state, i64 %%ra, ptr %%memory)
  ret ptr %%m
}
"""

CALLER = """declare ptr @sub_3000(ptr noalias, i64, ptr noalias)
define ptr @sub_4000(ptr noalias %state, i64 %program_counter, ptr noalias %memory) {
  %m = call ptr @sub_3000(ptr %state, i64 12288, ptr %memory)
  ret ptr %m
}
"""


def synthetic_exe():
    body = bytearray(0x4000)
    body[0x0000:0x0004] = bytes.fromhex("4801c8c3")  # add rax, rcx ; ret
    body[0x1000:0x1004] = bytes.fromhex("4829c8c3")  # sub rax, rcx ; ret
    data = bytearray(make_pe(b".text", body))
    struct.pack_into("<I", data, 0x98 + 120, 0)      # no import directory
    return bytes(data)


@unittest.skipIf(MISSING, MISSING or "")
class HarnessTest(unittest.TestCase):
    def test_matching_wrong_and_skipped_functions(self):
        with tempfile.TemporaryDirectory() as tmp:
            lift = os.path.join(tmp, "lift")
            os.makedirs(lift)
            for rva, text in ((0x1000, ADD_RAX_RCX % {"rva": 0x1000}),   # correct
                              (0x2000, ADD_RAX_RCX % {"rva": 0x2000}),   # wrong: original subtracts
                              (0x4000, CALLER)):                         # calls elsewhere
                with open(os.path.join(lift, f"{rva:08x}.ll"), "w") as f:
                    f.write(HEADER + text)
            exe = os.path.join(tmp, "fake.exe")
            with open(exe, "wb") as f:
                f.write(synthetic_exe())
            out = os.path.join(tmp, "harness")
            self.assertEqual(build_harness.main(
                ["--lift-dir", lift, "--out", out, "--remill-include", INCLUDE]), 0)

            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                code = compare_lifted.main([exe, "--inputs", "60", "--lift-dir", lift,
                                            "--lib", os.path.join(out, "libharness.so")])
            text = buf.getvalue()
            print(text)
            lines = {l.split()[0]: l for l in text.splitlines() if l.strip().startswith("0x")}
            good, bad, skipped = lines["0x1000"].split(), lines["0x2000"].split(), lines["0x4000"]
            self.assertGreater(int(good[1]), 0)            # compared
            self.assertEqual(int(good[3]), 0)              # none differ
            self.assertGreater(int(bad[3]), 0)             # wrong code is caught
            self.assertIn("skipped: calls functions that were not lifted", skipped)
            self.assertEqual(code, 1)                      # overall failure because of 0x2000


if __name__ == "__main__":
    unittest.main()
