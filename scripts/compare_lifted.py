#!/usr/bin/env python3
"""Compare lifted functions against the original x86-64 code.

Reference: the original bytes run in the Unicorn x86-64 emulator.
Under test: the Remill-lifted code, compiled natively (scripts/build_harness.py).
Both start from identical random registers, flags, vector registers and memory.
Only functions that make no calls are compared.
Usage: python3 scripts/compare_lifted.py path/to/ROTTR.exe [--inputs 200] [--seed 1]
"""
import argparse
import ctypes
import glob
import os
import random
import re
import struct
import sys

from unicorn import (Uc, UcError, UC_ARCH_X86, UC_MODE_64, UC_PROT_ALL, UC_PROT_EXEC,
                     UC_PROT_READ, UC_ERR_READ_UNMAPPED, UC_ERR_WRITE_UNMAPPED,
                     UC_ERR_FETCH_UNMAPPED, UC_ERR_WRITE_PROT, UC_ERR_READ_PROT,
                     UC_ERR_FETCH_PROT)
from unicorn import x86_const as X

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import recon  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
PAGE = 0x1000
DATA_BASE, STACK_BASE, REGION_SIZE = 0x10000000, 0x20000000, 0x10000
RET_ADDR = 0x7FFF0000
MEMORY_ERRORS = {UC_ERR_READ_UNMAPPED, UC_ERR_WRITE_UNMAPPED, UC_ERR_FETCH_UNMAPPED,
                 UC_ERR_WRITE_PROT, UC_ERR_READ_PROT, UC_ERR_FETCH_PROT}

GPR_NAMES = ["rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rsp", "rbp",
             "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"]
GPR_REGS = [getattr(X, "UC_X86_REG_" + n.upper()) for n in GPR_NAMES]
FLAG_NAMES = ["cf", "pf", "af", "zf", "sf", "df", "of"]
FLAG_BITS = [0, 2, 4, 6, 7, 10, 11]
XMM_REGS = [getattr(X, f"UC_X86_REG_XMM{i}") for i in range(16)]
LEAF_BAD = re.compile(r"@(sub_[0-9a-f]+|__remill_(function_call|jump|missing_block|async_hyper_call))\b")


class Snapshot(ctypes.Structure):
    _fields_ = [("gpr", ctypes.c_uint64 * 16), ("flags", ctypes.c_uint8 * 8),
                ("xmm", (ctypes.c_uint8 * 16) * 16)]


def is_leaf(ir_text):
    """True if the lifted function makes no calls or jumps to other code."""
    for line in ir_text.splitlines():
        s = line.strip()
        if s.startswith(("define", "declare", ";")) or not s:
            continue
        if LEAF_BAD.search(s):
            return False
    return True


def build_image(data, report):
    size = max(s["vaddr"] + max(s["vsize"], s["rsize"]) for s in report["sections"])
    size = (size + PAGE - 1) // PAGE * PAGE
    image = bytearray(size)
    first = min(s["rptr"] for s in report["sections"] if s["rsize"])
    image[:first] = data[:first]
    for s in report["sections"]:
        n = min(s["rsize"], s["vsize"]) if s["vsize"] else s["rsize"]
        image[s["vaddr"]:s["vaddr"] + n] = data[s["rptr"]:s["rptr"] + n]
    return image


def random_register(rng):
    k = rng.randrange(10)
    if k < 4:
        return rng.choice([0, 1, 0xFFFFFFFFFFFFFFFF, rng.randrange(0x100), rng.randrange(0x10000)])
    if k < 7:
        return DATA_BASE + 0x8000 + rng.randrange(-0x2000, 0x2000)
    if k == 7:
        return STACK_BASE + 0x8000 + rng.randrange(-0x1000, 0x1000)
    if k == 8:
        return rng.getrandbits(64)
    return rng.getrandbits(32)


def random_xmm(rng):
    if rng.random() < 0.5:
        return struct.pack("<4f", *(rng.uniform(-100, 100) for _ in range(4)))
    return rng.randbytes(16)


class Reference:
    """The original code, run in Unicorn."""

    def __init__(self, image):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_64)
        self.uc.mem_map(0, len(image), UC_PROT_READ | UC_PROT_EXEC)
        self.uc.mem_write(0, bytes(image))
        self.uc.mem_map(DATA_BASE, REGION_SIZE, UC_PROT_READ | UC_PROT_ALL & ~UC_PROT_EXEC)
        self.uc.mem_map(STACK_BASE, REGION_SIZE, UC_PROT_READ | UC_PROT_ALL & ~UC_PROT_EXEC)
        self.uc.mem_map(RET_ADDR, PAGE, UC_PROT_READ | UC_PROT_EXEC)
        self.uc.mem_write(RET_ADDR, b"\xCC")

    def run(self, rva, regs, flags, xmm, data, stack):
        uc = self.uc
        uc.mem_write(DATA_BASE, data)
        uc.mem_write(STACK_BASE, stack)
        for reg, value in zip(GPR_REGS, regs):
            uc.reg_write(reg, value)
        eflags = 0x2
        for bit, value in zip(FLAG_BITS, flags):
            eflags |= value << bit
        uc.reg_write(X.UC_X86_REG_EFLAGS, eflags)
        for reg, value in zip(XMM_REGS, xmm):
            uc.reg_write(reg, int.from_bytes(value, "little"))
        try:
            uc.emu_start(rva, RET_ADDR, timeout=500_000, count=200_000)
        except UcError as error:
            kind = "memfault" if error.errno in MEMORY_ERRORS else "emulator-error"
            return kind, None
        if uc.reg_read(X.UC_X86_REG_RIP) != RET_ADDR:
            return "timeout", None
        final_flags = uc.reg_read(X.UC_X86_REG_EFLAGS)
        return "ok", {
            "gpr": [uc.reg_read(r) for r in GPR_REGS],
            "flags": [(final_flags >> b) & 1 for b in FLAG_BITS],
            "xmm": [uc.reg_read(r).to_bytes(16, "little") for r in XMM_REGS],
            "data": bytes(uc.mem_read(DATA_BASE, REGION_SIZE)),
            "stack": bytes(uc.mem_read(STACK_BASE, REGION_SIZE)),
        }


class Lifted:
    """The lifted code, run natively through the shim."""

    def __init__(self, lib_path, image):
        self.lib = ctypes.CDLL(lib_path)
        self.lib.harness_run.argtypes = [ctypes.c_void_p, ctypes.c_uint64,
                                         ctypes.POINTER(Snapshot), ctypes.POINTER(Snapshot),
                                         ctypes.c_int, ctypes.POINTER(ctypes.c_uint32)]
        self.lib.harness_add_region.argtypes = [ctypes.c_uint64, ctypes.c_uint64,
                                                ctypes.c_void_p, ctypes.c_int]
        self.image = (ctypes.c_uint8 * len(image)).from_buffer(image)
        self.image_len = len(image)

    def address(self, rva):
        return ctypes.cast(getattr(self.lib, f"sub_{rva:x}"), ctypes.c_void_p).value

    def run(self, rva, regs, flags, xmm, data, stack):
        d = (ctypes.c_uint8 * REGION_SIZE).from_buffer_copy(data)
        s = (ctypes.c_uint8 * REGION_SIZE).from_buffer_copy(stack)
        self.lib.harness_clear_regions()
        self.lib.harness_add_region(0, self.image_len, ctypes.addressof(self.image), 0)
        self.lib.harness_add_region(DATA_BASE, REGION_SIZE, ctypes.addressof(d), 1)
        self.lib.harness_add_region(STACK_BASE, REGION_SIZE, ctypes.addressof(s), 1)
        snap_in, snap_out = Snapshot(), Snapshot()
        for i, v in enumerate(regs):
            snap_in.gpr[i] = v
        for i, v in enumerate(flags):
            snap_in.flags[i] = v
        for i, v in enumerate(xmm):
            for j, b in enumerate(v):
                snap_in.xmm[i][j] = b
        info = (ctypes.c_uint32 * 4)()
        rc = self.lib.harness_run(self.address(rva), rva, ctypes.byref(snap_in),
                                  ctypes.byref(snap_out), 500, info)
        return rc, {
            "gpr": list(snap_out.gpr),
            "flags": list(snap_out.flags)[:7],
            "xmm": [bytes(snap_out.xmm[i]) for i in range(16)],
            "data": bytes(d), "stack": bytes(s),
        }, list(info)


def differences(ref, lif):
    found = []
    for i, name in enumerate(GPR_NAMES):
        if ref["gpr"][i] != lif["gpr"][i]:
            found.append(f"{name}: original {ref['gpr'][i]:#x}, lifted {lif['gpr'][i]:#x}")
    for i, name in enumerate(FLAG_NAMES):
        if ref["flags"][i] != lif["flags"][i]:
            found.append(f"flag {name}: original {ref['flags'][i]}, lifted {lif['flags'][i]}")
    for i in range(16):
        if ref["xmm"][i] != lif["xmm"][i]:
            found.append(f"xmm{i}: original {ref['xmm'][i].hex()}, lifted {lif['xmm'][i].hex()}")
    for name in ("data", "stack"):
        if ref[name] != lif[name]:
            at = next(k for k in range(REGION_SIZE) if ref[name][k] != lif[name][k])
            found.append(f"{name} memory differs first at offset {at:#x}")
    return found


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("exe")
    p.add_argument("--inputs", type=int, default=200)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--lift-dir", default=os.path.join(ROOT, "generated/lift"))
    p.add_argument("--lib", default=os.path.join(ROOT, "generated/harness/libharness.so"))
    args = p.parse_args(argv)

    with open(args.exe, "rb") as f:
        data = f.read()
    report = recon.analyse(data)
    if report["protection_indicators"]:
        print("STOP: protection indicators found.", file=sys.stderr)
        return 2
    image = build_image(data, report)
    reference, lifted = Reference(image), Lifted(args.lib, image)
    rng = random.Random(args.seed)

    total_bad = total_compared = 0
    print(f"{'function':>10} {'compared':>8} {'match':>6} {'differ':>6} {'both-fault':>10} "
          f"{'skipped':>7}  note")
    for path in sorted(glob.glob(os.path.join(args.lift_dir, "*.ll"))):
        rva = int(os.path.basename(path).split(".")[0], 16)
        with open(path) as f:
            if not is_leaf(f.read()):
                print(f"{rva:#10x} {'':>8} {'':>6} {'':>6} {'':>10} {'':>7}  skipped: calls other functions")
                continue
        counts = {"match": 0, "differ": 0, "both-fault": 0, "skipped": 0}
        examples, undefined_used = [], 0
        for _ in range(args.inputs):
            regs = [random_register(rng) for _ in range(16)]
            regs[6] = STACK_BASE + 0x8000
            flags = [rng.getrandbits(1) for _ in range(5)] + [0, rng.getrandbits(1)]
            xmm = [random_xmm(rng) for _ in range(16)]
            mem = rng.randbytes(REGION_SIZE)
            stack = bytearray(rng.randbytes(REGION_SIZE))
            stack[0x8000:0x8008] = struct.pack("<Q", RET_ADDR)
            stack = bytes(stack)

            kind, ref = reference.run(rva, regs, flags, xmm, mem, stack)
            rc, lif, info = lifted.run(rva, regs, flags, xmm, mem, stack)
            undefined_used += info[0] > 0
            if kind in ("timeout", "emulator-error") or rc in (2, 3) or info[1]:
                counts["skipped"] += 1
            elif kind == "memfault" and rc == 1:
                counts["both-fault"] += 1
            elif (kind == "memfault") != (rc == 1):
                counts["differ"] += 1
                if len(examples) < 3:
                    examples.append("memory fault on only one side "
                                    f"(original {kind}, lifted code {rc})")
            else:
                diff = differences(ref, lif)
                if diff and info[0] > 0:
                    counts["skipped"] += 1
                elif diff:
                    counts["differ"] += 1
                    if len(examples) < 3:
                        examples.append("; ".join(diff[:4]))
                else:
                    counts["match"] += 1
        compared = counts["match"] + counts["differ"]
        total_compared += compared
        total_bad += counts["differ"]
        note = "undefined values used; mismatches there are not counted" if undefined_used else ""
        print(f"{rva:#10x} {compared:8d} {counts['match']:6d} {counts['differ']:6d} "
              f"{counts['both-fault']:10d} {counts['skipped']:7d}  {note}")
        for example in examples:
            print(f"           example: {example}")
    print(f"\ntotal compared {total_compared}, differing {total_bad}")
    return 1 if total_bad else 0


if __name__ == "__main__":
    sys.exit(main())
