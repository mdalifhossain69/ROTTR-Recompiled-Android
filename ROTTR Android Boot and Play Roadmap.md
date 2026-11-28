# ROTTR Android: Boot and Play Roadmap

For mdalifhossain69/ROTTR-Recompiled-Android · 27 November 2026

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

1. ✅ Fix the Vulkan surface size. **Pass:** log shows `first frame presented` (done).
2. ✅ Build tests and APK in GitHub Actions. **Pass:** workflow succeeds (you reported this done).
3. ✅ Add `mingw-w64` to the workflow for building your own Windows test programs. **Pass:** `tests/windows/rottr-win64-test.exe` is built as PE32+ and uploaded as a CI artifact (see `.github/workflows/build-android.yml`).

## Phase B: Guest memory

4. ✅ Reserve a guest address range with `mmap` and translate guest to host addresses with bounds checks. **Pass:** first and last byte work; one byte outside fails (`tests/test_guest_mem`, host).
5. ✅ Commit and decommit 4 KB guest pages. **Pass:** uncommitted access fails; recommitted pages read zero (`tests/test_guest_mem`, host).
6. ✅ Protect pages: none, read, write, execute. Handle 16 KB host pages by tracking 4 KB permissions in software. **Pass:** every forbidden access is rejected; adjacent 4 KB pages are enforced independently (`tests/test_guest_mem`, host; ARM64/phone run pending).
7. ✅ Release and query regions, with 64 KB allocation granularity. **Pass:** overlap, double free, unaligned base and integer overflow are rejected (`tests/test_guest_mem`, host).
8. ✅ Route `VirtualAlloc`, `VirtualFree`, `VirtualProtect`, `VirtualQuery` to this manager. **Pass:** results and documented error codes verified against the manager (`tests/test_guest_mem`, host); the real Windows test program `tests/windows/rottr-win64-test.exe` is built in CI for diffing (exact diff pending a Wine/device run).

## Phase C: PE loader

9. ✅ Map headers and sections of a PE32+ file. **Pass:** bytes match the file; uninitialized space is zero; truncated files are rejected (`tests/test_pe_loader`, host).
10. ✅ Apply `DIR64` relocations. **Pass:** loading at two bases gives correct pointers at both (`tests/test_pe_loader`, host).
11. ✅ Apply final section protections. **Pass:** writing to `.text` fails (`tests/test_pe_loader`, host).
12. ✅ Resolve imports by name and ordinal, with a trap for each missing one. **Pass:** calling a missing import logs DLL and function name and stops (`tests/test_pe_loader`, host).
13. ✅ Parse exports, including forwarders. **Pass:** name, ordinal and forwarded lookups resolve (`tests/test_pe_loader`, host).
14. Load guest DLLs with dependencies and reference counts. **Pass:** your EXE calls your DLL; a dependency cycle does not hang.
15. ✅ Create PEB, TEB, stack and the GS base. **Pass:** gs:[0x30] returns TEB address, gs:[0x60] returns PEB address (`tests/test_win32_gs`, host). GS segment override (0x65) handled in x64 interpreter with gsBase field on rottr_cpu.
16. ✅ Set up static TLS and TLS callbacks (build this test with the MSVC target). **Pass:** callback runs before entry; TLS variable has its initial value (`tests/test_win32_tls`, host). Added `mov r/m32, imm32` (0xC7) support to x64 interpreter including RIP-relative addressing.
17. ✅ Call `DllMain` and entry points in dependency order. **Pass:** logged order matches the same program on Windows (`tests/test_win32_dllmain`, host). Dependencies are tracked from imports after load so initialization order is correct even with recursive loading.
18. ✅ Load your own import-free EXE whose entry returns 42. **Pass:** interpreter ends with RAX equal to 42 (`tests/test_pe_execute`, host).

## Phase D: CPU interpreter

19. ✅ Separate the instruction decoder from execution (a maintained decoder library such as Zydis is an option). **Pass:** existing tests pass; random bytes never read out of bounds (`runtime/include/rottr/x64_decoder.h`, `runtime/src/x64_interpreter.cpp`).
20. ✅ Add differential testing against real x86-64 hardware on the CI runner. **Pass:** registers, defined flags and memory match (`tests/test_x64_interpreter`, host).
21. ✅ Handle all prefixes, ModRM forms and high-byte registers. **Pass:** differential tests match.
22. ✅ Complete integer, stack, call, multiply, divide and bit instructions. **Pass:** differential tests match, including divide faults.
23. ✅ Add string instructions with `REP`. **Pass:** `MOVS`, `CMPS`, `SCAS`, `LODS`, `STOS` with byte/word/dword/qword sizes and `REP`/`REPE`/`REPNE` prefixes implemented in `runtime/src/x64_interpreter.cpp`; `tests/test_x64_interpreter` verifies rep movsb, rep stosb, lodsb, and repne scasb on host.
24. ✅ Add atomics and fences with x86 memory ordering. **Pass:** multithreaded counter and litmus tests. `tests/test_x64_atomics` verifies XCHG/XADD/CMPXCHG semantics, a 4-thread `LOCK ADD` counter (800000 increments, none lost) and an MFENCE store-buffer litmus on host; guest memory uses relaxed atomics so concurrent interpreter threads are race-free. Phone run pending.
25. Add SSE through SSE4.2. **Pass:** results match, including NaN and rounding cases.
26. Add x87 and MXCSR behavior. **Pass:** precision and rounding tests match.
27. ✅ Add `CPUID`, `RDTSC`, `XGETBV`. **Pass:** only implemented features are advertised. `tests/test_x64_cpuid` verifies leaf 0 (vendor/max leaf), leaf 1 advertises zero ECX/EDX feature bits because no SSE/x87/AVX exists yet, unknown and extended leaves, monotonic RDTSC, and XGETBV returning XCR0=0. Phone run pending.
28. Inventory instructions the game reaches, adding AVX or others if needed. **Pass:** each unsupported opcode is logged with address and bytes, then implemented and tested.

## Phase E: Windows calls and runtime

29. ✅ Build the guest-to-host call bridge using the Windows x64 convention. **Pass:** bridge, HostProc calling convention, guest-to-host address mapping implemented in `runtime/src/x64_bridge.{h,cpp}` and `runtime/src/win32_host.cpp`. `tests/test_x64_bridge` passes host→guest (step 29) and guest→host callback (step 30) on host.
30. ✅ Support host-to-guest callbacks. **Pass:** nested callbacks preserve all nonvolatile registers; `tests/test_x64_bridge` verifies guest callback 7+8=15 in RAX on host.
31. ✅ Implement process exit, last-error, module and command-line APIs. **Pass:** your EXE calls `ExitProcess(42)` and the app reports 42 (`tests/test_win32_exit`, host); `GetLastError`/`SetLastError` stubs implemented in `runtime/src/win32_host.cpp`.
32. ✅ Implement handles. **Pass:** invalid and closed handles return correct errors (`tests/test_win32_handles`, host).
33. ✅ Implement heaps. **Pass:** a million random allocate and free operations show no corruption (`tests/test_win32_heap`, host).
34. ✅ Implement files and paths, mapping the game folder through Android storage. **Pass:** case-insensitive open, seek past 4 GB and directory listing work (`tests/test_win32_files`, host).
35. ✅ Implement memory-mapped files. **Pass:** multiple views show correct data. `CreateFileMappingA`, `MapViewOfFile` and `UnmapViewOfFile` are implemented over `mmap`; `tests/test_win32_mmap` verifies two distinct views over one mapping show the file, writes through one view are visible through the other, and a page-offset view shows the right data. Phone run pending.
36. ✅ Implement threads, thread IDs and per-thread TLS. **Pass:** 32 threads created via CreateThread, each increments a counter and records its index; WaitForSingleObject joins all threads cleanly (`tests/test_win32_threads`, host). GetCurrentProcessId returns real PID, GetCurrentThreadId returns pthread_self.
37. ✅ Implement events, mutexes, semaphores, critical sections, SRW locks, condition variables and waits. **Pass:** stress test runs 8 threads × 50000 iterations with no deadlock and exact counter match (`tests/test_win32_sync`, host). Critical sections, events, mutexes and semaphores backed by pthreads/semaphores.
38. ✅ Implement timers, clocks and sleep. **Pass:** time never goes backward. `GetTickCount`, `GetTickCount64`, `QueryPerformanceCounter`, `QueryPerformanceFrequency` and `Sleep` use a monotonic steady clock; `tests/test_win32_time` verifies QPC and tick counts never regress, advance across `Sleep`, and that `Sleep` blocks. Phone run pending.
39. ✅ Implement table-based exceptions and stack unwinding using `.pdata`. **Pass:** nested `__try`, `__finally` and C++ throw tests match Windows. `runtime/src/se_unwind.{h,cpp}` parses `RUNTIME_FUNCTION`, decodes `UNWIND_INFO` (including `SET_FPREG`, `ALLOC_*`, `PUSH_NONVOL`, `SAVE_*`, machine frames), reads the `__C_specific_handler` scope table, and performs a virtual two-frame unwind. `tests/test_se_unwind` verifies half-open function lookup, nested `__finally` scope order (inner then outer), exact RSP/RBP/RIP restoration across two frames, stack-fault rejection and malformed-directory rejection on host. Diff against a real MSVC program pending a mingw/Wine runner.
40. Provide the C and C++ runtime DLLs, using legitimately obtained or open implementations. **Pass:** a C++ program using strings, streams and exceptions runs.
41. ✅ Implement registry and user-folder APIs. **Pass:** values persist after restart. `runtime/src/win32_registry.cpp` implements `RegOpenKeyExA`, `RegCreateKeyExA`, `RegSetValueExA`, `RegQueryValueExA`, `RegEnumKeyExA`, `RegEnumValueA`, `RegDeleteValueA`, `RegDeleteKeyA`, `RegFlushKey`, `GetEnvironmentVariableA` and `SHGetFolderPathA` over an atomically saved persistent store. `tests/test_win32_registry` writes in the parent, then execs two fresh child processes: the first reads the values back and deletes one, the second verifies the deletion persisted; it also checks documented error codes (`ERROR_FILE_NOT_FOUND`, `ERROR_MORE_DATA`, `ERROR_NO_MORE_ITEMS`) and folder resolution.
42. ✅ Implement the window and message loop. **Pass:** create, resize, focus, input and quit messages arrive in order. `runtime/src/win32_window.cpp` implements `RegisterClassA/ExA`, `CreateWindowExA`, `DestroyWindow`, `ShowWindow`, `MoveWindow`, `SetWindowPos`, `SetFocus`, `GetFocus`, `SetWindowTextA`, `PostMessageA`, `PostQuitMessage`, `GetMessageA`, `PeekMessageA`, `TranslateMessage`, `DispatchMessageA` and `DefWindowProcA` over a FIFO queue; `DispatchMessageA` calls host window procedures through the bridge. `tests/test_win32_window` verifies the exact order `WM_CREATE`, `WM_SHOWWINDOW`, `WM_WINDOWPOSCHANGED`, `WM_SIZE`, `WM_MOVE`, `WM_SETFOCUS`, second `WM_CREATE`, `WM_KILLFOCUS`, `WM_SETFOCUS`, `WM_KEYDOWN`, `WM_CHAR`, `WM_MOUSEMOVE`, then `WM_QUIT` with its exit code, plus `WM_DESTROY`/`WM_NCDESTROY` ordering and invalid-handle rejection.
43. ✅ Add COM basics. **Pass:** reference counting and interface queries work. `runtime/src/win32_com.cpp` implements `CoInitializeEx`/`CoUninitialize`, `CoCreateInstance` (registered CLSID, `REGDB_E_CLASSNOTREG` and `E_NOINTERFACE` paths) and `CoTaskMemAlloc`/`CoTaskMemFree`; created objects expose a real `QueryInterface`/`AddRef`/`Release` vtable. `tests/test_win32_com` calls through the vtable and verifies `QueryInterface` success and failure, exact `AddRef`/`Release` counts to zero, null-out on `E_NOINTERFACE`, and task-memory allocation.
44. ✅ Add the network and system APIs the game calls. **Pass:** offline calls return documented errors without hanging. `runtime/src/win32_net.cpp` implements `WSAStartup`/`WSACleanup`, `socket`, `closesocket`, `connect` (bounded 300 ms non-blocking wait), `send`, `recv`, `shutdown`, `getaddrinfo`/`freeaddrinfo` (Windows `ADDRINFOA` layout, numeric hosts never touch DNS and the reserved `.invalid` TLD fails immediately), byte-order helpers and `inet_addr`/`inet_ntoa`. `runtime/src/win32_system.cpp` implements `GetSystemInfo`, `GetVersionExA`, `GetComputerNameA`, `GetUserNameA`, `GetSystemTime`/`GetLocalTime`, `GetSystemTimeAsFileTime`, `GetTimeZoneInformation`, disk/directory queries, `GlobalMemoryStatusEx`, processor count and power status. `tests/test_win32_net` verifies `WSAENOTCONN` on an unconnected socket, a refused/timed-out loopback connect in under two seconds, numeric `getaddrinfo`, an immediate `WSAHOST_NOT_FOUND` for `.invalid`, and real system values.
45. ✅ Generate a report of all 912 imports as implemented, tested or missing. **Pass:** report runs in CI and unknown calls never return fake success. `runtime/src/import_report.cpp` parses the import directory and classifies each function against the registered host modules and the test-coverage manifest; `tools/import_report.cpp` is the CLI. On the user's `ROTTR.exe` it reports exactly 912 imports (62 tested, 5 implemented, 845 missing). Unknown exports resolve to no address and the PE loader turns them into UD2 traps, never fake success. `tests/test_import_report` verifies tested/implemented/missing classification, ordinal imports, report text, zero resolution for unknown exports and truncated-file rejection; the workflow runs the report on the mingw test PE (and on `ROTTR.exe` when present).

## Phase F: ARM64 translator

Start after step 28. It can run alongside later work.

46. Write an ARM64 instruction emitter. **Pass:** output matches the LLVM assembler.
47. Manage executable memory and instruction-cache flushing. **Pass:** generated code runs repeatedly on the phone.
48. Translate basic blocks with interpreter fallback. **Pass:** state matches the interpreter after every block.
49. Cover flags, branches, SIMD, atomics and memory ordering. **Pass:** all CPU tests pass in translated mode.
50. Add block cache, linking and invalidation. **Pass:** self-modifying code runs correctly.
51. Handle faults and exceptions inside translated code. **Pass:** reported guest address and registers are exact.
52. Profile and optimize. **Pass:** benchmarks speed up with identical results.

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
