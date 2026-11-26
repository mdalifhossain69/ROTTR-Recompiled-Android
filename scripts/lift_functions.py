#!/usr/bin/env python3
"""Lift a sample of functions from the user's executable to LLVM IR with Remill.

Usage: python3 scripts/lift_functions.py path/to/ROTTR.exe [--count 10] [--lifter PATH]
Writes IR to generated/lift/ (keep it out of Git; it is derived from the game).
"""
import argparse
import csv
import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))
import recon  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
DEFAULT_LIFTER = os.path.join(ROOT, "tools/remill-src/build/bin/lift/remill-lift-17")


def read_functions(path, low, high):
    rows = []
    with open(path) as f:
        for r in csv.DictReader(f):
            if r["chained"] != "0":
                continue
            begin, end = int(r["begin_rva"], 16), int(r["end_rva"], 16)
            if low <= end - begin <= high:
                rows.append((begin, end))
    return rows


def file_offset(sections, rva):
    for s in sections:
        if s["vaddr"] <= rva < s["vaddr"] + s["rsize"]:
            return s["rptr"] + rva - s["vaddr"]
    raise ValueError(f"RVA {rva:#x} not in file data")


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("exe")
    p.add_argument("--count", type=int, default=10)
    p.add_argument("--lifter", default=DEFAULT_LIFTER)
    p.add_argument("--functions", default=os.path.join(ROOT, "generated/functions.csv"))
    args = p.parse_args(argv)

    with open(args.exe, "rb") as f:
        data = f.read()
    report = recon.analyse(data)
    if report["protection_indicators"]:
        print("STOP: protection indicators found.", file=sys.stderr)
        return 2

    candidates = read_functions(args.functions, 32, 256)
    # Spread the sample across the code instead of taking neighbours.
    step = max(1, len(candidates) // args.count)
    chosen = candidates[::step][:args.count]

    out = os.path.join(ROOT, "generated/lift")
    os.makedirs(out, exist_ok=True)
    failures = 0
    for begin, end in chosen:
        off = file_offset(report["sections"], begin)
        code = data[off:off + (end - begin)].hex()
        name = f"{begin:08x}"
        cmd = [args.lifter, "-arch", "amd64", "-os", "windows",
               "-address", str(begin), "-bytes", code,
               "-ir_out", os.path.join(out, name + ".ll")]
        result = subprocess.run(cmd, capture_output=True, text=True)
        status = "ok" if result.returncode == 0 else f"FAILED ({result.returncode})"
        failures += result.returncode != 0
        print(f"{name} {end - begin:5d} bytes  {status}")
        if result.returncode != 0:
            print(result.stderr.strip()[-400:], file=sys.stderr)
    print(f"done: {len(chosen) - failures} lifted, {failures} failed. IR in {out}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
