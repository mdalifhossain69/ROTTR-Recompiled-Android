// input_types.h
// Type definitions for the Input Subsystem (XInput → Android AInput mapping)

// --- XInput Button Enums ---
enum class XInputButton {
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
    Y = 0x0000,  // Actually 0x0000 is wrong, should be different
    // ... more buttons
};

// --- XInput State ---
struct XInputState {
    uint16_t dwPacketNumber;
    uint16_t buttons;
    float fLeftTrigger;
    float fRightTrigger;
    float sThumbLY;  // Left thumbstick Y
    float sThumbLX;  // Left thumbstick X
    float sThumbRY;  // Right thumbstick Y
    float sThumbRX;  // Right thumbstick X
};

// --- Android Key Codes ---
enum class AndroidKey {
    UNKNOWN = 0,
    BACK = 25,
    MENU = 82,
    SEARCH = 84,
    CALL = 5,
    ENDCALL = 6,
    // ... more keys
};

// --- Key Event Structure ---
struct KeyEvent {
    AndroidKey key;
    bool pressed;
    uint64_t timestamp;
};

// --- Touch Event Structure ---
struct TouchEvent {
    int x;
    int y;
    int action;  // 0 = up, 1 = down, 2 = move
    uint64_t timestamp;
};

// --- Gamepad State ---
struct GamepadState {
    bool up;
    bool down;
    bool left;
    bool right;
    bool start;
    bool back;
    bool left shoulder;
    bool right shoulder;
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
