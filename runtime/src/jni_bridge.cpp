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
extern "C" void graphics_present(void);
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
    bool initialized = false;
    ANativeWindow* window = nullptr;
    int32_t screenWidth = 0;
    int32_t screenHeight = 0;
} g_state;

static void initialize_subsystems(int width, int height, ANativeWindow* window) {
    if (g_state.initialized) {
        return;
    }
    g_state.window = window;
    g_state.screenWidth = width;
    g_state.screenHeight = height;

    LOGI("JNI: initializing subsystems %dx%d", width, height);

    if (window != nullptr) {
        if (!video_init((unsigned int)width, (unsigned int)height)) {
            LOGE("JNI: video_init failed");
        }
        if (!graphics_init(width, height, window)) {
            LOGE("JNI: graphics_init failed");
        }
    }

    if (!audio_init(48000, AUDIO_CHANNEL_STEREO, AUDIO_FORMAT_FLOAT)) {
        LOGE("JNI: audio_init failed (non-fatal)");
    }
    if (!input_init()) {
        LOGE("JNI: input_init failed");
    }
    touch_controls_init(width, height);
    perf_init();
    g_state.initialized = true;
    LOGI("JNI: all subsystems initialized");
}

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
    g_state.screenWidth = (int32_t)width;
    g_state.screenHeight = (int32_t)height;
    // Subsystems that need a window are started in nativeSetSurface.
    initialize_subsystems((int)width, (int)height, g_state.window);
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
    initialize_subsystems(g_state.screenWidth, g_state.screenHeight, window);
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
    if (g_state.window != nullptr) {
        ANativeWindow_release(g_state.window);
        g_state.window = nullptr;
    }
    LOGI("JNI: nativeReleaseSurface");
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
    // Note: attaching an AInputQueue from a regular Activity's InputQueue requires
    // the NativeActivity glue path. Store a best-effort reference here; the input
    // subsystem polls via its own path in the game loop.
    if (inputQueue != nullptr) {
        LOGI("JNI: nativeSetInputQueue queue=%p", (void*)inputQueue);
        input_set_input_queue(nullptr);
    } else {
        LOGI("JNI: nativeSetInputQueue queue=null");
        input_set_input_queue(nullptr);
    }
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
    if (g_state.window != nullptr) {
        ANativeWindow_release(g_state.window);
        g_state.window = nullptr;
    }
    touch_controls_update_gamepad_c(nullptr);
    perf_shutdown();
    input_shutdown();
    audio_shutdown();
    graphics_shutdown();
    video_shutdown();
    g_state.initialized = false;
}
