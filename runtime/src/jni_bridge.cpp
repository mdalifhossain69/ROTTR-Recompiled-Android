// JNI exports for com.rottr.recomp.NativeBridge: owns the Vulkan render thread for the
// Surface supplied by the boot screen. All entry points are called on the UI thread.

#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include "rottr/pe_inspect.h"
#include "rottr/vulkan_renderer.h"

#define LOG_TAG "ROTTR-Vulkan"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

// These lines reach the ROTTR-logs file through the app's logcat capture.
void log_line(bool isError, const char* message) {
    __android_log_write(isError ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO, LOG_TAG, message);
}

ANativeWindow* g_window = nullptr;
std::unique_ptr<rottr::VulkanRenderer> g_renderer;
std::thread g_thread;
std::atomic<bool> g_running{false};
int g_stagesDone = 1;

void render_loop(rottr::VulkanRenderer* renderer) {
    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    auto lastReport = start;
    while (g_running.load()) {
        const auto now = clock::now();
        if (!renderer->drawFrame(std::chrono::duration<float>(now - start).count())) {
            LOGE("rendering stopped after an unrecoverable Vulkan error");
            break;
        }
        if (now - lastReport >= std::chrono::seconds(5)) {
            LOGI("frames presented: %llu", static_cast<unsigned long long>(renderer->presentedFrames()));
            lastReport = now;
        }
    }
}

void stop_rendering() {
    g_running.store(false);
    if (g_thread.joinable()) g_thread.join();
    g_renderer.reset();  // destroys Vulkan objects before the window is released
    if (g_window != nullptr) {
        ANativeWindow_release(g_window);
        g_window = nullptr;
    }
}

bool start_rendering(ANativeWindow* window) {
    g_window = window;  // takes over the caller's reference
    auto renderer = std::make_unique<rottr::VulkanRenderer>(log_line);
    const auto createSurface = [window](VkInstance instance, VkSurfaceKHR* surface) {
        VkAndroidSurfaceCreateInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
        info.window = window;
        return vkCreateAndroidSurfaceKHR(instance, &info, nullptr, surface);
    };
    if (!renderer->init(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME, createSurface,
                        static_cast<uint32_t>(ANativeWindow_getWidth(window)),
                        static_cast<uint32_t>(ANativeWindow_getHeight(window)))) {
        stop_rendering();
        return false;
    }
    renderer->setStagesDone(g_stagesDone);
    g_renderer = std::move(renderer);
    g_running.store(true);
    g_thread = std::thread(render_loop, g_renderer.get());
    return true;
}

// Reads the whole file behind fd (which stays owned by the caller) and checks its headers.
std::string inspect_fd(int fd, bool& passed) {
    constexpr off_t kMaxBytes = 512 * 1024 * 1024;
    struct stat info{};
    if (fstat(fd, &info) != 0 || info.st_size <= 0 || info.st_size > kMaxBytes)
        return "Header check failed: cannot size the file, or it is empty or over 512 MiB\n";
    std::vector<uint8_t> bytes;
    try {
        bytes.resize(static_cast<size_t>(info.st_size));
    } catch (const std::bad_alloc&) {
        return "Header check failed: not enough memory\n";
    }
    size_t done = 0;
    while (done < bytes.size()) {
        const ssize_t n = pread(fd, bytes.data() + done, bytes.size() - done, static_cast<off_t>(done));
        if (n <= 0) return "Header check failed: could not read the file\n";
        done += static_cast<size_t>(n);
    }
    const rottr::PeReport report = rottr::inspectPe(bytes.data(), bytes.size());
    passed = report.passed();
    return rottr::describePe(report);
}

}  // namespace

extern "C" {

// Returns "1\n<report>" if the check passed, otherwise "0\n<report>". Report text is ASCII.
JNIEXPORT jstring JNICALL
Java_com_rottr_recomp_NativeBridge_nativeInspectExe(JNIEnv* env, jclass, jint fd) {
    bool passed = false;
    const std::string text = inspect_fd(fd, passed);
    return env->NewStringUTF(((passed ? "1\n" : "0\n") + text).c_str());
}

JNIEXPORT jboolean JNICALL
Java_com_rottr_recomp_NativeBridge_nativeSetSurface(JNIEnv* env, jclass, jobject surface) {
    if (surface == nullptr) {
        stop_rendering();
        return JNI_FALSE;
    }
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (window == nullptr) {
        LOGE("ANativeWindow_fromSurface returned null");
        return JNI_FALSE;
    }
    if (window == g_window && g_renderer != nullptr) {
        ANativeWindow_release(window);  // already rendering to it; size changes are handled there
        return JNI_TRUE;
    }
    stop_rendering();
    return start_rendering(window) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_rottr_recomp_NativeBridge_nativeReleaseSurface(JNIEnv*, jclass) {
    stop_rendering();
}

JNIEXPORT void JNICALL
Java_com_rottr_recomp_NativeBridge_nativeSetStagesDone(JNIEnv*, jclass, jint count) {
    g_stagesDone = count;
    if (g_renderer != nullptr) g_renderer->setStagesDone(count);
}

}  // extern "C"
