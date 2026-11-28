// Roadmap step 42: window and message loop.
//
// Implements a per-process window registry and FIFO message queue. Window
// messages are enqueued in the same order Windows documents them (create,
// show, resize, focus, input, quit) and DispatchMessage calls the window
// procedure through the host/guest call bridge.

#include "rottr/x64_bridge.h"

#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>

namespace rottr {
namespace {

constexpr uint32_t kMsgCreate = 0x0001;
constexpr uint32_t kMsgDestroy = 0x0002;
constexpr uint32_t kMsgMove = 0x0003;
constexpr uint32_t kMsgSize = 0x0005;
constexpr uint32_t kMsgSetFocus = 0x0007;
constexpr uint32_t kMsgKillFocus = 0x0008;
constexpr uint32_t kMsgSetText = 0x000c;
constexpr uint32_t kMsgShowWindow = 0x0018;
constexpr uint32_t kMsgWindowPosChanged = 0x0047;
constexpr uint32_t kMsgNcDestroy = 0x0082;
constexpr uint32_t kMsgQuit = 0x0012;

struct Message {
    uint64_t hwnd = 0;
    uint32_t message = 0;
    uint64_t wParam = 0;
    uint64_t lParam = 0;
    uint32_t time = 0;
};

struct WindowInfo {
    std::string className;
    uint64_t wndProc = 0;
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;
    bool visible = false;
    bool destroyed = false;
};

std::mutex g_mutex;
std::condition_variable g_messageReady;
std::map<uint64_t, WindowInfo> g_windows;
std::map<std::string, uint64_t> g_classes;
std::deque<Message> g_queue;
uint64_t g_nextWindow = 0x10000ULL;
uint64_t g_focus = 0;
uint32_t g_classAtom = 0xc000;

void enqueue(const Message& message) {
    g_queue.push_back(message);
    g_messageReady.notify_all();
}

void fillMessage(uint64_t msgPtr, const Message& message) {
    if (msgPtr == 0) return;
    auto* out = reinterpret_cast<uint8_t*>(msgPtr);
    std::memset(out, 0, 48);
    std::memcpy(out + 0, &message.hwnd, 8);
    std::memcpy(out + 8, &message.message, 4);
    std::memcpy(out + 16, &message.wParam, 8);
    std::memcpy(out + 24, &message.lParam, 8);
    std::memcpy(out + 32, &message.time, 4);
}

bool matches(const Message& message, uint64_t filter) {
    return filter == 0 || message.hwnd == filter;
}

// Windows treats a zero filter range (min == max == 0) as "all messages".
bool inRange(const Message& message, uint64_t minMessage, uint64_t maxMessage) {
    if (minMessage == 0 && maxMessage == 0) return true;
    return message.message >= minMessage && message.message <= maxMessage;
}

}  // namespace

extern "C" uint64_t host_RegisterClassA(uint64_t wndClassPtr, uint64_t, uint64_t,
                                        uint64_t, uint64_t, uint64_t, uint64_t,
                                        uint64_t) {
    if (wndClassPtr == 0) return 0;
    const auto* bytes = reinterpret_cast<const uint8_t*>(wndClassPtr);
    uint64_t wndProc = 0;
    uint64_t namePtr = 0;
    std::memcpy(&wndProc, bytes + 8, 8);
    std::memcpy(&namePtr, bytes + 64, 8);
    if (wndProc == 0 || namePtr == 0) return 0;
    const std::string name(reinterpret_cast<const char*>(namePtr));
    std::lock_guard<std::mutex> lock(g_mutex);
    g_classes[name] = wndProc;
    g_classAtom += 1;
    return g_classAtom;
}

extern "C" uint64_t host_RegisterClassExA(uint64_t wndClassPtr, uint64_t, uint64_t,
                                          uint64_t, uint64_t, uint64_t, uint64_t,
                                          uint64_t) {
    return host_RegisterClassA(wndClassPtr, 0, 0, 0, 0, 0, 0, 0);
}

extern "C" uint64_t host_CreateWindowExA(uint64_t exStyle, uint64_t classNamePtr,
                                         uint64_t windowNamePtr, uint64_t style,
                                         uint64_t x, uint64_t y, uint64_t width,
                                         uint64_t height) {
    (void)exStyle;
    (void)style;
    if (classNamePtr == 0) return 0;
    const std::string name(reinterpret_cast<const char*>(classNamePtr));
    std::lock_guard<std::mutex> lock(g_mutex);
    auto cls = g_classes.find(name);
    if (cls == g_classes.end()) return 0;
    const uint64_t hwnd = g_nextWindow;
    g_nextWindow += 0x10;
    WindowInfo info;
    info.className = name;
    info.wndProc = cls->second;
    info.x = static_cast<int32_t>(x);
    info.y = static_cast<int32_t>(y);
    info.width = static_cast<int32_t>(width);
    info.height = static_cast<int32_t>(height);
    g_windows[hwnd] = info;
    (void)windowNamePtr;
    enqueue(Message{hwnd, kMsgCreate, 0, 0, 0});
    return hwnd;
}

extern "C" uint64_t host_DestroyWindow(uint64_t hwnd, uint64_t, uint64_t, uint64_t,
                                       uint64_t, uint64_t, uint64_t, uint64_t) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_windows.find(hwnd);
    if (it == g_windows.end()) return 0;
    if (!it->second.destroyed) {
        it->second.destroyed = true;
        enqueue(Message{hwnd, kMsgDestroy, 0, 0, 0});
        enqueue(Message{hwnd, kMsgNcDestroy, 0, 0, 0});
    }
    if (g_focus == hwnd) g_focus = 0;
    return 1;
}

extern "C" uint64_t host_ShowWindow(uint64_t hwnd, uint64_t command, uint64_t,
                                    uint64_t, uint64_t, uint64_t, uint64_t,
                                    uint64_t) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_windows.find(hwnd);
    if (it == g_windows.end()) return 0;
    const uint64_t previous = it->second.visible ? 1 : 0;
    it->second.visible = command != 0;  // SW_HIDE == 0
    enqueue(Message{hwnd, kMsgShowWindow, command, 0, 0});
    return previous;
}

extern "C" uint64_t host_MoveWindow(uint64_t hwnd, uint64_t x, uint64_t y,
                                    uint64_t width, uint64_t height, uint64_t repaint,
                                    uint64_t, uint64_t) {
    (void)repaint;
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_windows.find(hwnd);
    if (it == g_windows.end()) return 0;
    it->second.x = static_cast<int32_t>(x);
    it->second.y = static_cast<int32_t>(y);
    it->second.width = static_cast<int32_t>(width);
    it->second.height = static_cast<int32_t>(height);
    enqueue(Message{hwnd, kMsgWindowPosChanged, 0, 0, 0});
    enqueue(Message{hwnd, kMsgSize, 0,
                    (height & 0xffffULL) << 16 | (width & 0xffffULL), 0});
    enqueue(Message{hwnd, kMsgMove, 0,
                    (y & 0xffffULL) << 16 | (x & 0xffffULL), 0});
    return 1;
}

extern "C" uint64_t host_SetWindowPos(uint64_t hwnd, uint64_t after, uint64_t x,
                                      uint64_t y, uint64_t width, uint64_t height,
                                      uint64_t flags, uint64_t) {
    (void)after;
    (void)flags;
    return host_MoveWindow(hwnd, x, y, width, height, 1, 0, 0);
}

extern "C" uint64_t host_SetFocus(uint64_t hwnd, uint64_t, uint64_t, uint64_t,
                                  uint64_t, uint64_t, uint64_t, uint64_t) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (hwnd != 0 && g_windows.find(hwnd) == g_windows.end()) return 0;
    const uint64_t previous = g_focus;
    if (previous == hwnd) return previous;
    if (previous != 0 && g_windows.find(previous) != g_windows.end()) {
        enqueue(Message{previous, kMsgKillFocus, hwnd, 0, 0});
    }
    g_focus = hwnd;
    if (hwnd != 0) enqueue(Message{hwnd, kMsgSetFocus, previous, 0, 0});
    return previous;
}

extern "C" uint64_t host_GetFocus(uint64_t, uint64_t, uint64_t, uint64_t,
                                  uint64_t, uint64_t, uint64_t, uint64_t) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_focus;
}

extern "C" uint64_t host_SetWindowTextA(uint64_t hwnd, uint64_t textPtr,
                                        uint64_t, uint64_t, uint64_t, uint64_t,
                                        uint64_t, uint64_t) {
    (void)textPtr;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_windows.find(hwnd) == g_windows.end()) return 0;
    enqueue(Message{hwnd, kMsgSetText, 0, 0, 0});
    return 1;
}

extern "C" uint64_t host_PostMessageA(uint64_t hwnd, uint64_t message,
                                      uint64_t wParam, uint64_t lParam,
                                      uint64_t, uint64_t, uint64_t, uint64_t) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (hwnd != 0 && g_windows.find(hwnd) == g_windows.end()) return 0;
    enqueue(Message{hwnd, static_cast<uint32_t>(message), wParam, lParam, 0});
    return 1;
}

extern "C" uint64_t host_PostQuitMessage(uint64_t exitCode, uint64_t, uint64_t,
                                         uint64_t, uint64_t, uint64_t, uint64_t,
                                         uint64_t) {
    std::lock_guard<std::mutex> lock(g_mutex);
    enqueue(Message{0, kMsgQuit, exitCode, 0, 0});
    return 0;
}

extern "C" uint64_t host_GetMessageA(uint64_t msgPtr, uint64_t hwnd,
                                     uint64_t minMessage, uint64_t maxMessage,
                                     uint64_t, uint64_t, uint64_t, uint64_t) {
    std::unique_lock<std::mutex> lock(g_mutex);
    g_messageReady.wait(lock, [&] {
        for (const Message& message : g_queue) {
            if (!matches(message, hwnd)) continue;
            if (!inRange(message, minMessage, maxMessage)) continue;
            return true;
        }
        return false;
    });
    for (auto it = g_queue.begin(); it != g_queue.end(); ++it) {
        if (!matches(*it, hwnd)) continue;
        if (!inRange(*it, minMessage, maxMessage)) continue;
        const Message message = *it;
        g_queue.erase(it);
        fillMessage(msgPtr, message);
        return message.message == kMsgQuit ? 0 : 1;
    }
    return 0;
}

extern "C" uint64_t host_PeekMessageA(uint64_t msgPtr, uint64_t hwnd,
                                      uint64_t minMessage, uint64_t maxMessage,
                                      uint64_t remove, uint64_t, uint64_t,
                                      uint64_t) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto it = g_queue.begin(); it != g_queue.end(); ++it) {
        if (!matches(*it, hwnd)) continue;
        if (!inRange(*it, minMessage, maxMessage)) continue;
        const Message message = *it;
        if (remove != 0) g_queue.erase(it);
        fillMessage(msgPtr, message);
        return 1;
    }
    return 0;
}

extern "C" uint64_t host_TranslateMessage(uint64_t, uint64_t, uint64_t, uint64_t,
                                          uint64_t, uint64_t, uint64_t, uint64_t) {
    return 0;
}

extern "C" uint64_t host_DispatchMessageA(uint64_t msgPtr, uint64_t, uint64_t,
                                          uint64_t, uint64_t, uint64_t, uint64_t,
                                          uint64_t) {
    if (msgPtr == 0) return 0;
    const auto* bytes = reinterpret_cast<const uint8_t*>(msgPtr);
    uint64_t hwnd = 0;
    uint32_t message = 0;
    uint64_t wParam = 0;
    uint64_t lParam = 0;
    std::memcpy(&hwnd, bytes + 0, 8);
    std::memcpy(&message, bytes + 8, 4);
    std::memcpy(&wParam, bytes + 16, 8);
    std::memcpy(&lParam, bytes + 24, 8);
    uint64_t wndProc = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_windows.find(hwnd);
        if (it == g_windows.end()) return 0;
        wndProc = it->second.wndProc;
    }
    HostProc proc = resolveHostCallProc(wndProc);
    if (proc == nullptr) return 0;  // guest window procs are dispatched by the CPU
    return proc(hwnd, message, wParam, lParam, 0, 0, 0, 0);
}

extern "C" uint64_t host_DefWindowProcA(uint64_t, uint64_t, uint64_t, uint64_t,
                                        uint64_t, uint64_t, uint64_t, uint64_t) {
    return 0;
}

void registerWindowHostModule() {
    HostModule module;
    module.name = "user32.dll";
    module.exportsByName = {
        {"RegisterClassA", reinterpret_cast<uint64_t>(&host_RegisterClassA)},
        {"RegisterClassExA", reinterpret_cast<uint64_t>(&host_RegisterClassExA)},
        {"CreateWindowExA", reinterpret_cast<uint64_t>(&host_CreateWindowExA)},
        {"DestroyWindow", reinterpret_cast<uint64_t>(&host_DestroyWindow)},
        {"ShowWindow", reinterpret_cast<uint64_t>(&host_ShowWindow)},
        {"MoveWindow", reinterpret_cast<uint64_t>(&host_MoveWindow)},
        {"SetWindowPos", reinterpret_cast<uint64_t>(&host_SetWindowPos)},
        {"SetFocus", reinterpret_cast<uint64_t>(&host_SetFocus)},
        {"GetFocus", reinterpret_cast<uint64_t>(&host_GetFocus)},
        {"SetWindowTextA", reinterpret_cast<uint64_t>(&host_SetWindowTextA)},
        {"PostMessageA", reinterpret_cast<uint64_t>(&host_PostMessageA)},
        {"PostQuitMessage", reinterpret_cast<uint64_t>(&host_PostQuitMessage)},
        {"GetMessageA", reinterpret_cast<uint64_t>(&host_GetMessageA)},
        {"PeekMessageA", reinterpret_cast<uint64_t>(&host_PeekMessageA)},
        {"TranslateMessage", reinterpret_cast<uint64_t>(&host_TranslateMessage)},
        {"DispatchMessageA", reinterpret_cast<uint64_t>(&host_DispatchMessageA)},
        {"DefWindowProcA", reinterpret_cast<uint64_t>(&host_DefWindowProcA)},
    };
    std::lock_guard<std::mutex> lock(hostModulesMutex());
    hostModules().push_back(std::move(module));
}

}  // namespace rottr
