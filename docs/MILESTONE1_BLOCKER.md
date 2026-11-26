# Milestone 1 blocker — lift-and-compare cannot run end-to-end yet

## Status (as of commit b71c747 / 972c621)

What **works** (verified on the Ubuntu proot-distro container, aarch64 host):

- `python3 scripts/doctor.py` — cmake, ninja, clang, python3 all present
- Remill built from source (`scripts/setup-remill.sh`, LLVM 17):
  - `tools/remill-src/build/bin/lift/remill-lift-17`  ✅
  - headers at `tools/remill-src/include/remill/...`  ✅
- `python3 scripts/recon.py bin_input/ROTTR.exe` — exit 0, no protection indicators
- `python3 scripts/functions.py bin_input/ROTTR.exe --out generated/functions.csv` — 72,050 table entries
- `python3 scripts/lift_functions.py bin_input/ROTTR.exe --count 10` — 10 `.ll` IR files in `generated/lift/`
- `python3 scripts/build_harness.py ...` — `generated/harness/libharness.so` builds (10 lifted functions, 17 stubbed callees)
- `ctest` — 4/4 pass (guest, recon, functions, harness—harness skips without the real Remill runtime linked)
- `python3` `unicorn` 2.1.4 works in Ubuntu (unlike Termux, where it fails to load)

What **does not work** yet — `python3 scripts/compare_lifted.py bin_input/ROTTR.exe`:

```
OSError: .../libharness.so: undefined symbol: __remill_write_memory_32
```

And even after linking, the shim's calling convention and state layout do not match real Remill-lifted code.

## Root cause (three interlocking issues)

### 1. Remill-lifted functions use a 3-argument signature
Real Remill output (see `generated/lift/*.ll`):

```llvm
define ptr @sub_1000(ptr noalias %state, i64 %program_counter, ptr noalias %memory)
```

The shim (`harness/shim.cpp`) calls `fn(&state)` with **one** argument. It must call
`fn(state, pc, memory)` with three. The shim's `RemillState`/`Snapshot` mapping and the
`harness_run` signature in `compare_lifted.py` both need to change to match.

### 2. State layout mismatch
Lifted IR does `getelementptr %struct.X86State` at fixed offsets (e.g. rax index 1,
rcx index 2, ..., rsp index 13, rip index 33). The shim's homemade `RemillState`
(`gpr[16]`, `rip`, `rflags`, `xmm[16][2]`, `memory_state`) does **not** match the real
`remill::X86State` layout, so even if it linked, every register read/write would hit the
wrong field and every comparison would falsely "differ".

Fix: the shim must use the real `remill::State` / `X86State` (include
`remill/Arch/X86/Runtime/State.h`) so the `getelementptr` offsets line up.

### 3. Memory intrinsics only exist as x86_64 bitcode
The lifted `.o` files reference `__remill_read_memory_64`, `__remill_write_memory_64`,
`__remill_function_call`, `__remill_function_return`, `__remill_undefined_8`, and the
`__remill_flag_computation_*` symbols. None of these are present in any of Remill's
built static archives (`lib/Arch/*.a`, `lib/BC/*.a`, `lib/OS/*.a`, `lib/Version/*.a`).

They exist only as LLVM bitcode under
`tools/remill-src/build/lib/Arch/X86/Runtime/` (`x86_Intrinsics.cpp.bc`,
`x86_HyperCall.cpp.bc`, …), compiled for the **x86_64 guest** target. The host is
**aarch64**, so that bitcode cannot be compiled to native aarch64 objects and linked
with the lifted `.o` files.

## What is needed to finish Milestone 1

1. **Rewrite `harness/shim.cpp`** to:
   - call the lifted function as `fn(state, pc, memory)` (3 args),
   - use the real `remill::X86State` layout so GEP offsets match,
   - pass a guest-memory buffer as the `%memory` argument.

2. **Provide native aarch64 implementations of the Remill runtime intrinsics**
   (`__remill_read_memory_64`, `__remill_write_memory_64`,
   `__remill_function_call`, `__remill_function_return`, `__remill_undefined_8`,
   `__remill_flag_computation_*`). Since Remill only ships them as x86_64 bitcode,
   they must either be re-implemented natively against `memory_state`/the guest-memory
   buffer, or the Remill runtime source (`lib/Arch/Runtime/Intrinsics.cpp`, etc.) must
   be compiled for the host arch.

3. **Update `compare_lifted.py`** `harness_run` call to pass `pc` and `memory` and to
   read back the full state per the real layout.

## Exit criteria (from docs/milestone1-lift.md)

All 10 lifted functions produce identical results on x86-64 (Unicorn) and natively,
each step recorded with commit hash, device, and NDK version.

## Files touched in this session

- `harness/shim.cpp` — fixed `static_cast`→`reinterpret_cast` and the
  `goto`-over-initialization compile errors (committed: ubuntu b71c747, termux 972c621)
- `generated/functions.csv`, `generated/lift/*.ll`, `generated/harness/` — derived from
  the user's `ROTTR.exe`; gitignored, never committed
- `docs/NEXT_STEPS.md`, `docs/MILESTONE1_BLOCKER.md` — runbook + this blocker note

## Repo layout (two checkouts of the same work)

- Termux: `/data/data/com.termux/files/home/ROTTR-Recomp` (branch `main`, commits
  d18772b merge + 60213ec runbook + 972c621 shim fix)
- Ubuntu proot-distro: `/root/ROTTR-Recomp` (branch `static-recomp-scaffold`, commits
  c219e85 mdali scaffold + b71c747 shim fix)

The user's `ROTTR.exe` (SHA-256
`4daad0e31a0b64bf5cc079881ba254c5f5392ce506444a7f8b38c28076437c7f`) lives at
`~/storage/downloads/ROTTR.exe` (Termux) and at
`/root/ROTTR-Recomp/bin_input/ROTTR.exe` (Ubuntu). It is **not** committed to git.
