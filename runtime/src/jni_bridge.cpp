// jni_bridge.cpp
// JNI exports for com.rottr.recomp.NativeBridge.
// Provides the nativeInit / nativeSetSurface / ... entry points called from the
// Java/Kotlin side of the ROTTR-Recomp launcher Activity.
//
// This mirrors the subsystem initialization performed in android_main.cpp
// (NativeActivity path) so that a regular Activity + TextureView launcher works.

#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <cstdint>

#define LOG_TAG "ROTTR-Recomp-JNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Subsystem declarations (implemented across runtime/src)
extern "C" bool graphics_init(int width, int height, void* window);
extern "C" void graphics_shutdown(void);
extern "C" void graphics_set_target_fps(unsigned int fps);
extern "C" void graphics_set_frame_pacing(bool enable);

extern "C" bool audio_init(unsigned int sampleRate, int channels, int format);
extern "C" void audio_shutdown(void);

extern "C" bool input_init(void);
extern "C" void input_shutdown(void);
extern "C" void input_set_input_queue(void* queue);

extern "C" bool video_init(unsigned int width, unsigned int height);
extern "C" void video_shutdown(void);
extern "C" void video_set_native_window(void* window);

extern "C" void touch_controls_init(int width, int height);
extern "C" void touch_controls_update_gamepad_c(void* gamepad);

extern "C" void perf_init(void);
extern "C" void perf_shutdown(void);

// Audio channel / format constants must match audio_types.h
static const int AUDIO_CHANNEL_STEREO = 0;
static const int AUDIO_FORMAT_FLOAT = 0;

static struct {
    bool coreInitialized = false;
    bool windowInitialized = false;
    ANativeWindow* window = nullptr;
    int32_t screenWidth = 0;
    int32_t screenHeight = 0;
} g_state;

static void initialize_core(int width, int height) {
    if (g_state.coreInitialized) {
        return;
    }
    g_state.screenWidth = width;
    g_state.screenHeight = height;

    LOGI("JNI: initializing core subsystems %dx%d", width, height);

    if (!audio_init(48000, AUDIO_CHANNEL_STEREO, AUDIO_FORMAT_FLOAT)) {
        LOGE("JNI: audio_init failed (non-fatal)");
    }
    if (!input_init()) {
        LOGE("JNI: input_init failed");
    }
    touch_controls_init(width, height);
    perf_init();

    g_state.coreInitialized = true;
}

static void shutdown_window(void) {
    if (!g_state.windowInitialized) {
        return;
    }
    graphics_shutdown();
    video_shutdown();
    g_state.windowInitialized = false;
}

static void initialize_window(int width, int height, ANativeWindow* window) {
    if (window == nullptr) {
        return;
    }
    if (g_state.windowInitialized && g_state.window == window) {
        return;
    }

    shutdown_window();
    if (g_state.window != nullptr) {
        ANativeWindow_release(g_state.window);
        g_state.window = nullptr;
    }

    g_state.window = window;
    g_state.screenWidth = width;
    g_state.screenHeight = height;

    LOGI("JNI: initializing window subsystems %dx%d", width, height);

    video_init(static_cast<unsigned int>(width), static_cast<unsigned int>(height));
    video_set_native_window(window);
    if (!graphics_init(width, height, window)) {
        LOGE("JNI: graphics_init failed");
    }

    g_state.windowInitialized = true;
}

static void release_window(void) {
    shutdown_window();
    if (g_state.window != nullptr) {
        ANativeWindow_release(g_state.window);
        g_state.window = nullptr;
    }
}

extern "C" {

/*
 * Class:     com_rottr_recomp_NativeBridge
 * Method:    nativeInit
 * Signature: (II)V
 */
JNIEXPORT void JNICALL
Java_com_rottr_recomp_NativeBridge_nativeInit(JNIEnv* env, jclass clazz, jint width, jint height) {
    (void)env;
    (void)clazz;
    LOGI("JNI: nativeInit %dx%d", (int)width, (int)height);
    initialize_core((int)width, (int)height);
}

/*
 * Class:     com_rottr_recomp_NativeBridge
 * Method:    nativeSetSurface
 * Signature: (Landroid/view/Surface;)V
 */
JNIEXPORT void JNICALL
Java_com_rottr_recomp_NativeBridge_nativeSetSurface(JNIEnv* env, jclass clazz, jobject surface) {
    (void)clazz;
    ANativeWindow* window = nullptr;
    if (surface != nullptr) {
        window = ANativeWindow_fromSurface(env, surface);
        if (window == nullptr) {
            LOGE("JNI: ANativeWindow_fromSurface returned null");
        }
    }
    LOGI("JNI: nativeSetSurface window=%p", (void*)window);
    initialize_core(g_state.screenWidth, g_state.screenHeight);
    initialize_window(g_state.screenWidth, g_state.screenHeight, window);
}

/*
 * Class:     com_rottr_recomp_NativeBridge
 * Method:    nativeReleaseSurface
 * Signature: ()V
 */
JNIEXPORT void JNICALL
Java_com_rottr_recomp_NativeBridge_nativeReleaseSurface(JNIEnv* env, jclass clazz) {
    (void)env;
    (void)clazz;
    LOGI("JNI: nativeReleaseSurface");
    release_window();
}

/*
 * Class:     com_rottr_recomp_NativeBridge
 * Method:    nativeSetInputQueue
 * Signature: (Landroid/view/InputQueue;)V
 */
JNIEXPORT void JNICALL
Java_com_rottr_recomp_NativeBridge_nativeSetInputQueue(JNIEnv* env, jclass clazz, jobject inputQueue) {
    (void)env;
    (void)clazz;
    // AInputQueue from a regular Activity needs the NativeActivity glue path;
    // keep the input subsystem in its self-polling mode for now.
    LOGI("JNI: nativeSetInputQueue queue=%p", (void*)inputQueue);
    input_set_input_queue(nullptr);
}

/*
 * Class:     com_rottr_recomp_NativeBridge
 * Method:    nativeShutdown
 * Signature: ()V
 */
JNIEXPORT void JNICALL
Java_com_rottr_recomp_NativeBridge_nativeShutdown(JNIEnv* env, jclass clazz) {
    (void)env;
    (void)clazz;
    LOGI("JNI: nativeShutdown");
    release_window();
    touch_controls_update_gamepad_c(nullptr);
    perf_shutdown();
    input_shutdown();
    audio_shutdown();
    g_state.coreInitialized = false;
}

} // extern "C"
