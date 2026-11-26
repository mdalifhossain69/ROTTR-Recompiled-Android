// touch_controls.cpp
// Touch-to-gamepad mapping for on-screen controls
// Provides virtual joystick and button overlays

#include "input/input_types.h"
#include <android/log.h>
#include <cstdint>
#include <cmath>
#include <algorithm>

#define LOG_TAG "ROTTR-TouchControls"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Touch control layout configuration
struct TouchControlLayout {
    // Left stick (movement)
    struct {
        float center_x;      // Normalized 0-1
        float center_y;
        float radius_inner;  // Dead zone
        float radius_outer;  // Max displacement
        bool active = false;
        int32_t pointer_id = -1;
        float current_x = 0;
        float current_y = 0;
    } left_stick;

    // Right stick (camera)
    struct {
        float center_x;
        float center_y;
        float radius_inner;
        float radius_outer;
        bool active = false;
        int32_t pointer_id = -1;
        float current_x = 0;
        float current_y = 0;
    } right_stick;

    // Buttons (A, B, X, Y, L1, R1, Start, Back)
    struct Button {
        float center_x;
        float center_y;
        float radius;
        XInputButton button;
        bool pressed = false;
        int32_t pointer_id = -1;
    } buttons[8];

    // D-pad (optional, can use left stick)
    struct {
        float center_x;
        float center_y;
        float size;
        bool active = false;
        int32_t pointer_id = -1;
        float current_x = 0;
        float current_y = 0;
    } dpad;

    int screen_width = 0;
    int screen_height = 0;
};

static TouchControlLayout g_layout;
static bool g_layout_initialized = false;

// Initialize touch control layout for screen size
void touch_controls_init_layout(int width, int height) {
    g_layout.screen_width = width;
    g_layout.screen_height = height;

    // Use normalized coordinates (0-1) for resolution independence
    // Left stick - bottom left
    g_layout.left_stick.center_x = 0.15f;
    g_layout.left_stick.center_y = 0.75f;
    g_layout.left_stick.radius_inner = 0.03f;
    g_layout.left_stick.radius_outer = 0.12f;

    // Right stick - bottom right
    g_layout.right_stick.center_x = 0.85f;
    g_layout.right_stick.center_y = 0.75f;
    g_layout.right_stick.radius_inner = 0.03f;
    g_layout.right_stick.radius_outer = 0.12f;

    // Buttons - right side, above right stick
    float button_y = 0.55f;
    float button_spacing = 0.08f;
    float button_x = 0.85f;

    g_layout.buttons[0] = {button_x, button_y + button_spacing * 0, 0.05f, XInputButton::A, false, -1};
    g_layout.buttons[1] = {button_x + button_spacing, button_y + button_spacing * 0.5f, 0.05f, XInputButton::B, false, -1};
    g_layout.buttons[2] = {button_x, button_y - button_spacing * 0.5f, 0.05f, XInputButton::X, false, -1};
    g_layout.buttons[3] = {button_x - button_spacing, button_y + button_spacing * 0.5f, 0.05f, XInputButton::Y, false, -1};

    // Shoulder buttons - top right
    g_layout.buttons[4] = {0.75f, 0.15f, 0.045f, XInputButton::LEFT_SHOULDER, false, -1};
    g_layout.buttons[5] = {0.90f, 0.15f, 0.045f, XInputButton::RIGHT_SHOULDER, false, -1};

    // Start/Back - top center
    g_layout.buttons[6] = {0.45f, 0.10f, 0.04f, XInputButton::START, false, -1};
    g_layout.buttons[7] = {0.55f, 0.10f, 0.04f, XInputButton::BACK, false, -1};

    // D-pad - top left (optional)
    g_layout.dpad.center_x = 0.15f;
    g_layout.dpad.center_y = 0.20f;
    g_layout.dpad.size = 0.08f;

    g_layout_initialized = true;
    LOGI("Touch control layout initialized: %dx%d", width, height);
}

// Convert screen coordinates to normalized (0-1)
static void screen_to_normalized(int x, int y, float* out_x, float* out_y) {
    if (g_layout.screen_width > 0 && g_layout.screen_height > 0) {
        *out_x = static_cast<float>(x) / g_layout.screen_width;
        *out_y = static_cast<float>(y) / g_layout.screen_height;
    } else {
        *out_x = 0;
        *out_y = 0;
    }
}

// Check if point is within circle
static bool point_in_circle(float px, float py, float cx, float cy, float radius) {
    float dx = px - cx;
    float dy = py - cy;
    return (dx * dx + dy * dy) <= (radius * radius);
}

// Process touch down event
void touch_controls_on_touch_down(int pointer_id, int x, int y) {
    if (!g_layout_initialized) return;

    float nx, ny;
    screen_to_normalized(x, y, &nx, &ny);

    // Check left stick
    if (point_in_circle(nx, ny, g_layout.left_stick.center_x, g_layout.left_stick.center_y,
                        g_layout.left_stick.radius_outer)) {
        g_layout.left_stick.active = true;
        g_layout.left_stick.pointer_id = pointer_id;
        g_layout.left_stick.current_x = nx;
        g_layout.left_stick.current_y = ny;
        return;
    }

    // Check right stick
    if (point_in_circle(nx, ny, g_layout.right_stick.center_x, g_layout.right_stick.center_y,
                        g_layout.right_stick.radius_outer)) {
        g_layout.right_stick.active = true;
        g_layout.right_stick.pointer_id = pointer_id;
        g_layout.right_stick.current_x = nx;
        g_layout.right_stick.current_y = ny;
        return;
    }

    // Check buttons
    for (int i = 0; i < 8; ++i) {
        if (point_in_circle(nx, ny, g_layout.buttons[i].center_x, g_layout.buttons[i].center_y,
                            g_layout.buttons[i].radius)) {
            g_layout.buttons[i].pressed = true;
            g_layout.buttons[i].pointer_id = pointer_id;
            return;
        }
    }

    // Check D-pad
    if (point_in_circle(nx, ny, g_layout.dpad.center_x, g_layout.dpad.center_y,
                        g_layout.dpad.size)) {
        g_layout.dpad.active = true;
        g_layout.dpad.pointer_id = pointer_id;
        g_layout.dpad.current_x = nx;
        g_layout.dpad.current_y = ny;
        return;
    }
}

// Process touch move event
void touch_controls_on_touch_move(int pointer_id, int x, int y) {
    if (!g_layout_initialized) return;

    float nx, ny;
    screen_to_normalized(x, y, &nx, &ny);

    // Update left stick
    if (g_layout.left_stick.active && g_layout.left_stick.pointer_id == pointer_id) {
        float dx = nx - g_layout.left_stick.center_x;
        float dy = ny - g_layout.left_stick.center_y;
        float dist = std::sqrt(dx * dx + dy * dy);

        if (dist > g_layout.left_stick.radius_outer) {
            // Clamp to outer radius
            float factor = g_layout.left_stick.radius_outer / dist;
            dx *= factor;
            dy *= factor;
        }

        // Apply dead zone
        if (dist < g_layout.left_stick.radius_inner) {
            dx = 0;
            dy = 0;
        }

        g_layout.left_stick.current_x = g_layout.left_stick.center_x + dx;
        g_layout.left_stick.current_y = g_layout.left_stick.center_y + dy;
        return;
    }

    // Update right stick
    if (g_layout.right_stick.active && g_layout.right_stick.pointer_id == pointer_id) {
        float dx = nx - g_layout.right_stick.center_x;
        float dy = ny - g_layout.right_stick.center_y;
        float dist = std::sqrt(dx * dx + dy * dy);

        if (dist > g_layout.right_stick.radius_outer) {
            float factor = g_layout.right_stick.radius_outer / dist;
            dx *= factor;
            dy *= factor;
        }

        if (dist < g_layout.right_stick.radius_inner) {
            dx = 0;
            dy = 0;
        }

        g_layout.right_stick.current_x = g_layout.right_stick.center_x + dx;
        g_layout.right_stick.current_y = g_layout.right_stick.center_y + dy;
        return;
    }

    // Update D-pad
    if (g_layout.dpad.active && g_layout.dpad.pointer_id == pointer_id) {
        g_layout.dpad.current_x = nx;
        g_layout.dpad.current_y = ny;
        return;
    }
}

// Process touch up event
void touch_controls_on_touch_up(int pointer_id, int x, int y) {
    if (!g_layout_initialized) return;
    (void)x; (void)y;

    // Release left stick
    if (g_layout.left_stick.active && g_layout.left_stick.pointer_id == pointer_id) {
        g_layout.left_stick.active = false;
        g_layout.left_stick.pointer_id = -1;
        g_layout.left_stick.current_x = g_layout.left_stick.center_x;
        g_layout.left_stick.current_y = g_layout.left_stick.center_y;
        return;
    }

    // Release right stick
    if (g_layout.right_stick.active && g_layout.right_stick.pointer_id == pointer_id) {
        g_layout.right_stick.active = false;
        g_layout.right_stick.pointer_id = -1;
        g_layout.right_stick.current_x = g_layout.right_stick.center_x;
        g_layout.right_stick.current_y = g_layout.right_stick.center_y;
        return;
    }

    // Release buttons
    for (int i = 0; i < 8; ++i) {
        if (g_layout.buttons[i].pressed && g_layout.buttons[i].pointer_id == pointer_id) {
            g_layout.buttons[i].pressed = false;
            g_layout.buttons[i].pointer_id = -1;
            return;
        }
    }

    // Release D-pad
    if (g_layout.dpad.active && g_layout.dpad.pointer_id == pointer_id) {
        g_layout.dpad.active = false;
        g_layout.dpad.pointer_id = -1;
        g_layout.dpad.current_x = g_layout.dpad.center_x;
        g_layout.dpad.current_y = g_layout.dpad.center_y;
        return;
    }
}

// Update gamepad state from touch controls
void touch_controls_update_gamepad(GamepadState* gamepad) {
    if (!g_layout_initialized || !gamepad) return;

    // Left stick
    float lx = (g_layout.left_stick.current_x - g_layout.left_stick.center_x) / g_layout.left_stick.radius_outer;
    float ly = (g_layout.left_stick.current_y - g_layout.left_stick.center_y) / g_layout.left_stick.radius_outer;
    gamepad->left_stick_x = std::clamp(lx, -1.0f, 1.0f);
    gamepad->left_stick_y = std::clamp(-ly, -1.0f, 1.0f);  // Invert Y

    // Right stick
    float rx = (g_layout.right_stick.current_x - g_layout.right_stick.center_x) / g_layout.right_stick.radius_outer;
    float ry = (g_layout.right_stick.current_y - g_layout.right_stick.center_y) / g_layout.right_stick.radius_outer;
    gamepad->right_stick_x = std::clamp(rx, -1.0f, 1.0f);
    gamepad->right_stick_y = std::clamp(-ry, -1.0f, 1.0f);

    // Buttons
    gamepad->a = g_layout.buttons[0].pressed;
    gamepad->b = g_layout.buttons[1].pressed;
    gamepad->x = g_layout.buttons[2].pressed;
    gamepad->y = g_layout.buttons[3].pressed;
    gamepad->left_shoulder = g_layout.buttons[4].pressed;
    gamepad->right_shoulder = g_layout.buttons[5].pressed;
    gamepad->start = g_layout.buttons[6].pressed;
    gamepad->back = g_layout.buttons[7].pressed;

    // D-pad (from D-pad area or left stick cardinal directions)
    if (g_layout.dpad.active) {
        float dx = g_layout.dpad.current_x - g_layout.dpad.center_x;
        float dy = g_layout.dpad.current_y - g_layout.dpad.center_y;

        if (std::abs(dx) > std::abs(dy)) {
            gamepad->left = dx < 0;
            gamepad->right = dx > 0;
        } else {
            gamepad->up = dy < 0;
            gamepad->down = dy > 0;
        }
    } else {
        // Use left stick for D-pad if not using D-pad area
        if (std::abs(lx) > 0.5f || std::abs(ly) > 0.5f) {
            if (std::abs(lx) > std::abs(ly)) {
                gamepad->left = lx < -0.5f;
                gamepad->right = lx > 0.5f;
            } else {
                gamepad->up = ly < -0.5f;
                gamepad->down = ly > 0.5f;
            }
        }
    }

    // Triggers (could map to shoulder buttons or add dedicated trigger areas)
    gamepad->left_trigger = g_layout.buttons[4].pressed ? 1.0f : 0.0f;
    gamepad->right_trigger = g_layout.buttons[5].pressed ? 1.0f : 0.0f;
}

// Get render data for drawing touch control overlays
void touch_controls_get_render_data(TouchControlRenderData* out_data) {
    if (!g_layout_initialized || !out_data) return;

    out_data->circle_count = 0;

    // Left stick base
    out_data->circles[out_data->circle_count++] = {
        g_layout.left_stick.center_x, g_layout.left_stick.center_y,
        g_layout.left_stick.radius_outer, 0x80FFFFFF, false
    };
    out_data->circles[out_data->circle_count++] = {
        g_layout.left_stick.center_x, g_layout.left_stick.center_y,
        g_layout.left_stick.radius_inner, 0x40FFFFFF, true
    };
    // Left stick knob
    out_data->circles[out_data->circle_count++] = {
        g_layout.left_stick.current_x, g_layout.left_stick.current_y,
        g_layout.left_stick.radius_inner, g_layout.left_stick.active ? 0xFFFFFFFF : 0x80FFFFFF, true
    };

    // Right stick base
    out_data->circles[out_data->circle_count++] = {
        g_layout.right_stick.center_x, g_layout.right_stick.center_y,
        g_layout.right_stick.radius_outer, 0x80FFFFFF, false
    };
    out_data->circles[out_data->circle_count++] = {
        g_layout.right_stick.center_x, g_layout.right_stick.center_y,
        g_layout.right_stick.radius_inner, 0x40FFFFFF, true
    };
    // Right stick knob
    out_data->circles[out_data->circle_count++] = {
        g_layout.right_stick.current_x, g_layout.right_stick.current_y,
        g_layout.right_stick.radius_inner, g_layout.right_stick.active ? 0xFFFFFFFF : 0x80FFFFFF, true
    };

    // Buttons
    static const uint32_t button_colors[8] = {
        0xFF00FF00,  // A - Green
        0xFFFF0000,  // B - Red
        0xFF0000FF,  // X - Blue
        0xFFFFFF00,  // Y - Yellow
        0xFFFF8800,  // L1 - Orange
        0xFFFF8800,  // R1 - Orange
        0xFFFFFFFF,  // Start - White
        0xFFFFFFFF,  // Back - White
    };

    for (int i = 0; i < 8; ++i) {
        out_data->circles[out_data->circle_count++] = {
            g_layout.buttons[i].center_x, g_layout.buttons[i].center_y,
            g_layout.buttons[i].radius,
            g_layout.buttons[i].pressed ? button_colors[i] : (button_colors[i] & 0x80FFFFFF),
            true
        };
    }

    // D-pad
    out_data->circles[out_data->circle_count++] = {
        g_layout.dpad.center_x, g_layout.dpad.center_y,
        g_layout.dpad.size, 0x80FFFFFF, g_layout.dpad.active
    };
}

// Public API
extern "C" void touch_controls_init(int width, int height) {
    touch_controls_init_layout(width, height);
}

extern "C" void touch_controls_on_touch_down_c(int pointer_id, int x, int y) {
    touch_controls_on_touch_down(pointer_id, x, y);
}

extern "C" void touch_controls_on_touch_move_c(int pointer_id, int x, int y) {
    touch_controls_on_touch_move(pointer_id, x, y);
}

extern "C" void touch_controls_on_touch_up_c(int pointer_id, int x, int y) {
    touch_controls_on_touch_up(pointer_id, x, y);
}

extern "C" void touch_controls_update_gamepad_c(GamepadState* gamepad) {
    touch_controls_update_gamepad(gamepad);
}

extern "C" void touch_controls_get_render_data_c(void* out_data) {
    touch_controls_get_render_data(static_cast<TouchControlRenderData*>(out_data));
}