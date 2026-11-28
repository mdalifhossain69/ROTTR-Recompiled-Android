// Roadmap step 42: Implement the window and message loop.
// Pass: create, resize, focus, input and quit messages arrive in order.
#include "rottr/x64_bridge.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

extern "C" {
namespace rottr {
uint64_t host_RegisterClassA(uint64_t wndClass, uint64_t, uint64_t, uint64_t,
                             uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t host_CreateWindowExA(uint64_t exStyle, uint64_t className,
                              uint64_t windowName, uint64_t style, uint64_t x,
                              uint64_t y, uint64_t width, uint64_t height);
uint64_t host_DestroyWindow(uint64_t hwnd, uint64_t, uint64_t, uint64_t, uint64_t,
                            uint64_t, uint64_t, uint64_t);
uint64_t host_ShowWindow(uint64_t hwnd, uint64_t command, uint64_t, uint64_t,
                         uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t host_MoveWindow(uint64_t hwnd, uint64_t x, uint64_t y, uint64_t width,
                         uint64_t height, uint64_t repaint, uint64_t, uint64_t);
uint64_t host_SetFocus(uint64_t hwnd, uint64_t, uint64_t, uint64_t, uint64_t,
                       uint64_t, uint64_t, uint64_t);
uint64_t host_GetFocus(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                       uint64_t, uint64_t);
uint64_t host_PostMessageA(uint64_t hwnd, uint64_t message, uint64_t wParam,
                           uint64_t lParam, uint64_t, uint64_t, uint64_t,
                           uint64_t);
uint64_t host_PostQuitMessage(uint64_t exitCode, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t host_GetMessageA(uint64_t msg, uint64_t hwnd, uint64_t minMessage,
                          uint64_t maxMessage, uint64_t, uint64_t, uint64_t,
                          uint64_t);
uint64_t host_PeekMessageA(uint64_t msg, uint64_t hwnd, uint64_t minMessage,
                           uint64_t maxMessage, uint64_t remove, uint64_t,
                           uint64_t, uint64_t);
uint64_t host_DispatchMessageA(uint64_t msg, uint64_t, uint64_t, uint64_t,
                               uint64_t, uint64_t, uint64_t, uint64_t);
}  // namespace rottr
}

namespace {

constexpr uint32_t kMsgCreate = 0x0001;
constexpr uint32_t kMsgDestroy = 0x0002;
constexpr uint32_t kMsgMove = 0x0003;
constexpr uint32_t kMsgSize = 0x0005;
constexpr uint32_t kMsgSetFocus = 0x0007;
constexpr uint32_t kMsgKillFocus = 0x0008;
constexpr uint32_t kMsgShowWindow = 0x0018;
constexpr uint32_t kMsgWindowPosChanged = 0x0047;
constexpr uint32_t kMsgNcDestroy = 0x0082;
constexpr uint32_t kMsgKeyDown = 0x0100;
constexpr uint32_t kMsgChar = 0x0102;
constexpr uint32_t kMsgMouseMove = 0x0200;

// Windows x64 MSG layout: 48 bytes.
struct TestMsg {
    uint64_t hwnd;
    uint32_t message;
    uint32_t padding;
    uint64_t wParam;
    uint64_t lParam;
    uint32_t time;
    int32_t pointX;
    int32_t pointY;
    uint32_t privateData;
};

static_assert(sizeof(TestMsg) == 48, "MSG layout must match Windows x64");

std::vector<uint32_t> g_messages;
std::vector<uint64_t> g_lParams;

bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

extern "C" uint64_t testWndProc(uint64_t, uint64_t message, uint64_t, uint64_t lParam,
                                uint64_t, uint64_t, uint64_t, uint64_t) {
    g_messages.push_back(static_cast<uint32_t>(message));
    g_lParams.push_back(lParam);
    return 0;
}

}  // namespace

int main() {
    const uint64_t wndProc = rottr::addHostCallProc(&testWndProc);

    uint8_t windowClass[72] = {};
    std::memcpy(windowClass + 8, &wndProc, sizeof(wndProc));
    const uint64_t className = reinterpret_cast<uint64_t>("ROTTRTest");
    std::memcpy(windowClass + 64, &className, sizeof(className));
    if (!expect(rottr::host_RegisterClassA(reinterpret_cast<uint64_t>(windowClass),
                                           0, 0, 0, 0, 0, 0, 0) != 0,
                "RegisterClassA must return a class atom"))
        return 1;

    const uint64_t window = rottr::host_CreateWindowExA(
        0, className, reinterpret_cast<uint64_t>("ROTTR"), 0x00cf0000U, 0, 0, 800,
        600);
    if (!expect(window != 0, "CreateWindowExA must return a window handle")) return 1;

    rottr::host_ShowWindow(window, 5, 0, 0, 0, 0, 0, 0);  // SW_SHOW
    rottr::host_MoveWindow(window, 10, 20, 640, 480, 1, 0, 0);
    if (!expect(rottr::host_SetFocus(window, 0, 0, 0, 0, 0, 0, 0) == 0,
                "first SetFocus must return the previous (null) focus"))
        return 1;

    const uint64_t second = rottr::host_CreateWindowExA(
        0, className, reinterpret_cast<uint64_t>("Second"), 0x00cf0000U, 0, 0, 320,
        240);
    if (!expect(second != 0 && second != window,
                "second CreateWindowExA must return a distinct window"))
        return 1;
    if (!expect(rottr::host_SetFocus(second, 0, 0, 0, 0, 0, 0, 0) == window,
                "SetFocus must return the previously focused window"))
        return 1;
    if (!expect(rottr::host_GetFocus(0, 0, 0, 0, 0, 0, 0, 0) == second,
                "GetFocus must report the new focus"))
        return 1;

    if (!expect(rottr::host_PostMessageA(window, kMsgKeyDown, 0x0d, 0, 0, 0, 0, 0) ==
                    1,
                "PostMessageA(WM_KEYDOWN) must succeed"))
        return 1;
    if (!expect(rottr::host_PostMessageA(window, kMsgChar, '\r', 0, 0, 0, 0, 0) == 1,
                "PostMessageA(WM_CHAR) must succeed"))
        return 1;
    if (!expect(rottr::host_PostMessageA(window, kMsgMouseMove, 0, 0, 0, 0, 0, 0) ==
                    1,
                "PostMessageA(WM_MOUSEMOVE) must succeed"))
        return 1;
    if (!expect(rottr::host_PostMessageA(0xdeadbeefU, kMsgKeyDown, 0, 0, 0, 0, 0,
                                         0) == 0,
                "PostMessageA to an invalid window must fail"))
        return 1;

    // PeekMessage observes the mouse-move message without consuming it, so
    // the main loop still dispatches it in order.
    TestMsg peeked{};
    if (!expect(rottr::host_PeekMessageA(reinterpret_cast<uint64_t>(&peeked), window,
                                         kMsgMouseMove, kMsgMouseMove, 0, 0, 0, 0) ==
                    1 &&
                    peeked.message == kMsgMouseMove,
                "PeekMessageA must observe the posted mouse message"))
        return 1;

    if (!expect(rottr::host_PostQuitMessage(7, 0, 0, 0, 0, 0, 0, 0) == 0,
                "PostQuitMessage must succeed"))
        return 1;

    // ---- Message loop ----
    uint64_t quitParam = 0;
    for (;;) {
        TestMsg msg{};
        const uint64_t result = rottr::host_GetMessageA(
            reinterpret_cast<uint64_t>(&msg), 0, 0, 0, 0, 0, 0, 0);
        if (result == static_cast<uint64_t>(-1)) {
            std::fprintf(stderr, "FAIL: GetMessageA returned -1\n");
            return 1;
        }
        if (result == 0) {
            quitParam = msg.wParam;
            break;
        }
        rottr::host_DispatchMessageA(reinterpret_cast<uint64_t>(&msg), 0, 0, 0, 0, 0,
                                     0, 0);
    }

    const std::vector<uint32_t> expected = {
        kMsgCreate, kMsgShowWindow, kMsgWindowPosChanged, kMsgSize, kMsgMove,
        kMsgSetFocus, kMsgCreate, kMsgKillFocus, kMsgSetFocus, kMsgKeyDown,
        kMsgChar, kMsgMouseMove,
    };
    if (!expect(g_messages == expected,
                "messages must arrive in create/resize/focus/input/quit order")) {
        std::fprintf(stderr, "  got:");
        for (uint32_t id : g_messages) std::fprintf(stderr, " 0x%04x", id);
        std::fprintf(stderr, "\n");
        return 1;
    }
    if (!expect(g_lParams[3] == ((480ULL << 16) | 640ULL),
                "WM_SIZE lParam must carry the new width and height"))
        return 1;
    if (!expect(g_lParams[4] == ((20ULL << 16) | 10ULL),
                "WM_MOVE lParam must carry the new x and y"))
        return 1;
    if (!expect(quitParam == 7, "WM_QUIT must carry the exit code")) return 1;

    // Destroy enqueues WM_DESTROY then WM_NCDESTROY, in that order.
    if (!expect(rottr::host_DestroyWindow(window, 0, 0, 0, 0, 0, 0, 0) == 1,
                "DestroyWindow must succeed"))
        return 1;
    TestMsg destroyMsg{};
    if (!expect(rottr::host_PeekMessageA(reinterpret_cast<uint64_t>(&destroyMsg),
                                         window, 0, 0, 1, 0, 0, 0) == 1 &&
                    destroyMsg.message == kMsgDestroy,
                "WM_DESTROY must be queued first"))
        return 1;
    if (!expect(rottr::host_PeekMessageA(reinterpret_cast<uint64_t>(&destroyMsg),
                                         window, 0, 0, 1, 0, 0, 0) == 1 &&
                    destroyMsg.message == kMsgNcDestroy,
                "WM_NCDESTROY must follow WM_DESTROY"))
        return 1;

    std::puts("OK (create/resize/focus/input/quit messages ordered)");
    return 0;
}
