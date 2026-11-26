// input_types.h
// Type definitions for the Input Subsystem (XInput → Android AInput mapping)

#pragma once

#include <cstdint>

// --- Windows basic types ---
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;

// Forward declarations for Android types
struct AInputQueue;
struct AInputEvent;

// --- XInput Button Enums ---
enum class XInputButton : UINT16 {
    UP = 0x0001,
    DOWN = 0x0002,
    LEFT = 0x0004,
    RIGHT = 0x0008,
    START = 0x0010,
    BACK = 0x0020,
    LEFT_THUMB_BUTTON = 0x0100,
    RIGHT_THUMB_BUTTON = 0x0200,
    LEFT_SHOULDER = 0x0400,
    RIGHT_SHOULDER = 0x0800,
    GUIDE = 0x1000,
    A = 0x2000,
    B = 0x4000,
    X = 0x8000,
    Y = 0x0001,  // Fixed: was 0x0000
};

// --- XInput State ---
struct XInputState {
    UINT16 dwPacketNumber;
    UINT16 buttons;
    float fLeftTrigger;
    float fRightTrigger;
    float sThumbLY;  // Left thumbstick Y
    float sThumbLX;  // Left thumbstick X
    float sThumbRY;  // Right thumbstick Y
    float sThumbRX;  // Right thumbstick X
};

// --- Android Key Codes ---
enum class AndroidKey : UINT32 {
    UNKNOWN = 0,
    BACK = 4,
    MENU = 82,
    SEARCH = 84,
    CALL = 5,
    ENDCALL = 6,
    DPAD_UP = 19,
    DPAD_DOWN = 20,
    DPAD_LEFT = 21,
    DPAD_RIGHT = 22,
    BUTTON_A = 96,
    BUTTON_B = 97,
    BUTTON_X = 99,
    BUTTON_Y = 100,
    BUTTON_L1 = 102,
    BUTTON_R1 = 103,
    BUTTON_L2 = 104,
    BUTTON_R2 = 105,
    BUTTON_THUMBL = 106,
    BUTTON_THUMBR = 107,
    BUTTON_START = 108,
    BUTTON_SELECT = 109,
    BUTTON_MODE = 110,
    // ... more keys
};

// --- Key Event Structure ---
struct KeyEvent {
    AndroidKey key;
    bool pressed;
    UINT64 timestamp;
};

// --- Touch Event Structure ---
struct TouchEvent {
    int x;
    int y;
    int action;  // 0 = up, 1 = down, 2 = move
    UINT64 timestamp;
};

// --- Gamepad State ---
struct GamepadState {
    UINT16 buttons;
    bool up;
    bool down;
    bool left;
    bool right;
    bool start;
    bool back;
    bool left_shoulder;
    bool right_shoulder;
    bool a;
    bool b;
    bool x;
    bool y;
    float left_trigger;
    float right_trigger;
    float left_stick_x;
    float left_stick_y;
    float right_stick_x;
    float right_stick_y;
};

// --- Function Declarations ---
// Initialize input subsystem
extern "C" bool input_init(void);

// Shutdown input subsystem
extern "C" void input_shutdown(void);

// Process XInput state and map to Android input
extern "C" void input_process_state(const XInputState& state);

// Handle key down event
extern "C" void input_key_down(AndroidKey key);

// Handle key up event
extern "C" void input_key_up(AndroidKey key);

// Handle touch event
extern "C" void input_touch_event(const TouchEvent& event);

// Get current gamepad state
extern "C" GamepadState input_get_gamepad_state(void);

// Poll and process pending input events
extern "C" void input_poll_events(void);

// Set the Android input queue (called from native activity)
extern "C" void input_set_input_queue(AInputQueue* queue);

// Check if a key is currently pressed
extern "C" bool input_is_key_pressed(AndroidKey key);

// Get pending key events (for game to poll)
extern "C" UINT32 input_get_key_events(KeyEvent* outEvents, UINT32 maxEvents);

// Get pending touch events (for game to poll)
extern "C" UINT32 input_get_touch_events(TouchEvent* outEvents, UINT32 maxEvents);

// --- XInput Interface Stubs ---
// XInputGetState
extern "C" UINT32 xinput_get_state(UINT32 userIndex, XInputState* state);

// XInputSetState (vibration)
extern "C" UINT32 xinput_set_state(UINT32 userIndex, void* vibration);

// XInputGetCapabilities
extern "C" UINT32 xinput_get_capabilities(UINT32 userIndex, UINT32 flags, void* capabilities);

// XInputEnable
extern "C" void xinput_enable(UINT32 enable);

// --- Touch Controls (Phase 6) ---
// Initialize touch control layout for screen size
extern "C" void touch_controls_init(int width, int height);

// Touch event handlers (called from Java/JNI)
extern "C" void touch_controls_on_touch_down_c(int pointer_id, int x, int y);
extern "C" void touch_controls_on_touch_move_c(int pointer_id, int x, int y);
extern "C" void touch_controls_on_touch_up_c(int pointer_id, int x, int y);

// Update gamepad state from touch controls
extern "C" void touch_controls_update_gamepad_c(GamepadState* gamepad);

// Render data for drawing touch control overlays
struct TouchControlRenderData {
    struct Circle {
        float x, y, radius;
        uint32_t color;
        bool filled;
    } circles[16];
    int circle_count;
};

extern "C" void touch_controls_get_render_data_c(void* out_data);