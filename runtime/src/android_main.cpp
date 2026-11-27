// android_main.cpp
// Android native activity entry point for ROTTR-Recomp
// Uses android_native_app_glue for proper event handling

#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/input.h>
#include <android/looper.h>
#include <android/log.h>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <thread>

// Subsystem headers
#include "graphics/graphics_types.h"
#include "audio/audio_types.h"
#include "input/input_types.h"
#include "video/video_types.h"
#include "performance.h"

#define LOG_TAG "ROTTR-Recomp"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Global state
static struct {
    bool initialized = false;
    ANativeWindow* window = nullptr;
    AInputQueue* inputQueue = nullptr;
    int32_t screenWidth = 0;
    int32_t screenHeight = 0;
} g_android_state;

// Forward declarations from subsystems
extern "C" bool graphics_init(int width, int height, HWND hwnd);
extern "C" void graphics_shutdown(void);
extern "C" bool graphics_present(void);
extern "C" void graphics_set_target_fps(uint32_t fps);
extern "C" void graphics_set_frame_pacing(bool enable);

extern "C" bool audio_init(uint32_t sampleRate, AudioChannel channels, AudioFormat format);
extern "C" void audio_shutdown(void);

extern "C" bool input_init(void);
extern "C" void input_shutdown(void);
extern "C" void input_set_input_queue(AInputQueue* queue);
extern "C" void input_poll_events(void);

extern "C" bool video_init(uint32_t width, uint32_t height);
extern "C" void video_shutdown(void);
extern "C" void video_set_native_window(void* window);

// Touch controls (Phase 6)
extern "C" void touch_controls_init(int width, int height);
extern "C" void touch_controls_on_touch_down_c(int pointer_id, int x, int y);
extern "C" void touch_controls_on_touch_move_c(int pointer_id, int x, int y);
extern "C" void touch_controls_on_touch_up_c(int pointer_id, int x, int y);
extern "C" void touch_controls_update_gamepad_c(GamepadState* gamepad);
extern "C" GamepadState input_get_gamepad_state(void);

// Native activity callbacks
static void onStart(ANativeActivity* activity) {
    (void)activity;
    LOGI("onStart");
}

static void onResume(ANativeActivity* activity) {
    (void)activity;
    LOGI("onResume");
}

static void onPause(ANativeActivity* activity) {
    (void)activity;
    LOGI("onPause");
}

static void onStop(ANativeActivity* activity) {
    (void)activity;
    LOGI("onStop");
}

static void onDestroy(ANativeActivity* activity) {
    (void)activity;
    LOGI("onDestroy");
    
    // Shutdown all subsystems
    if (g_android_state.initialized) {
        graphics_shutdown();
        audio_shutdown();
        input_shutdown();
        video_shutdown();
        perf_shutdown();
        g_android_state.initialized = false;
    }
}

static void onWindowFocusChanged(ANativeActivity* activity, int hasFocus) {
    (void)activity;
    (void)hasFocus;
    LOGI("onWindowFocusChanged: %d", hasFocus);
}

static void onNativeWindowCreated(ANativeActivity* activity, ANativeWindow* window) {
    (void)activity;
    LOGI("onNativeWindowCreated: %p", window);
    g_android_state.window = window;
    
    if (!g_android_state.initialized) {
        // Initialize subsystems on first window creation
        int32_t width = ANativeWindow_getWidth(window);
        int32_t height = ANativeWindow_getHeight(window);
        g_android_state.screenWidth = width;
        g_android_state.screenHeight = height;
        
        LOGI("Initializing subsystems: %dx%d", width, height);
        
        // Initialize video first (sets up conversion buffers)
        if (!video_init(width, height)) {
            LOGE("video_init failed");
            return;
        }
        
        // Initialize graphics with window handle
        if (!graphics_init(width, height, reinterpret_cast<HWND>(window))) {
            LOGE("graphics_init failed");
            video_shutdown();
            return;
        }
        
        // Initialize audio
        if (!audio_init(48000, AudioChannel::STEREO, AudioFormat::FLOAT)) {
            LOGE("audio_init failed");
            // Continue anyway, audio is not critical
        }
        
        // Initialize input
        if (!input_init()) {
            LOGE("input_init failed");
        }
        
        // Initialize touch controls
        touch_controls_init(width, height);
        
        // Initialize performance monitoring
        perf_init();
        
        // Wire input queue
        if (g_android_state.inputQueue) {
            input_set_input_queue(g_android_state.inputQueue);
        }
        
        // Wire native window to video subsystem
        video_set_native_window(window);
        
        g_android_state.initialized = true;
        LOGI("All subsystems initialized successfully");
    } else {
        // Window recreated, update graphics/video
        int32_t width = ANativeWindow_getWidth(window);
        int32_t height = ANativeWindow_getHeight(window);
        
        if (width != g_android_state.screenWidth || height != g_android_state.screenHeight) {
            g_android_state.screenWidth = width;
            g_android_state.screenHeight = height;
            LOGI("Window resized: %dx%d", width, height);
            // TODO: Handle resize in graphics/video
        }
    }
}

static void onNativeWindowDestroyed(ANativeActivity* activity, ANativeWindow* window) {
    (void)activity;
    LOGI("onNativeWindowDestroyed: %p", window);
    g_android_state.window = nullptr;
    
    // Note: We don't shutdown here because the window might be recreated
    // Full shutdown happens in onDestroy
}

static void onInputQueueCreated(ANativeActivity* activity, AInputQueue* queue) {
    (void)activity;
    LOGI("onInputQueueCreated: %p", queue);
    g_android_state.inputQueue = queue;
    
    if (g_android_state.initialized) {
        input_set_input_queue(queue);
    }
}

static void onInputQueueDestroyed(ANativeActivity* activity, AInputQueue* queue) {
    (void)activity;
    (void)queue;
    LOGI("onInputQueueDestroyed");
    g_android_state.inputQueue = nullptr;
}

static void onConfigurationChanged(ANativeActivity* activity) {
    (void)activity;
    LOGI("onConfigurationChanged");
}

static void onLowMemory(ANativeActivity* activity) {
    (void)activity;
    LOGI("onLowMemory");
}

// Main entry point - called by Android runtime
extern "C" void android_main(ANativeActivity* activity) {
    LOGI("android_main starting");
    
    // Set up native activity callbacks
    activity->callbacks->onStart = onStart;
    activity->callbacks->onResume = onResume;
    activity->callbacks->onPause = onPause;
    activity->callbacks->onStop = onStop;
    activity->callbacks->onDestroy = onDestroy;
    activity->callbacks->onWindowFocusChanged = onWindowFocusChanged;
    activity->callbacks->onNativeWindowCreated = onNativeWindowCreated;
    activity->callbacks->onNativeWindowDestroyed = onNativeWindowDestroyed;
    activity->callbacks->onInputQueueCreated = onInputQueueCreated;
    activity->callbacks->onInputQueueDestroyed = onInputQueueDestroyed;
    activity->callbacks->onConfigurationChanged = onConfigurationChanged;
    activity->callbacks->onLowMemory = onLowMemory;
    
    // Set user data pointer
    activity->instance = &g_android_state;
    
    // Keep the main thread alive - process events using ALooper_pollOnce
    // (ALooper_pollAll is deprecated)
    // Frame pacing: target 60 FPS (16.67ms per frame)
    const int TARGET_FRAME_TIME_MS = 16;
    std::chrono::steady_clock::time_point lastFrameTime = std::chrono::steady_clock::now();
    
    while (true) {
        int events;
        int fd;
        int ident;
        int timeout = g_android_state.initialized ? TARGET_FRAME_TIME_MS : -1;
        
        ident = ALooper_pollOnce(timeout, &fd, &events, nullptr);
        
        if (ident >= 0) {
            // There's an event to process
            // The glue callbacks will be invoked automatically by the looper
        }
        
        // Process input events if initialized
        if (g_android_state.initialized) {
            auto frameStart = std::chrono::steady_clock::now();
            
            input_poll_events();
            
            // Update touch controls gamepad state
            GamepadState gamepad = input_get_gamepad_state();
            touch_controls_update_gamepad_c(&gamepad);
            
            // Present frame (includes frame pacing)
            graphics_present();
            
            // Record frame time for performance monitoring
            auto frameEnd = std::chrono::steady_clock::now();
            double frameTimeMs = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
            perf_record_frame(frameTimeMs);
        }
        
        // Frame pacing - sleep to maintain target frame rate
        if (g_android_state.initialized) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFrameTime).count();
            
            if (elapsed < TARGET_FRAME_TIME_MS) {
                int sleepMs = TARGET_FRAME_TIME_MS - static_cast<int>(elapsed);
                // Small sleep to reduce CPU usage
                if (sleepMs > 2) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs - 1));
                }
                // Spin for remaining time
                while (std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - lastFrameTime).count() < TARGET_FRAME_TIME_MS) {
                    // busy wait
                }
            }
            lastFrameTime = std::chrono::steady_clock::now();
        }
        
        // Check if we should exit - use a global flag instead of destroyRequested
        // which is not available in newer NDKs
        // We'll rely on onDestroy callback to set a shutdown flag
        // For now, just loop
    }
    
    LOGI("android_main exiting");
}
