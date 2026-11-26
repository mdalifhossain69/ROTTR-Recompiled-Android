"""Tests use a synthetic PE32+ file; no game data is needed."""
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "scripts"))
import recon  # noqa: E402


def make_pe(section=b".text", body=None):
    body = body if body is not None else bytearray(0x200)
    if len(body) == 0x200 and not any(body):
        struct.pack_into("<I", body, 12, 0x1000 + 0x40)  # import name RVA
        body[0x40:0x4D] = b"KERNEL32.dll\0"
    data = bytearray(0x400)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HH", data, 0x84, 0x8664, 1)
    struct.pack_into("<H", data, 0x94, 240)
    opt = 0x98
    struct.pack_into("<H", data, opt, 0x20B)
    struct.pack_into("<I", data, opt + 16, 0x1000)
    struct.pack_into("<I", data, opt + 108, 16)
    struct.pack_into("<I", data, opt + 120, 0x1000)  # import directory RVA
    head = opt + 240
    struct.pack_into("<8sIIII", data, head, section, len(body), 0x1000, len(body), 0x400)
    struct.pack_into("<I", data, head + 36, 0x60000020)
    return bytes(data) + bytes(body)


class ReconTest(unittest.TestCase):
    def test_plain_x64(self):
        report = recon.analyse(make_pe())
        self.assertEqual(report["machine"], "x86-64")
        self.assertEqual(report["format"], "PE32+")
        self.assertEqual(report["imports"], ["KERNEL32.dll"])
        self.assertEqual(report["protection_indicators"], [])

    def test_bind_section_is_flagged(self):
        self.assertTrue(recon.analyse(make_pe(b".bind"))["protection_indicators"])

    def test_high_entropy_code_is_flagged(self):
        report = recon.analyse(make_pe(body=bytearray(range(256)) * 16))
        self.assertTrue(report["protection_indicators"])

    def test_bad_input(self):
        for bad in (b"", b"hello", make_pe()[:0x90]):
            with self.assertRaises(ValueError):
                recon.analyse(bad)


if __name__ == "__main__":
    unittest.main()
