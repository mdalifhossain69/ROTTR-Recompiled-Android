# ROTTR Android: Boot and Play Roadmap

For mdalifhossain69/ROTTR-Recompiled-Android · 28 November 2026

## How to use this

Do the steps in order. A step is done only when its **Pass** condition is true and the earlier tests still pass. This is an engineering plan, not a proven route: nobody has verified that this runtime can run the game, later steps will split into smaller ones, and some may need redesign. There is no time estimate.

Host test command:

```
cmake -S . -B build/host && cmake --build build/host && ctest --test-dir build/host --output-on-failure
```

For each step, add a test file under `tests/`, register it with `add_test` in `CMakeLists.txt`, run it in GitHub Actions, and run it on the phone when the step involves ARM64, memory protection or graphics.

Rules: commit no game files. Keep Steam, EOS and ownership checks intact. If a check cannot work legitimately in your runtime, stop there rather than patching or faking it.

Shorter route to playing: Winlator Ludashi, which you report already runs the game. Steps below are for your own runtime.

## Phase A: Foundation

1. Fix the Vulkan surface size. **Pending verification:** the required Android log showing `first frame presented` was not available in this audit; no device run was possible.
2. Build tests and APK in GitHub Actions. **Pending verification:** the workflow is `workflow_dispatch`-only and `gh run list` returned no runs for this branch, so there is no successful run or APK artifact to cite.
3. Add `mingw-w64` to the workflow for building your own Windows test programs. **Pending verification:** the workflow contains build/upload steps for `tests/windows/rottr-win64-test.exe`, but no successful run or uploaded artifact is currently evidenced.

## Phase B: Guest memory

4. Reserve a guest address range with `mmap` and translate guest to host addresses with bounds checks. **Host verified; Android verification pending:** first/last byte and out-of-range behavior pass `tests/test_guest_mem`; the roadmap requires an Android memory run before completion.
5. Commit and decommit 4 KB guest pages. **Host verified; Android verification pending:** uncommitted access and zero-on-recommit pass `tests/test_guest_mem`.
6. Protect pages: none, read, write, execute. Handle 16 KB host pages by tracking 4 KB permissions in software. **Host verified; incomplete:** permission checks pass `tests/test_guest_mem`; no 16 KB Android host/device run has been recorded.
7. Release and query regions, with 64 KB allocation granularity. **Host verified; Android verification pending:** overlap, double free, unaligned base and integer overflow checks pass `tests/test_guest_mem`.
8. Route `VirtualAlloc`, `VirtualFree`, `VirtualProtect`, `VirtualQuery` to this manager. **Host verified; incomplete:** manager results/error codes pass `tests/test_guest_mem`; Windows-output comparison and Android memory validation are pending.

## Phase C: PE loader

9. ✅ Map headers and sections of a PE32+ file. **Pass:** bytes match the file; uninitialized space is zero; truncated files are rejected (`tests/test_pe_loader`, host).
10. ✅ Apply `DIR64` relocations. **Pass:** loading at two bases gives correct pointers at both (`tests/test_pe_loader`, host).
11. ✅ Apply final section protections. **Pass:** writing to `.text` fails (`tests/test_pe_loader`, host).
12. ✅ Resolve imports by name and ordinal, with a trap for each missing one. **Pass:** calling a missing import logs DLL and function name and stops (`tests/test_pe_loader`, host).
13. ✅ Parse exports, including forwarders. **Pass:** name, ordinal and forwarded lookups resolve (`tests/test_pe_loader`, host).
14. ✅ Load guest DLLs with dependencies and reference counts. **Pass:** `tests/test_pe_modules` loads an EXE importing `DepFunc`, calls it through the DLL's IAT, verifies the nested `BaseFunc` dependency returns 42, checks dependency reference counts, and resolves both exports in a cyclic DLL dependency without missing-import traps (host).
15. ✅ Create PEB, TEB, stack and the GS base. **Pass:** gs:[0x30] returns TEB address, gs:[0x60] returns PEB address (`tests/test_win32_gs`, host). GS segment override (0x65) handled in x64 interpreter with gsBase field on rottr_cpu.
16. Set up static TLS and TLS callbacks (build this test with the MSVC target). **Partial:** TLS directory parsing and a synthetic callback are tested by `tests/test_win32_tls`; static TLS allocation/initialization and an MSVC-built test are not implemented or verified.
17. ✅ Call DLL entry points in dependency order. **Pass:** `PeModuleRegistry::callEntryPoints` calls `DLL_PROCESS_ATTACH` in dependency order; `tests/test_win32_dllmain` verifies the dependent DLL observes its dependency's marker and resolves its import. Comparison against Windows remains pending.
18. ✅ Load your own import-free EXE whose entry returns 42. **Pass:** interpreter ends with RAX equal to 42 (`tests/test_pe_execute`, host).

## Phase D: CPU interpreter

19. Separate instruction decoding from execution (a maintained decoder library such as Zydis is an option). **Partial:** the bounded instruction-byte reader is built as a separate runtime source, and prefix/opcode decoding now produces metadata before execution. `tests/test_x64_interpreter` checks hostile addresses, non-executable memory, the 15-byte limit, truncated prefixes, and deterministic random instruction bytes. Opcode-specific operand decoding still happens inside execution; full decoder/executor separation is not complete.
20. Add differential testing against real x86-64 hardware on the CI runner. **Not done:** current interpreter tests assert selected expected results; no test harness comparing interpreter registers, flags, and memory against native x86-64 execution was found.
21. Handle all prefixes, ModRM forms and high-byte registers. **Not done:** only a subset is implemented. In particular, FS override (`0x64`) currently aliases GS (`0x65`), and the interpreter does not support all ModRM forms.
22. Complete integer, stack, call, multiply, divide and bit instructions. **Partial:** the interpreter implements a useful subset and has host expected-result tests; instruction completeness and differential behavior, including divide faults, have not been established.
23. ✅ Add string instructions with `REP`. **Pass:** `MOVS`, `CMPS`, `SCAS`, `LODS`, `STOS` support byte/word/dword/qword sizes and `REP`/`REPE`/`REPNE`. `tests/test_x64_interpreter` checks all five operations across element sizes, compare-prefix stopping, non-REP RCX preservation, and zero-count REP; this caught and fixed incorrect RCX updates for non-REP and a spurious iteration when RCX was zero.
24. Add atomics and fences with x86 memory ordering. **Host verified; Android validation pending:** `tests/test_x64_atomics` checks XCHG/XADD/CMPXCHG, a 4-thread `LOCK ADD` counter and an MFENCE store-buffer litmus. Unaligned guest accesses use `memcpy`, so the implementation does not guarantee race-free behavior for arbitrary overlapping concurrent accesses; run the test on Android ARM64 before completion.
25. Add SSE through SSE4.2. **Pass:** results match, including NaN and rounding cases.
26. Add x87 and MXCSR behavior. **Pass:** precision and rounding tests match.
27. ✅ Add `CPUID`, `RDTSC`, `XGETBV`. **Pass:** only implemented features are advertised. `tests/test_x64_cpuid` verifies leaf 0 (vendor/max leaf), leaf 1 advertises zero ECX/EDX feature bits because no SSE/x87/AVX exists yet, unknown and extended leaves, monotonic RDTSC, and XGETBV returning XCR0=0. Phone run pending.
28. Inventory instructions the game reaches, adding AVX or others if needed. **Not done:** no game-instruction trace/inventory logger was found; unsupported execution returns a stop result but does not log opcode bytes for systematic implementation.

## Phase E: Windows calls and runtime

29. ✅ Build the guest-to-host call bridge using the Windows x64 convention. **Pass:** bridge, `HostProc` calling convention and guest-to-host address mapping are implemented in `runtime/src/x64_bridge.{h,cpp}` and `runtime/src/win32_host.cpp`; `tests/test_x64_bridge` verifies register arguments and a stack argument.
30. Support host-to-guest callbacks. **Partial:** `tests/test_x64_bridge` verifies a basic callback returns 15; nested callbacks and nonvolatile-register preservation are not tested.
31. ✅ Implement process exit, last-error, module and command-line APIs. **Pass:** your EXE calls `ExitProcess(42)` and the app reports 42 (`tests/test_win32_exit`, host); `GetLastError`/`SetLastError` stubs implemented in `runtime/src/win32_host.cpp`.
32. ✅ Implement handles. **Pass:** invalid and closed handles return correct errors (`tests/test_win32_handles`, host).
33. ✅ Implement heaps. **Pass:** a million random allocate and free operations show no corruption (`tests/test_win32_heap`, host).
34. Implement files and paths, mapping the game folder through Android storage. **Host path operations verified; Android integration incomplete:** `tests/test_win32_files` covers case-insensitive open, a seek past 4 GB and directory listing under `/tmp`; it does not demonstrate access through Android user-selected storage.
35. ✅ Implement memory-mapped files. **Pass:** multiple views show correct data. `CreateFileMappingA`, `MapViewOfFile` and `UnmapViewOfFile` are implemented over `mmap`; `tests/test_win32_mmap` verifies two distinct views over one mapping show the file, writes through one view are visible through the other, and a page-offset view shows the right data. Phone run pending.
36. Implement threads, thread IDs and per-thread TLS. **Partial:** `tests/test_win32_threads` exercises 32 host threads, IDs, wait/join and close; it does not test Windows TLS, and no `TlsAlloc`/`TlsGetValue`/`TlsSetValue`/`TlsFree` APIs are implemented.
37. Implement events, mutexes, semaphores, critical sections, SRW locks, condition variables and waits. **Partial:** host tests now verify event reset/signaling and finite/infinite waits, mutex initial ownership/recursion/owner-only release, semaphore waits/count limits, and critical-section stress. SRW locks and condition variables remain unimplemented; Android run pending.
38. ✅ Implement timers, clocks and sleep. **Pass:** time never goes backward. `GetTickCount`, `GetTickCount64`, `QueryPerformanceCounter`, `QueryPerformanceFrequency` and `Sleep` use a monotonic steady clock; `tests/test_win32_time` verifies QPC and tick counts never regress, advance across `Sleep`, and that `Sleep` blocks. Phone run pending.
39. Implement table-based exceptions and stack unwinding using `.pdata`. **Partial:** `runtime/src/se_unwind.{h,cpp}` parses `RUNTIME_FUNCTION`, decodes supported `UNWIND_INFO`, reads `__C_specific_handler` scopes and performs virtual unwinds. `tests/test_se_unwind` validates synthetic layouts and malformed input; actual exception dispatch, nested `__try`/`__finally`, C++ throws and comparison with Windows are not demonstrated.
40. Provide the C and C++ runtime DLLs, using legitimately obtained or open implementations. **Pass:** a C++ program using strings, streams and exceptions runs.
41. ✅ Implement registry and user-folder APIs. **Pass:** values persist after restart. `runtime/src/win32_registry.cpp` implements `RegOpenKeyExA`, `RegCreateKeyExA`, `RegSetValueExA`, `RegQueryValueExA`, `RegEnumKeyExA`, `RegEnumValueA`, `RegDeleteValueA`, `RegDeleteKeyA`, `RegFlushKey`, `GetEnvironmentVariableA` and `SHGetFolderPathA` over an atomically saved persistent store. `tests/test_win32_registry` writes in the parent, then execs two fresh child processes: the first reads the values back and deletes one, the second verifies the deletion persisted; it also checks documented error codes (`ERROR_FILE_NOT_FOUND`, `ERROR_MORE_DATA`, `ERROR_NO_MORE_ITEMS`) and folder resolution.
42. ✅ Implement the window and message loop. **Pass:** create, resize, focus, input and quit messages arrive in order. `runtime/src/win32_window.cpp` implements `RegisterClassA/ExA`, `CreateWindowExA`, `DestroyWindow`, `ShowWindow`, `MoveWindow`, `SetWindowPos`, `SetFocus`, `GetFocus`, `SetWindowTextA`, `PostMessageA`, `PostQuitMessage`, `GetMessageA`, `PeekMessageA`, `TranslateMessage`, `DispatchMessageA` and `DefWindowProcA` over a FIFO queue; `DispatchMessageA` calls host window procedures through the bridge. `tests/test_win32_window` verifies the exact order `WM_CREATE`, `WM_SHOWWINDOW`, `WM_WINDOWPOSCHANGED`, `WM_SIZE`, `WM_MOVE`, `WM_SETFOCUS`, second `WM_CREATE`, `WM_KILLFOCUS`, `WM_SETFOCUS`, `WM_KEYDOWN`, `WM_CHAR`, `WM_MOUSEMOVE`, then `WM_QUIT` with its exit code, plus `WM_DESTROY`/`WM_NCDESTROY` ordering and invalid-handle rejection.
43. ✅ Add COM basics. **Pass:** reference counting and interface queries work. `runtime/src/win32_com.cpp` implements `CoInitializeEx`/`CoUninitialize`, `CoCreateInstance` (registered CLSID, `REGDB_E_CLASSNOTREG` and `E_NOINTERFACE` paths) and `CoTaskMemAlloc`/`CoTaskMemFree`; created objects expose a real `QueryInterface`/`AddRef`/`Release` vtable. `tests/test_win32_com` calls through the vtable and verifies `QueryInterface` success and failure, exact `AddRef`/`Release` counts to zero, null-out on `E_NOINTERFACE`, and task-memory allocation.
44. ✅ Add the network and system APIs the game calls. **Pass:** offline calls return documented errors without hanging. `runtime/src/win32_net.cpp` implements `WSAStartup`/`WSACleanup`, `socket`, `closesocket`, `connect` (bounded 300 ms non-blocking wait), `send`, `recv`, `shutdown`, `getaddrinfo`/`freeaddrinfo` (Windows `ADDRINFOA` layout, numeric hosts never touch DNS and the reserved `.invalid` TLD fails immediately), byte-order helpers and `inet_addr`/`inet_ntoa`. `runtime/src/win32_system.cpp` implements `GetSystemInfo`, `GetVersionExA`, `GetComputerNameA`, `GetUserNameA`, `GetSystemTime`/`GetLocalTime`, `GetSystemTimeAsFileTime`, `GetTimeZoneInformation`, disk/directory queries, `GlobalMemoryStatusEx`, processor count and power status. `tests/test_win32_net` verifies `WSAENOTCONN` on an unconnected socket, a refused/timed-out loopback connect in under two seconds, numeric `getaddrinfo`, an immediate `WSAHOST_NOT_FOUND` for `.invalid`, and real system values.
45. Generate a report of all 912 imports as implemented, tested or missing. **Locally verified; CI verification pending:** `runtime/src/import_report.cpp` parses imports and classifies them against registered host modules and the test-coverage manifest; `tools/import_report.cpp` is the CLI. Running it on the local, ignored `bin_input/ROTTR.exe` reports 912 imports (62 tested, 5 implemented, 845 missing). Unknown exports resolve to no address and the PE loader installs UD2 traps. `tests/test_import_report` passes; the workflow has a report step, but no successful workflow run is currently evidenced.

## Phase F: ARM64 translator

Start after step 28. It can run alongside later work.
The current host is x86-64. In this audit CTest skips all six tests that execute generated AArch64 code; the workflow runs on `ubuntu-24.04` and does not provide an AArch64 device. Keep these steps incomplete until their native execution criteria are verified on Android ARM64.

46. ✅ Write an ARM64 instruction emitter. **Pass:** output matches the LLVM assembler. `runtime/src/arm64_emitter.cpp` emits AArch64 words for moves/immediates, arithmetic, logical, shifts, multiply, loads/stores, branches (including condition/CBZ/TBZ), ADR/ADRP, RET/BR/BLR, SVC and BRK. `tests/test_arm64_emitter` assembles every covered instruction with `llvm-mc -triple=aarch64 -show-encoding` and compares the exact bytes; the workflow installs LLVM so the check runs in CI. A missing `llvm-mc` skips the test rather than passing it.
47. Manage executable memory and instruction-cache flushing. **Code/test present; native execution unverified:** `tests/test_arm64_code_buffer` skips generated-code execution on this host; Android ARM64 run required.
48. Translate basic blocks with interpreter fallback. **Code/test present; native execution unverified:** `tests/test_arm64_block_translator` skips generated-code execution on this host; Android ARM64 run required.
49. Cover flags, branches, SIMD, atomics and memory ordering. **Partial/unverified:** current translation tests skip on x86-64. The translated test environment variable is configured, but `hasArm64TranslationBackend()` is false here, so `runX64ForTest` falls back to the interpreter; this does not verify translated execution.
50. Add block cache, linking and invalidation. **Code/test present; native execution unverified:** `tests/test_arm64_block_cache` skips translated-code execution on this host; Android ARM64 run required.
51. Handle faults and exceptions inside translated code. **Code/test present; native execution unverified:** `tests/test_arm64_faults` skips translated-code execution on this host; Android ARM64 run required.
52. Profile and optimize. **Benchmark present; measurements unverified:** the `arm64_bench` CTest/`tools/arm64_bench` skip on this host; collect cached/uncached correctness and performance measurements on Android ARM64 before marking complete.

## Phase G: Graphics

53. Compile the DXBC shader library for Android ARM64 (vkd3d-shader). **Pass:** your test shaders produce SPIR-V accepted by `spirv-val`.
54. Expose DXGI as guest COM interfaces. **Pass:** your program lists the adapter and creates a swapchain.
55. Expose D3D11 device and context interfaces (evaluate reusing DXVK Native before writing your own). **Pass:** your program clears the screen blue.
56. Draw a triangle. **Pass:** screenshot matches the reference image.
57. Add buffers, textures, views, samplers and constant buffers. **Pass:** textured spinning cube matches the reference.
58. Add depth, blend, rasterizer state and multiple render targets. **Pass:** state tests match.
59. Add compute, geometry and tessellation shaders as supported by the GPU. **Pass:** each sample matches.
60. Add synchronization, queries, deferred contexts and compressed formats. **Pass:** Vulkan validation reports no errors.
61. Add pipeline caching and surface recreation. **Pass:** 20 suspend and resume cycles survive.

## Phase H: Boot the game

62. Load `ROTTR.exe` and all its DLLs without running it. **Pass:** every import resolves to real code.
63. Make Steam and EOS work legitimately with real components. **Pass:** ownership is verified normally. If impossible, stop here.
64. Run to the entry point and C runtime start. **Pass:** execution reaches the game's main function.
65. Repeat: run, take the first failure, write a small test that reproduces it, fix it. **Pass:** the game creates its window.
66. Create the D3D11 device. **Pass:** game's first present reaches the screen.
67. Play intro video through Bink using the user's own DLL. **Pass:** video displays correctly.
68. **Reach the main menu.** **Pass:** three clean launches in a row.

## Phase I: Make it playable

69. Add game audio. **Pass:** music and speech play in sync.
70. Add controller input and touch controls. **Pass:** every game action works.
71. Verify saves. **Pass:** save, close, reopen and load gives the same progress.
72. Start a new game. **Pass:** Lara is controllable and the first level renders.
73. Test long play. **Pass:** two hours without a crash or growing memory.
74. Optimize CPU, GPU, shader stutter and heat. **Pass:** a stable 30 FPS in a fixed test scene after 30 minutes; raise the target afterward.
75. Finish the game once. **Pass:** campaign completes with no blocker.
76. Release the APK with source, licenses and no game files. **Pass:** a clean phone install works using only the user's own game copy.
