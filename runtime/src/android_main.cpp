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

// Subsystem headers
#include "graphics/graphics_types.h"
#include "audio/audio_types.h"
#include "input/input_types.h"
#include "video/video_types.h"

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

extern "C" bool audio_init(uint32_t sampleRate, AudioChannel channels, AudioFormat format);
extern "C" void audio_shutdown(void);

extern "C" bool input_init(void);
extern "C" void input_shutdown(void);
extern "C" void input_set_input_queue(AInputQueue* queue);
extern "C" void input_poll_events(void);

extern "C" bool video_init(uint32_t width, uint32_t height);
extern "C" void video_shutdown(void);
extern "C" void video_set_native_window(void* window);

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
    while (true) {
        int events;
        int fd;
        int ident;
        int timeout = g_android_state.initialized ? 16 : -1;
        
        ident = ALooper_pollOnce(timeout, &fd, &events, nullptr);
        
        if (ident >= 0) {
            // There's an event to process
            // The glue callbacks will be invoked automatically by the looper
        }
        
        // Process input events if initialized
        if (g_android_state.initialized) {
            input_poll_events();
        }
        
        // Check if we should exit - use a global flag instead of destroyRequested
        // which is not available in newer NDKs
        // We'll rely on onDestroy callback to set a shutdown flag
        // For now, just loop
    }
    
    LOGI("android_main exiting");
}

// JNI bridge functions for Java/Kotlin integration
extern "C" {

// Called from Java to initialize native subsystems
JNIEXPORT void JNICALL
Java_com_rottr_recomp_NativeBridge_nativeInit(JNIEnv* env, jclass clazz, jint width, jint height) {
    (void)env; (void)clazz;
    LOGI("JNI nativeInit: %dx%d", width, height);
    g_android_state.screenWidth = width;
    g_android_state.screenHeight = height;
    
    if (!g_android_state.initialized) {
        video_init(width, height);
        graphics_init(width, height, nullptr);
        audio_init(48000, AudioChannel::STEREO, AudioFormat::FLOAT);
        input_init();
        g_android_state.initialized = true;
    }
}

// Called from Java to set the native window
JNIEXPORT void JNICALL
Java_com_rottr_recomp_NativeBridge_nativeSetSurface(JNIEnv* env, jclass clazz, jobject surface) {
    (void)clazz;
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    LOGI("JNI nativeSetSurface: %p", window);
    
    if (window) {
        onNativeWindowCreated(nullptr, window);
    }
}

// Called from Java to release the native window
JNIEXPORT void JNICALL
Java_com_rottr_recomp_NativeBridge_nativeReleaseSurface(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    LOGI("JNI nativeReleaseSurface");
    if (g_android_state.window) {
        onNativeWindowDestroyed(nullptr, g_android_state.window);
        ANativeWindow_release(g_android_state.window);
        g_android_state.window = nullptr;
    }
}

// Called from Java to set the input queue
JNIEXPORT void JNICALL
Java_com_rottr_recomp_NativeBridge_nativeSetInputQueue(JNIEnv* env, jclass clazz, jobject inputQueue) {
    (void)env; (void)clazz; (void)inputQueue;
    // Note: AInputQueue from Java requires NDK glue code
    // This is a placeholder for the actual implementation
    LOGI("JNI nativeSetInputQueue");
}

// Called from Java to shutdown native subsystems
JNIEXPORT void JNICALL
Java_com_rottr_recomp_NativeBridge_nativeShutdown(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    LOGI("JNI nativeShutdown");
    if (g_android_state.initialized) {
        graphics_shutdown();
        audio_shutdown();
        input_shutdown();
        video_shutdown();
        g_android_state.initialized = false;
    }
}

} // extern "C"