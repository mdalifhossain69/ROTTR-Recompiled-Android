// runtime/src/win32_stubs.cpp
// Basic Windows API stubs for static recompilation.
// These provide minimal implementations needed for the game to link and run.

#include <cstdint>
#include <cstring>

// --- Basic types ---
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef uint64_t QWORD;
typedef uint32_t DWORD;
typedef uint16_t WORD;
typedef uint8_t BYTE;

// --- Function pointer types ---
typedef DWORD (WINAPI *LPTHREAD_START_ROUTINE)(LPVOID);
typedef VOID (WINAPI *PTOP_LEVEL_EXCEPTION_FILTER)(struct _EXCEPTION_POINTERS*);
typedef void (WINAPI *PPS_GET_MODULE_FILE_NAME_W)(HMODULE, LPWSTR, DWORD);

// --- Core stubs ---

// VirtualAlloc - memory allocation
extern "C" void* WINAPI VirtualAlloc(void* lpAddress, DWORD dwSize, DWORD flAllocationType, DWORD flProtect) {
    // Simple: just return address if within our guest memory range, else null
    static uint8_t guest_memory[0x100000];
    if ((uint64_t)lpAddress + dwSize <= sizeof(guest_memory)) {
        return lpAddress;
    }
    return nullptr;
}

// --- Critical game APIs stubs ---

// Sleep - minimal delay
extern "C" void WINAPI Sleep(DWORD dwMilliseconds) {
    // No-op stub for game timing
}

// GetTickCount - return simulated time
extern "C" DWORD WINAPI GetTickCount(void) {
    return 0;  // Simplified
}

// --- String/Buffer operations ---

// lstrlenW - string length
extern "C" int WINAPI lstrlenW(const wchar_t* lpString) {
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

// --- Module operations ---

// GetModuleHandleW - module handle
extern "C" HMODULE WINAPI GetModuleHandleW(const wchar_t* lpModuleName) {
    // Simplified: return null for now, game may handle this
    return nullptr;
}

// --- Debug output ---

// OutputDebugStringW - debug output stub
extern "C" void WINAPI OutputDebugStringW(const wchar_t* lpcString) {
    // No-op for game runtime
}

