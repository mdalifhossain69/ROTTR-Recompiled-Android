// input.cpp
// XInput → Android AInput mapping implementation
// Provides the function declarations from input_types.h

#include "input/input_types.h"
#include <cstdint>
#include <cstring>
#include <mutex>
#include <queue>
#include <vector>

// Platform-specific includes for Android
#if defined(__ANDROID__)
#include <android/input.h>
#include <android/keycodes.h>
#include <android/native_activity.h>
#else
// Stub definitions for non-Android builds
typedef struct AInputQueue AInputQueue;
typedef struct AInputEvent AInputEvent;
#define AINPUT_EVENT_TYPE_KEY 1
#define AINPUT_EVENT_TYPE_MOTION 2
#define AKEY_EVENT_ACTION_DOWN 0
#define AKEY_EVENT_ACTION_UP 1
#define AMOTION_EVENT_ACTION_DOWN 0
#define AMOTION_EVENT_ACTION_UP 1
#define AMOTION_EVENT_ACTION_MOVE 2
#define AMOTION_EVENT_ACTION_POINTER_DOWN 5
#define AMOTION_EVENT_ACTION_POINTER_UP 6
#define AKEYCODE_BACK 4
#define AKEYCODE_MENU 82
#define AKEYCODE_DPAD_UP 19
#define AKEYCODE_DPAD_DOWN 20
#define AKEYCODE_DPAD_LEFT 21
#define AKEYCODE_DPAD_RIGHT 22
#define AKEYCODE_BUTTON_A 96
#define AKEYCODE_BUTTON_B 97
#define AKEYCODE_BUTTON_X 99
#define AKEYCODE_BUTTON_Y 100
#define AKEYCODE_BUTTON_L1 102
#define AKEYCODE_BUTTON_R1 103
#define AKEYCODE_BUTTON_L2 104
#define AKEYCODE_BUTTON_R2 105
#define AKEYCODE_BUTTON_THUMBL 106
#define AKEYCODE_BUTTON_THUMBR 107
#define AKEYCODE_BUTTON_START 108
#define AKEYCODE_BUTTON_SELECT 109
#define AKEYCODE_BUTTON_MODE 110
#define AINPUT_SOURCE_GAMEPAD 0x0100
#define AINPUT_SOURCE_JOYSTICK 0x0200
#define AINPUT_SOURCE_TOUCHSCREEN 0x1000
#define AINPUT_SOURCE_KEYBOARD 0x0100
int32_t AInputQueue_getEvent(AInputQueue* queue, AInputEvent** outEvent) { (void)queue; (void)outEvent; return 0; }
int32_t AInputQueue_preDispatchEvent(AInputQueue* queue, AInputEvent* event) { (void)queue; (void)event; return 0; }
int32_t AInputQueue_finishEvent(AInputQueue* queue, AInputEvent* event, int handled) { (void)queue; (void)event; (void)handled; return 0; }
int32_t AInputEvent_getType(const AInputEvent* event) { (void)event; return 0; }
int32_t AInputEvent_getSource(const AInputEvent* event) { (void)event; return 0; }
int32_t AKeyEvent_getAction(const AInputEvent* event) { (void)event; return 0; }
int32_t AKeyEvent_getKeyCode(const AInputEvent* event) { (void)event; return 0; }
int32_t AMotionEvent_getAction(const AInputEvent* event) { (void)event; return 0; }
int32_t AMotionEvent_getPointerCount(const AInputEvent* event) { (void)event; return 0; }
float AMotionEvent_getX(const AInputEvent* event, size_t pointerIndex) { (void)event; (void)pointerIndex; return 0; }
float AMotionEvent_getY(const AInputEvent* event, size_t pointerIndex) { (void)event; (void)pointerIndex; return 0; }
int32_t AMotionEvent_getActionButton(const AInputEvent* event) { (void)event; return 0; }
int64_t AInputEvent_getEventTime(const AInputEvent* event) { (void)event; return 0; }
#endif

// Internal input state
struct InputState {
    bool initialized = false;
    GamepadState gamepad = {};
    std::mutex gamepadMutex;

    // Touch/key event queues for polling
    std::queue<KeyEvent> keyEvents;
    std::queue<TouchEvent> touchEvents;
    std::mutex eventMutex;

    // Android input queue (set by native activity)
    AInputQueue* inputQueue = nullptr;

    // Key state tracking
    bool keyStates[256] = {false};

    // Axis state for gamepad
    float leftStickX = 0.0f;
    float leftStickY = 0.0f;
    float rightStickX = 0.0f;
    float rightStickY = 0.0f;
    float leftTrigger = 0.0f;
    float rightTrigger = 0.0f;
};

static InputState g_input;

// Map Android key code to XInput button
static XInputButton android_key_to_xinput(int32_t androidKey) {
    switch (androidKey) {
        case AKEYCODE_DPAD_UP: return XInputButton::UP;
        case AKEYCODE_DPAD_DOWN: return XInputButton::DOWN;
        case AKEYCODE_DPAD_LEFT: return XInputButton::LEFT;
        case AKEYCODE_DPAD_RIGHT: return XInputButton::RIGHT;
        case AKEYCODE_BUTTON_START: return XInputButton::START;
        case AKEYCODE_BUTTON_SELECT: return XInputButton::BACK;
        case AKEYCODE_BUTTON_A: return XInputButton::A;
        case AKEYCODE_BUTTON_B: return XInputButton::B;
        case AKEYCODE_BUTTON_X: return XInputButton::X;
        case AKEYCODE_BUTTON_Y: return XInputButton::Y;
        case AKEYCODE_BUTTON_L1: return XInputButton::LEFT_SHOULDER;
        case AKEYCODE_BUTTON_R1: return XInputButton::RIGHT_SHOULDER;
        case AKEYCODE_BUTTON_L2: return XInputButton::LEFT_SHOULDER;  // Trigger as shoulder
        case AKEYCODE_BUTTON_R2: return XInputButton::RIGHT_SHOULDER;
        case AKEYCODE_BUTTON_THUMBL: return XInputButton::LEFT_THUMB_BUTTON;
        case AKEYCODE_BUTTON_THUMBR: return XInputButton::RIGHT_THUMB_BUTTON;
        case AKEYCODE_BUTTON_MODE: return XInputButton::GUIDE;
        default: return static_cast<XInputButton>(0);
    }
}

// Map Android motion event axis to gamepad axis
static void process_motion_event(const AInputEvent* event) {
    int32_t action = AMotionEvent_getAction(event);
    int32_t actionMasked = action & 0xFF;
    (void)actionMasked;
    size_t pointerIndex = (action >> 8) & 0xFF;
    (void)pointerIndex;
    int32_t pointerCount = AMotionEvent_getPointerCount(event);

    // For gamepad-like devices, process axis
    int32_t source = AInputEvent_getSource(event);
    if (source & (AINPUT_SOURCE_GAMEPAD | AINPUT_SOURCE_JOYSTICK)) {
        std::lock_guard<std::mutex> lock(g_input.gamepadMutex);

        // Standard gamepad axes (may vary by device)
        // AXIS_X, AXIS_Y = left stick
        // AXIS_Z, AXIS_RZ = right stick
        // AXIS_LTRIGGER, AXIS_RTRIGGER = triggers
        // This is simplified - real implementation would use AMotionEvent_getAxisValue

        for (size_t i = 0; i < static_cast<size_t>(pointerCount); ++i) {
            float x = AMotionEvent_getX(event, i);
            float y = AMotionEvent_getY(event, i);

            // Normalize to -1.0 to 1.0 range (assuming 0-1 input)
            x = x * 2.0f - 1.0f;
            y = y * 2.0f - 1.0f;

            if (i == 0) {
                g_input.leftStickX = x;
                g_input.leftStickY = -y;  // Invert Y
            } else if (i == 1) {
                g_input.rightStickX = x;
                g_input.rightStickY = -y;
            }
        }
    }
}

// Process a single input event
static void process_input_event(AInputEvent* event) {
    int32_t type = AInputEvent_getType(event);

    if (type == AINPUT_EVENT_TYPE_KEY) {
        int32_t action = AKeyEvent_getAction(event);
        int32_t keyCode = AKeyEvent_getKeyCode(event);
        bool pressed = (action == AKEY_EVENT_ACTION_DOWN);

        // Track key state
        if (keyCode >= 0 && keyCode < 256) {
            g_input.keyStates[keyCode] = pressed;
        }

        // Queue key event
        KeyEvent ke;
        ke.key = static_cast<AndroidKey>(keyCode);
        ke.pressed = pressed;
        ke.timestamp = AInputEvent_getEventTime(event);

        {
            std::lock_guard<std::mutex> lock(g_input.eventMutex);
            g_input.keyEvents.push(ke);
        }

        // Update gamepad state for gamepad buttons
        XInputButton xbtn = android_key_to_xinput(keyCode);
        if (static_cast<uint16_t>(xbtn) != 0) {
            std::lock_guard<std::mutex> lock(g_input.gamepadMutex);
            uint16_t btnMask = static_cast<uint16_t>(xbtn);
            if (pressed) {
                g_input.gamepad.buttons |= btnMask;
            } else {
                g_input.gamepad.buttons &= ~btnMask;
            }

            // Update boolean fields for convenience
            if (xbtn == XInputButton::A) g_input.gamepad.a = pressed;
            else if (xbtn == XInputButton::B) g_input.gamepad.b = pressed;
            else if (xbtn == XInputButton::X) g_input.gamepad.x = pressed;
            else if (xbtn == XInputButton::Y) g_input.gamepad.y = pressed;
            else if (xbtn == XInputButton::UP) g_input.gamepad.up = pressed;
            else if (xbtn == XInputButton::DOWN) g_input.gamepad.down = pressed;
            else if (xbtn == XInputButton::LEFT) g_input.gamepad.left = pressed;
            else if (xbtn == XInputButton::RIGHT) g_input.gamepad.right = pressed;
            else if (xbtn == XInputButton::START) g_input.gamepad.start = pressed;
            else if (xbtn == XInputButton::BACK) g_input.gamepad.back = pressed;
            else if (xbtn == XInputButton::LEFT_SHOULDER) g_input.gamepad.left_shoulder = pressed;
            else if (xbtn == XInputButton::RIGHT_SHOULDER) g_input.gamepad.right_shoulder = pressed;
        }

    } else if (type == AINPUT_EVENT_TYPE_MOTION) {
        // Touch or motion event
        int32_t source = AInputEvent_getSource(event);

        if (source & AINPUT_SOURCE_TOUCHSCREEN) {
            int32_t action = AMotionEvent_getAction(event);
            int32_t actionMasked = action & 0xFF;
            size_t pointerIndex = (action >> 8) & 0xFF;

            TouchEvent te;
            te.x = static_cast<int>(AMotionEvent_getX(event, pointerIndex));
            te.y = static_cast<int>(AMotionEvent_getY(event, pointerIndex));
            te.timestamp = AInputEvent_getEventTime(event);

            if (actionMasked == AMOTION_EVENT_ACTION_DOWN ||
                actionMasked == AMOTION_EVENT_ACTION_POINTER_DOWN) {
                te.action = 1;  // down
            } else if (actionMasked == AMOTION_EVENT_ACTION_UP ||
                       actionMasked == AMOTION_EVENT_ACTION_POINTER_UP) {
                te.action = 0;  // up
            } else {
                te.action = 2;  // move
            }

            {
                std::lock_guard<std::mutex> lock(g_input.eventMutex);
                g_input.touchEvents.push(te);
            }
        } else if (source & (AINPUT_SOURCE_GAMEPAD | AINPUT_SOURCE_JOYSTICK)) {
            process_motion_event(event);
        }
    }
}

// Initialize input subsystem
extern "C" bool input_init(void) {
    if (g_input.initialized) {
        return true;
    }

    std::memset(&g_input, 0, sizeof(g_input));
    g_input.initialized = true;
    return true;
}

// Shutdown input subsystem
extern "C" void input_shutdown(void) {
    if (!g_input.initialized) {
        return;
    }

    // Clear queues
    {
        std::lock_guard<std::mutex> lock(g_input.eventMutex);
        while (!g_input.keyEvents.empty()) g_input.keyEvents.pop();
        while (!g_input.touchEvents.empty()) g_input.touchEvents.pop();
    }

    g_input.initialized = false;
}

// Set the Android input queue (called from native activity)
extern "C" void input_set_input_queue(AInputQueue* queue) {
    g_input.inputQueue = queue;
}

// Poll and process pending input events
extern "C" void input_poll_events(void) {
    if (!g_input.initialized || !g_input.inputQueue) {
        return;
    }

    AInputEvent* event = nullptr;
    while (AInputQueue_getEvent(g_input.inputQueue, &event) >= 0) {
        if (event) {
            process_input_event(event);
            AInputQueue_finishEvent(g_input.inputQueue, event, 1);
        }
    }
}

// Process XInput state and map to Android input
extern "C" void input_process_state(const XInputState& state) {
    if (!g_input.initialized) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_input.gamepadMutex);

    // Update gamepad from XInput state (called by game)
    g_input.gamepad.buttons = state.buttons;
    g_input.leftTrigger = state.fLeftTrigger;
    g_input.rightTrigger = state.fRightTrigger;
    g_input.leftStickX = state.sThumbLX;
    g_input.leftStickY = state.sThumbLY;
    g_input.rightStickX = state.sThumbRX;
    g_input.rightStickY = state.sThumbRY;

    // Update boolean fields
    g_input.gamepad.up = (state.buttons & static_cast<uint16_t>(XInputButton::UP)) != 0;
    g_input.gamepad.down = (state.buttons & static_cast<uint16_t>(XInputButton::DOWN)) != 0;
    g_input.gamepad.left = (state.buttons & static_cast<uint16_t>(XInputButton::LEFT)) != 0;
    g_input.gamepad.right = (state.buttons & static_cast<uint16_t>(XInputButton::RIGHT)) != 0;
    g_input.gamepad.start = (state.buttons & static_cast<uint16_t>(XInputButton::START)) != 0;
    g_input.gamepad.back = (state.buttons & static_cast<uint16_t>(XInputButton::BACK)) != 0;
    g_input.gamepad.left_shoulder = (state.buttons & static_cast<uint16_t>(XInputButton::LEFT_SHOULDER)) != 0;
    g_input.gamepad.right_shoulder = (state.buttons & static_cast<uint16_t>(XInputButton::RIGHT_SHOULDER)) != 0;
    g_input.gamepad.a = (state.buttons & static_cast<uint16_t>(XInputButton::A)) != 0;
    g_input.gamepad.b = (state.buttons & static_cast<uint16_t>(XInputButton::B)) != 0;
    g_input.gamepad.x = (state.buttons & static_cast<uint16_t>(XInputButton::X)) != 0;
    g_input.gamepad.y = (state.buttons & static_cast<uint16_t>(XInputButton::Y)) != 0;

    g_input.gamepad.left_trigger = state.fLeftTrigger;
    g_input.gamepad.right_trigger = state.fRightTrigger;
    g_input.gamepad.left_stick_x = state.sThumbLX;
    g_input.gamepad.left_stick_y = state.sThumbLY;
    g_input.gamepad.right_stick_x = state.sThumbRX;
    g_input.gamepad.right_stick_y = state.sThumbRY;
}

// Handle key down event
extern "C" void input_key_down(AndroidKey key) {
    if (!g_input.initialized) {
        return;
    }

    int32_t keyCode = static_cast<int32_t>(key);
    if (keyCode >= 0 && keyCode < 256) {
        g_input.keyStates[keyCode] = true;
    }

    KeyEvent ke;
    ke.key = key;
    ke.pressed = true;
    ke.timestamp = 0;  // Would use actual timestamp

    {
        std::lock_guard<std::mutex> lock(g_input.eventMutex);
        g_input.keyEvents.push(ke);
    }

    // Update gamepad if applicable
    XInputButton xbtn = android_key_to_xinput(keyCode);
    if (static_cast<uint16_t>(xbtn) != 0) {
        std::lock_guard<std::mutex> lock(g_input.gamepadMutex);
        uint16_t btnMask = static_cast<uint16_t>(xbtn);
        g_input.gamepad.buttons |= btnMask;
    }
}

// Handle key up event
extern "C" void input_key_up(AndroidKey key) {
    if (!g_input.initialized) {
        return;
    }

    int32_t keyCode = static_cast<int32_t>(key);
    if (keyCode >= 0 && keyCode < 256) {
        g_input.keyStates[keyCode] = false;
    }

    KeyEvent ke;
    ke.key = key;
    ke.pressed = false;
    ke.timestamp = 0;

    {
        std::lock_guard<std::mutex> lock(g_input.eventMutex);
        g_input.keyEvents.push(ke);
    }

    // Update gamepad if applicable
    XInputButton xbtn = android_key_to_xinput(keyCode);
    if (static_cast<uint16_t>(xbtn) != 0) {
        std::lock_guard<std::mutex> lock(g_input.gamepadMutex);
        uint16_t btnMask = static_cast<uint16_t>(xbtn);
        g_input.gamepad.buttons &= ~btnMask;
    }
}

// Handle touch event
extern "C" void input_touch_event(const TouchEvent& event) {
    if (!g_input.initialized) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_input.eventMutex);
    g_input.touchEvents.push(event);
}

// Get current gamepad state
extern "C" GamepadState input_get_gamepad_state(void) {
    std::lock_guard<std::mutex> lock(g_input.gamepadMutex);
    return g_input.gamepad;
}

// Check if a key is currently pressed
extern "C" bool input_is_key_pressed(AndroidKey key) {
    int32_t keyCode = static_cast<int32_t>(key);
    if (keyCode >= 0 && keyCode < 256) {
        return g_input.keyStates[keyCode];
    }
    return false;
}

// Get pending key events (for game to poll)
extern "C" uint32_t input_get_key_events(KeyEvent* outEvents, uint32_t maxEvents) {
    std::lock_guard<std::mutex> lock(g_input.eventMutex);
    uint32_t count = 0;
    while (!g_input.keyEvents.empty() && count < maxEvents) {
        outEvents[count] = g_input.keyEvents.front();
        g_input.keyEvents.pop();
        ++count;
    }
    return count;
}

// Get pending touch events (for game to poll)
extern "C" uint32_t input_get_touch_events(TouchEvent* outEvents, uint32_t maxEvents) {
    std::lock_guard<std::mutex> lock(g_input.eventMutex);
    uint32_t count = 0;
    while (!g_input.touchEvents.empty() && count < maxEvents) {
        outEvents[count] = g_input.touchEvents.front();
        g_input.touchEvents.pop();
        ++count;
    }
    return count;
}

// --- XInput Interface Stubs ---
// These would be called by lifted game code that uses XInput

extern "C" uint32_t xinput_get_state(uint32_t userIndex, XInputState* state) {
    if (!g_input.initialized || userIndex > 3) {
        return 1;  // ERROR_DEVICE_NOT_CONNECTED
    }

    std::lock_guard<std::mutex> lock(g_input.gamepadMutex);

    state->dwPacketNumber = 0;  // Would increment on each update
    state->buttons = g_input.gamepad.buttons;
    state->fLeftTrigger = g_input.leftTrigger;
    state->fRightTrigger = g_input.rightTrigger;
    state->sThumbLX = g_input.leftStickX;
    state->sThumbLY = g_input.leftStickY;
    state->sThumbRX = g_input.rightStickX;
    state->sThumbRY = g_input.rightStickY;

    return 0;  // ERROR_SUCCESS
}

extern "C" uint32_t xinput_set_state(uint32_t userIndex, void* vibration) {
    // Vibration not supported on most Android devices
    (void)userIndex;
    (void)vibration;
    return 0;
}

extern "C" uint32_t xinput_get_capabilities(uint32_t userIndex, uint32_t flags, void* capabilities) {
    (void)flags; (void)capabilities;
    if (!g_input.initialized || userIndex > 3) {
        return 1;
    }
    // Fill capabilities structure
    return 0;
}

extern "C" void xinput_enable(uint32_t enable) {
    // Enable/disable XInput
    (void)enable;
}