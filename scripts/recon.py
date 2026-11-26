#!/usr/bin/env python3
"""Read-only report on a user-supplied Windows executable.

Exit codes: 0 no protection indicators found, 1 unreadable or unsupported file,
2 protection indicators found (stop; this project does not remove protection).
"""
import argparse
import hashlib
import json
import math
import struct
import sys
from collections import Counter

MACHINES = {0x014C: "x86", 0x8664: "x86-64", 0xAA64: "arm64"}
EXECUTABLE = 0x20000000


def entropy(data):
    if not data:
        return 0.0
    n = len(data)
    return -sum(c / n * math.log2(c / n) for c in Counter(data).values())


def analyse(data):
    def unpack(fmt, off):
        if off < 0 or off + struct.calcsize(fmt) > len(data):
            raise ValueError("truncated or malformed PE file")
        return struct.unpack_from(fmt, data, off)

    if data[:2] != b"MZ":
        raise ValueError("not a Windows executable (no MZ header)")
    pe = unpack("<I", 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("PE signature not found")
    machine, nsections = unpack("<HH", pe + 4)
    opt_size = unpack("<H", pe + 20)[0]
    opt = pe + 24
    magic = unpack("<H", opt)[0]
    if magic not in (0x10B, 0x20B):
        raise ValueError("unknown optional header")
    wide = magic == 0x20B
    entry = unpack("<I", opt + 16)[0]
    ndirs = unpack("<I", opt + (108 if wide else 92))[0]

    sections = []
    for i in range(nsections):
        off = opt + opt_size + 40 * i
        name, vsize, vaddr, rsize, rptr = unpack("<8sIIII", off)
        flags = unpack("<I", off + 36)[0]
        sections.append({
            "name": name.rstrip(b"\0").decode("ascii", "replace"),
            "vaddr": vaddr, "vsize": vsize, "rsize": rsize, "rptr": rptr,
            "executable": bool(flags & EXECUTABLE),
            "entropy": round(entropy(data[rptr:rptr + rsize]), 3),
        })

    def file_offset(rva):
        for s in sections:
            if s["vaddr"] <= rva < s["vaddr"] + s["rsize"]:
                return s["rptr"] + rva - s["vaddr"]
        raise ValueError("address outside file data")

    imports = []
    if ndirs > 1:
        rva = unpack("<I", opt + (112 if wide else 96) + 8)[0]
        if rva:
            try:
                off = file_offset(rva)
                for i in range(1024):
                    name_rva = unpack("<I", off + 20 * i + 12)[0]
                    if not name_rva:
                        break
                    start = file_offset(name_rva)
                    end = data.index(b"\0", start)
                    imports.append(data[start:end].decode("ascii", "replace"))
            except ValueError:
                imports.append("<import table unreadable>")

    findings = []
    for s in sections:
        if s["name"] == ".bind":
            findings.append(".bind section (typical of the SteamStub wrapper)")
        if s["executable"] and s["entropy"] > 7.2:
            findings.append(f"executable section {s['name']!r} looks packed or encrypted")
    if "<import table unreadable>" in imports:
        findings.append("import table unreadable")

    return {
        "sha256": hashlib.sha256(data).hexdigest(),
        "size": len(data),
        "machine": MACHINES.get(machine, hex(machine)),
        "format": "PE32+" if wide else "PE32",
        "entry_rva": hex(entry),
        "sections": sections,
        "imports": imports,
        "protection_indicators": findings,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("exe")
    args = parser.parse_args(argv)
    try:
        with open(args.exe, "rb") as f:
            report = analyse(f.read())
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(json.dumps(report, indent=2))
    if report["protection_indicators"]:
        print("STOP: protection indicators found.", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
