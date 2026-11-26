// runtime/src/win32_stubs.cpp
// Basic Windows API stubs for static recompilation.
// These provide minimal implementations needed for the game to link and run.

#include <cstdint>
#include <cstring>
#include <cstdlib>

// --- Basic types ---
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef uint64_t QWORD;
typedef uint32_t DWORD;
typedef uint16_t WORD;
typedef uint8_t BYTE;
typedef int32_t BOOL;
typedef int64_t LONGLONG;
typedef uint64_t ULONGLONG;
typedef size_t SIZE_T;

// Windows calling convention (no-op on Linux)
#ifndef WINAPI
#define WINAPI
#endif

// Pointer types
typedef void* LPVOID;
typedef const void* LPCVOID;
typedef void* PVOID;
typedef const void* PCVOID;
typedef char* LPSTR;
typedef const char* LPCSTR;
typedef wchar_t* LPWSTR;
typedef const wchar_t* LPCWSTR;

// Handle types
typedef void* HANDLE;
typedef HANDLE HMODULE;
typedef HANDLE HINSTANCE;
typedef HANDLE HWND;
typedef HANDLE HDC;
typedef HANDLE HGLRC;

// Exception handling
typedef struct _EXCEPTION_POINTERS {
    void* ExceptionRecord;
    void* ContextRecord;
} EXCEPTION_POINTERS, *PEXCEPTION_POINTERS;

// Thread function
typedef DWORD (WINAPI *LPTHREAD_START_ROUTINE)(LPVOID);
typedef void (WINAPI *PTOP_LEVEL_EXCEPTION_FILTER)(struct _EXCEPTION_POINTERS*);

// --- Constants ---
#define FALSE 0
#define TRUE 1
#ifndef NULL
#define NULL 0
#endif
#define INFINITE 0xFFFFFFFF
#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define PAGE_READWRITE 0x04

// --- Critical game APIs stubs ---

// VirtualAlloc - memory allocation
extern "C" void* WINAPI VirtualAlloc(void* lpAddress, DWORD dwSize, DWORD flAllocationType, DWORD flProtect) {
    (void)flAllocationType; (void)flProtect;
    // Simple: just return address if within our guest memory range, else null
    static uint8_t guest_memory[0x100000];
    if ((uintptr_t)lpAddress + dwSize <= sizeof(guest_memory)) {
        return lpAddress;
    }
    return nullptr;
}

// VirtualFree - memory deallocation
extern "C" BOOL WINAPI VirtualFree(void* lpAddress, DWORD dwSize, DWORD dwFreeType) {
    (void)lpAddress; (void)dwSize; (void)dwFreeType;
    return TRUE;
}

// Sleep - minimal delay
extern "C" void WINAPI Sleep(DWORD dwMilliseconds) {
    // No-op stub for game timing
    (void)dwMilliseconds;
}

// GetTickCount - return simulated time
extern "C" DWORD WINAPI GetTickCount(void) {
    return 0;  // Simplified
}

// GetTickCount64 - 64-bit version
extern "C" uint64_t WINAPI GetTickCount64(void) {
    return 0;
}

// --- String/Buffer operations ---

// lstrlenW - string length
extern "C" int WINAPI lstrlenW(const wchar_t* lpString) {
    if (!lpString) return 0;
    int len = 0;
    while (lpString[len]) len++;
    return len;
}

// lstrlenA - ANSI string length
extern "C" int WINAPI lstrlenA(const char* lpString) {
    if (!lpString) return 0;
    int len = 0;
    while (lpString[len]) len++;
    return len;
}

// lstrcpyW - string copy
extern "C" wchar_t* WINAPI lstrcpyW(wchar_t* lpString1, const wchar_t* lpString2) {
    if (!lpString1 || !lpString2) return lpString1;
    int i = 0;
    while (lpString2[i]) {
        lpString1[i] = lpString2[i];
        i++;
    }
    lpString1[i] = L'\0';
    return lpString1;
}

// lstrcpyA - ANSI string copy
extern "C" char* WINAPI lstrcpyA(char* lpString1, const char* lpString2) {
    if (!lpString1 || !lpString2) return lpString1;
    int i = 0;
    while (lpString2[i]) {
        lpString1[i] = lpString2[i];
        i++;
    }
    lpString1[i] = '\0';
    return lpString1;
}

// lstrcatW - string concatenation
extern "C" wchar_t* WINAPI lstrcatW(wchar_t* lpString1, const wchar_t* lpString2) {
    if (!lpString1 || !lpString2) return lpString1;
    int i = 0;
    while (lpString1[i]) i++;
    int j = 0;
    while (lpString2[j]) {
        lpString1[i++] = lpString2[j++];
    }
    lpString1[i] = L'\0';
    return lpString1;
}

// wsprintfW - formatted string
extern "C" int WINAPI wsprintfW(wchar_t* buffer, const wchar_t* format, ...) {
    (void)buffer; (void)format;
    return 0;
}

// --- Module operations ---

// GetModuleHandleW - module handle
extern "C" HMODULE WINAPI GetModuleHandleW(const wchar_t* lpModuleName) {
    (void)lpModuleName;
    // Simplified: return null for now, game may handle this
    return nullptr;
}

// GetModuleHandleA - ANSI version
extern "C" HMODULE WINAPI GetModuleHandleA(const char* lpModuleName) {
    (void)lpModuleName;
    return nullptr;
}

// LoadLibraryW - load library
extern "C" HMODULE WINAPI LoadLibraryW(const wchar_t* lpLibFileName) {
    (void)lpLibFileName;
    return nullptr;
}

// LoadLibraryA - ANSI version
extern "C" HMODULE WINAPI LoadLibraryA(const char* lpLibFileName) {
    (void)lpLibFileName;
    return nullptr;
}

// FreeLibrary - free library
extern "C" BOOL WINAPI FreeLibrary(HMODULE hLibModule) {
    (void)hLibModule;
    return TRUE;
}

// GetProcAddress - get procedure address
extern "C" void* WINAPI GetProcAddress(HMODULE hModule, const char* lpProcName) {
    (void)hModule; (void)lpProcName;
    return nullptr;
}

// --- Debug output ---

// OutputDebugStringW - debug output stub
extern "C" void WINAPI OutputDebugStringW(const wchar_t* lpcString) {
    (void)lpcString;
    // No-op for game runtime
}

// OutputDebugStringA - ANSI version
extern "C" void WINAPI OutputDebugStringA(const char* lpcString) {
    (void)lpcString;
}

// --- File operations ---

// CreateFileW - create file
extern "C" HANDLE WINAPI CreateFileW(
    const wchar_t* lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    void* lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile
) {
    (void)lpFileName; (void)dwDesiredAccess; (void)dwShareMode;
    (void)lpSecurityAttributes; (void)dwCreationDisposition;
    (void)dwFlagsAndAttributes; (void)hTemplateFile;
    return reinterpret_cast<HANDLE>(-1);  // INVALID_HANDLE_VALUE
}

// CreateFileA - ANSI version
extern "C" HANDLE WINAPI CreateFileA(
    const char* lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    void* lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile
) {
    (void)lpFileName; (void)dwDesiredAccess; (void)dwShareMode;
    (void)lpSecurityAttributes; (void)dwCreationDisposition;
    (void)dwFlagsAndAttributes; (void)hTemplateFile;
    return reinterpret_cast<HANDLE>(-1);
}

// CloseHandle - close handle
extern "C" BOOL WINAPI CloseHandle(HANDLE hObject) {
    (void)hObject;
    return TRUE;
}

// ReadFile - read file
extern "C" BOOL WINAPI ReadFile(
    HANDLE hFile,
    void* lpBuffer,
    DWORD nNumberOfBytesToRead,
    DWORD* lpNumberOfBytesRead,
    void* lpOverlapped
) {
    (void)hFile; (void)lpBuffer; (void)nNumberOfBytesToRead;
    (void)lpNumberOfBytesRead; (void)lpOverlapped;
    return FALSE;
}

// WriteFile - write file
extern "C" BOOL WINAPI WriteFile(
    HANDLE hFile,
    const void* lpBuffer,
    DWORD nNumberOfBytesToWrite,
    DWORD* lpNumberOfBytesWritten,
    void* lpOverlapped
) {
    (void)hFile; (void)lpBuffer; (void)nNumberOfBytesToWrite;
    (void)lpNumberOfBytesWritten; (void)lpOverlapped;
    return FALSE;
}

// SetFilePointer - set file pointer
extern "C" DWORD WINAPI SetFilePointer(
    HANDLE hFile,
    LONG lDistanceToMove,
    LONG* lpDistanceToMoveHigh,
    DWORD dwMoveMethod
) {
    (void)hFile; (void)lDistanceToMove; (void)lpDistanceToMoveHigh; (void)dwMoveMethod;
    return 0xFFFFFFFF;
}

// --- Memory operations ---

// RtlZeroMemory - zero memory
extern "C" void WINAPI RtlZeroMemory(void* Destination, DWORD Length) {
    if (Destination) {
        std::memset(Destination, 0, Length);
    }
}

// RtlMoveMemory - move memory
extern "C" void WINAPI RtlMoveMemory(void* Destination, const void* Source, DWORD Length) {
    if (Destination && Source) {
        std::memmove(Destination, Source, Length);
    }
}

// RtlCopyMemory - copy memory
extern "C" void WINAPI RtlCopyMemory(void* Destination, const void* Source, DWORD Length) {
    if (Destination && Source) {
        std::memcpy(Destination, Source, Length);
    }
}

// --- Thread/Process ---

// GetCurrentProcessId
extern "C" DWORD WINAPI GetCurrentProcessId(void) {
    return 1;
}

// GetCurrentThreadId
extern "C" DWORD WINAPI GetCurrentThreadId(void) {
    return 1;
}

// GetCurrentProcess
extern "C" HANDLE WINAPI GetCurrentProcess(void) {
    return reinterpret_cast<HANDLE>(-1);
}

// GetCurrentThread
extern "C" HANDLE WINAPI GetCurrentThread(void) {
    return reinterpret_cast<HANDLE>(-2);
}

// --- Error handling ---

// GetLastError
extern "C" DWORD WINAPI GetLastError(void) {
    return 0;
}

// SetLastError
extern "C" void WINAPI SetLastError(DWORD dwErrCode) {
    (void)dwErrCode;
}

// --- Critical Section ---

typedef struct _CRITICAL_SECTION {
    void* DebugInfo;
    LONG LockCount;
    LONG RecursionCount;
    HANDLE OwningThread;
    HANDLE LockSemaphore;
    ULONGLONG SpinCount;
} CRITICAL_SECTION, *PCRITICAL_SECTION;

extern "C" void WINAPI InitializeCriticalSection(CRITICAL_SECTION* lpCriticalSection) {
    if (lpCriticalSection) {
        std::memset(lpCriticalSection, 0, sizeof(CRITICAL_SECTION));
    }
}

extern "C" void WINAPI DeleteCriticalSection(CRITICAL_SECTION* lpCriticalSection) {
    (void)lpCriticalSection;
}

extern "C" void WINAPI EnterCriticalSection(CRITICAL_SECTION* lpCriticalSection) {
    (void)lpCriticalSection;
}

extern "C" void WINAPI LeaveCriticalSection(CRITICAL_SECTION* lpCriticalSection) {
    (void)lpCriticalSection;
}

// --- Heap ---

// HeapAlloc
extern "C" LPVOID WINAPI HeapAlloc(HANDLE hHeap, DWORD dwFlags, SIZE_T dwBytes) {
    (void)hHeap; (void)dwFlags;
    return std::malloc(dwBytes);
}

// HeapFree
extern "C" BOOL WINAPI HeapFree(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem) {
    (void)hHeap; (void)dwFlags; (void)lpMem;
    std::free(lpMem);
    return TRUE;
}

// HeapReAlloc
extern "C" LPVOID WINAPI HeapReAlloc(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem, SIZE_T dwBytes) {
    (void)hHeap; (void)dwFlags;
    return std::realloc(lpMem, dwBytes);
}

// GetProcessHeap
extern "C" HANDLE WINAPI GetProcessHeap(void) {
    return reinterpret_cast<HANDLE>(1);
}

// --- Time ---

// QueryPerformanceCounter
extern "C" BOOL WINAPI QueryPerformanceCounter(int64_t* lpPerformanceCount) {
    if (lpPerformanceCount) {
        *lpPerformanceCount = 0;
    }
    return TRUE;
}

// QueryPerformanceFrequency
extern "C" BOOL WINAPI QueryPerformanceFrequency(int64_t* lpFrequency) {
    if (lpFrequency) {
        *lpFrequency = 1000000;
    }
    return TRUE;
}

// timeGetTime (multimedia timer)
extern "C" DWORD WINAPI timeGetTime(void) {
    return 0;
}