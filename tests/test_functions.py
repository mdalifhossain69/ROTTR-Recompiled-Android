"""Synthetic PE32+ with a two-entry exception table."""
import os
import struct
import sys
import unittest

here = os.path.dirname(__file__)
sys.path[:0] = [here, os.path.join(here, "..", "scripts")]
import functions  # noqa: E402
from test_recon import make_pe  # noqa: E402


def table(section=b".pdata"):
    body = bytearray(0x200)
    struct.pack_into("<IIIIII", body, 0, 0x2000, 0x2040, 0x1100,
                     0x2040, 0x2050, 0x1104)
    body[0x100] = 0x01                  # version 1, no flags
    body[0x104] = 0x01 | (0x4 << 3)     # version 1, chained
    data = bytearray(make_pe(section, body))
    struct.pack_into("<I", data, 0x98 + 120, 0)  # no import directory
    return bytes(data)


class FunctionsTest(unittest.TestCase):
    def test_ranges_and_chaining(self):
        _, result = functions.functions(table())
        self.assertEqual(result, [(0x2000, 0x2040, False), (0x2040, 0x2050, True)])

    def test_missing_table(self):
        with self.assertRaises(ValueError):
            functions.functions(table(b".text"))

    def test_protected_file_is_refused(self):
        with self.assertRaises(functions.Protected):
            functions.functions(table(b".bind"))


if __name__ == "__main__":
    unittest.main()
