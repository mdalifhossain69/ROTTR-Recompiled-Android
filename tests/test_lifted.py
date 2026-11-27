"""Lift hand-written x86-64 machine code with Remill, then compare it natively.

No game data is used. Needs 'unicorn', clang++, the Remill headers and the
remill-lift tool (REMILL_INCLUDE, REMILL_LIFT; defaults under tools/remill-src).
Skips itself if any is missing, unless ROTTR_REQUIRE_REMILL=1 makes that a failure.
"""
import contextlib
import io
import os
import struct
import subprocess
import sys
import tempfile
import unittest

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.join(here, "..")
sys.path[:0] = [here, os.path.join(root, "scripts")]
from test_harness import MISSING as HARNESS_MISSING  # noqa: E402
from test_harness import INCLUDE  # noqa: E402

LIFTER = os.environ.get("REMILL_LIFT",
                        os.path.join(root, "tools/remill-src/build/bin/lift/remill-lift-17"))
MISSING = HARNESS_MISSING or (None if os.path.isfile(LIFTER) else "remill-lift not found")
REQUIRED = os.environ.get("ROTTR_REQUIRE_REMILL") == "1"

if not HARNESS_MISSING:
    import build_harness
    import compare_lifted
    from test_recon import make_pe

# RVA -> (what it exercises, machine code)
FUNCTIONS = {
    0x1000: ("signed branch, store on both paths", "4839d17d074889174889d0c348890f4889c8c3"),
    0x1100: ("unsigned conditional move", "4889c84839d1480f47c2c3"),
    0x1200: ("loads and 8/16/32/64-bit stores",
             "488b0648034608488947108947186689471c88471ec3"),
    0x1300: ("loop over memory", "83e13f31c085c9740703048effc975f98907c3"),
    0x1400: ("every common condition code",
             "31c039d10f94c00f9c070f9e47010f9f47020f9247030f9647040f9747050f934706"
             "0f9547070f9047080f984709c3"),
    0x1500: ("push and pop", "534889cb4801d34889d85bc3"),
    0x1600: ("locked exchange-and-add", "f0480fc107c3"),
    0x1700: ("vector load, add, store", "0f10060f58c10f1107c3"),
    # 0x1800 calls 0x1900, which calls 0x1a00.
    0x1800: ("nested call, outer", "4889c8e8f80000004801d0c3"),
    0x1900: ("nested call, middle", "488907e8f800000048ffc0c3"),
    0x1A00: ("nested call, leaf", "4801c0c3"),
}


def synthetic_exe():
    body = bytearray(0x1000)
    for rva, (_, code) in FUNCTIONS.items():
        raw = bytes.fromhex(code)
        body[rva - 0x1000:rva - 0x1000 + len(raw)] = raw
    data = bytearray(make_pe(b".text", body))
    struct.pack_into("<I", data, 0x98 + 120, 0)  # no import directory
    return bytes(data)


@unittest.skipIf(MISSING and not REQUIRED, MISSING or "")
class LiftedCodeTest(unittest.TestCase):
    def test_lifted_functions_match_the_emulator(self):
        self.assertIsNone(MISSING)
        with tempfile.TemporaryDirectory() as tmp:
            lift = os.path.join(tmp, "lift")
            os.makedirs(lift)
            for rva, (_, code) in FUNCTIONS.items():
                subprocess.run([LIFTER, "-arch", "amd64", "-os", "windows",
                                "-address", str(rva), "-bytes", code,
                                "-ir_out", os.path.join(lift, f"{rva:08x}.ll")],
                               check=True, capture_output=True)
            exe = os.path.join(tmp, "fake.exe")
            with open(exe, "wb") as f:
                f.write(synthetic_exe())
            out = os.path.join(tmp, "harness")
            build_harness.main(["--lift-dir", lift, "--out", out, "--remill-include", INCLUDE])

            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                code = compare_lifted.main(
                    [exe, "--inputs", "400", "--lift-dir", lift,
                     "--require", str(len(FUNCTIONS)),
                     "--lib", os.path.join(out, "libharness.so")])
            print(buf.getvalue())
            self.assertEqual(code, 0, buf.getvalue())


if __name__ == "__main__":
    unittest.main()
