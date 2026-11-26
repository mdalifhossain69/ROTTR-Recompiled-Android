#!/usr/bin/env python3
"""Summarise the x86-64 exception table (.pdata), a first map of function ranges.

Leaf functions need no table entry, so this does not find every function.
Exit codes match recon.py.
"""
import argparse
import os
import struct
import sys

import recon

CHAINED = 0x4  # UNW_FLAG_CHAININFO


class Protected(Exception):
    pass


def functions(data):
    report = recon.analyse(data)
    if report["protection_indicators"]:
        raise Protected("protection indicators found")
    if report["machine"] != "x86-64":
        raise ValueError("only x86-64 files are supported")
    sections = report["sections"]
    pdata = next((s for s in sections if s["name"] == ".pdata"), None)
    if pdata is None:
        raise ValueError("no .pdata section")

    def offset(rva):
        for s in sections:
            if s["vaddr"] <= rva < s["vaddr"] + s["rsize"]:
                return s["rptr"] + rva - s["vaddr"]
        return None

    length = min(pdata["vsize"], pdata["rsize"])
    raw = data[pdata["rptr"]:pdata["rptr"] + length]
    raw = raw[:len(raw) - len(raw) % 12]
    result = []
    for begin, end, unwind in struct.iter_unpack("<III", raw):
        if begin == 0 or end <= begin:
            continue
        at = offset(unwind & ~1)
        chained = bool(unwind & 1) or (
            at is not None and at < len(data) and bool((data[at] >> 3) & CHAINED))
        result.append((begin, end, chained))
    return report, result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("exe")
    parser.add_argument("--out", help="write ranges as CSV (keep it under generated/)")
    args = parser.parse_args(argv)
    try:
        with open(args.exe, "rb") as f:
            report, result = functions(f.read())
        if args.out:
            os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
            with open(args.out, "w") as f:
                f.write("begin_rva,end_rva,chained\n")
                for begin, end, chained in result:
                    f.write(f"{begin:#x},{end:#x},{int(chained)}\n")
    except Protected as error:
        print(f"STOP: {error}.", file=sys.stderr)
        return 2
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    code = sum(s["vsize"] for s in report["sections"] if s["executable"])
    covered = sum(end - begin for begin, end, _ in result)
    primary = sum(1 for r in result if not r[2])
    sizes = sorted(end - begin for begin, end, chained in result if not chained)
    print(f"sha256          : {report['sha256']}")
    print(f"table entries   : {len(result)}")
    print(f"primary entries : {primary}")
    print(f"chained entries : {len(result) - primary}")
    print(f"code covered    : {covered} of {code} bytes ({100 * covered / max(code, 1):.1f}%)")
    if sizes:
        print(f"median primary  : {sizes[len(sizes) // 2]} bytes")
        print(f"largest primary : {sizes[-1]} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
