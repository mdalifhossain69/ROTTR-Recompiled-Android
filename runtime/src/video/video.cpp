// video.cpp
// Video format conversion and rendering implementation
// Provides the function declarations from video_types.h

#include "video/video_types.h"
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <vector>

// Forward declaration for graphics subsystem
extern "C" bool graphics_render_frame(const VideoSample& sample);

// Platform-specific includes for Android
#if defined(__ANDROID__)
#include <android/native_window.h>
#include <android/native_window_jni.h>
#else
typedef struct ANativeWindow ANativeWindow;
void ANativeWindow_acquire(ANativeWindow* window) { (void)window; }
void ANativeWindow_release(ANativeWindow* window) { (void)window; }
int32_t ANativeWindow_setBuffersGeometry(ANativeWindow* window, int32_t width, int32_t height, int32_t format) { (void)window; (void)width; (void)height; (void)format; return 0; }
int32_t ANativeWindow_lock(ANativeWindow* window, void* outBuffer, void* inOutDirtyBounds) { (void)window; (void)outBuffer; (void)inOutDirtyBounds; return 0; }
int32_t ANativeWindow_unlockAndPost(ANativeWindow* window) { (void)window; return 0; }

typedef struct ANativeWindow_Buffer {
    void* bits;
    int32_t width;
    int32_t height;
    int32_t stride;
    int32_t format;
    void* reserved[6];
} ANativeWindow_Buffer;

#define WINDOW_FORMAT_RGBA_8888 1
#define WINDOW_FORMAT_RGBX_8888 2
#define WINDOW_FORMAT_RGB_565 4
#endif

// Internal video state
struct VideoState {
    bool initialized = false;
    uint32_t width = 0;
    uint32_t height = 0;
    VideoFormat currentFormat = VideoFormat::UNKNOWN;
    ANativeWindow* nativeWindow = nullptr;

    // Conversion buffers
    std::vector<uint8_t> conversionBuffer;
    uint32_t conversionBufferSize = 0;

    // Frame timing
    uint64_t lastFrameTime = 0;
    uint32_t frameCount = 0;

    // Statistics
    uint64_t totalFramesRendered = 0;
    uint64_t totalBytesProcessed = 0;
};

static VideoState g_video;

// YUV to RGB conversion functions
static void yuv420p_to_rgba(const uint8_t* yData, const uint8_t* uData, const uint8_t* vData,
                            uint8_t* rgba, uint32_t width, uint32_t height,
                            int yStride, int uStride, int vStride) {
    (void)vStride;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            int yIdx = y * yStride + x;
            int uvIdx = (y / 2) * uStride + (x / 2);

            int Y = yData[yIdx];
            int U = uData[uvIdx] - 128;
            int V = vData[uvIdx] - 128;

            // BT.601 conversion
            int R = Y + 1.402f * V;
            int G = Y - 0.344f * U - 0.714f * V;
            int B = Y + 1.772f * U;

            // Clamp
            R = std::max(0, std::min(255, R));
            G = std::max(0, std::min(255, G));
            B = std::max(0, std::min(255, B));

            int rgbaIdx = (y * width + x) * 4;
            rgba[rgbaIdx] = static_cast<uint8_t>(R);
            rgba[rgbaIdx + 1] = static_cast<uint8_t>(G);
            rgba[rgbaIdx + 2] = static_cast<uint8_t>(B);
            rgba[rgbaIdx + 3] = 255;
        }
    }
}

static void nv12_to_rgba(const uint8_t* yData, const uint8_t* uvData,
                         uint8_t* rgba, uint32_t width, uint32_t height,
                         int yStride, int uvStride) {
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            int yIdx = y * yStride + x;
            int uvIdx = (y / 2) * uvStride + (x / 2) * 2;

            int Y = yData[yIdx];
            int U = uvData[uvIdx] - 128;
            int V = uvData[uvIdx + 1] - 128;

            int R = Y + 1.402f * V;
            int G = Y - 0.344f * U - 0.714f * V;
            int B = Y + 1.772f * U;

            R = std::max(0, std::min(255, R));
            G = std::max(0, std::min(255, G));
            B = std::max(0, std::min(255, B));

            int rgbaIdx = (y * width + x) * 4;
            rgba[rgbaIdx] = static_cast<uint8_t>(R);
            rgba[rgbaIdx + 1] = static_cast<uint8_t>(G);
            rgba[rgbaIdx + 2] = static_cast<uint8_t>(B);
            rgba[rgbaIdx + 3] = 255;
        }
    }
}

static void i420_to_rgba(const uint8_t* data, uint8_t* rgba, uint32_t width, uint32_t height) {
    // I420 is planar YUV 4:2:0
    const uint8_t* yData = data;
    const uint8_t* uData = data + width * height;
    const uint8_t* vData = uData + (width * height) / 4;

    yuv420p_to_rgba(yData, uData, vData, rgba, width, height,
                    width, width / 2, width / 2);
}

static void nv12_to_rgba(const uint8_t* data, uint8_t* rgba, uint32_t width, uint32_t height) {
    // NV12 is semi-planar YUV 4:2:0 (Y plane + interleaved UV)
    const uint8_t* yData = data;
    const uint8_t* uvData = data + width * height;

    nv12_to_rgba(yData, uvData, rgba, width, height, width, width);
}

static void nv21_to_rgba(const uint8_t* data, uint8_t* rgba, uint32_t width, uint32_t height) {
    // NV21 is semi-planar YUV 4:2:0 (Y plane + interleaved VU)
    const uint8_t* yData = data;
    const uint8_t* vuData = data + width * height;

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            int yIdx = y * width + x;
            int vuIdx = (y / 2) * width + (x / 2) * 2;

            int Y = yData[yIdx];
            int V = vuData[vuIdx] - 128;    // V first in NV21
            int U = vuData[vuIdx + 1] - 128; // U second

            int R = Y + 1.402f * V;
            int G = Y - 0.344f * U - 0.714f * V;
            int B = Y + 1.772f * U;

            R = std::max(0, std::min(255, R));
            G = std::max(0, std::min(255, G));
            B = std::max(0, std::min(255, B));

            int rgbaIdx = (y * width + x) * 4;
            rgba[rgbaIdx] = static_cast<uint8_t>(R);
            rgba[rgbaIdx + 1] = static_cast<uint8_t>(G);
            rgba[rgbaIdx + 2] = static_cast<uint8_t>(B);
            rgba[rgbaIdx + 3] = 255;
        }
    }
}

static void rgba_to_rgba(const uint8_t* src, uint8_t* dst, uint32_t width, uint32_t height) {
    // Already RGBA, just copy
    std::memcpy(dst, src, width * height * 4);
}

static void bgrx_to_rgba(const uint8_t* src, uint8_t* dst, uint32_t width, uint32_t height) {
    for (uint32_t i = 0; i < width * height; ++i) {
        dst[i * 4] = src[i * 4 + 2];     // R
        dst[i * 4 + 1] = src[i * 4 + 1]; // G
        dst[i * 4 + 2] = src[i * 4];     // B
        dst[i * 4 + 3] = 255;            // A
    }
}

static void bgr_to_rgba(const uint8_t* src, uint8_t* dst, uint32_t width, uint32_t height) {
    for (uint32_t i = 0; i < width * height; ++i) {
        dst[i * 4] = src[i * 3 + 2];     // R
        dst[i * 4 + 1] = src[i * 3 + 1]; // G
        dst[i * 4 + 2] = src[i * 3];     // B
        dst[i * 4 + 3] = 255;            // A
    }
}

// Ensure conversion buffer is large enough
static void ensure_conversion_buffer(uint32_t width, uint32_t height) {
    uint32_t needed = width * height * 4;  // RGBA
    if (g_video.conversionBufferSize < needed) {
        g_video.conversionBuffer.resize(needed);
        g_video.conversionBufferSize = needed;
    }
}

// Initialize video subsystem
extern "C" bool video_init(uint32_t width, uint32_t height) {
    if (g_video.initialized) {
        return true;
    }

    g_video.width = width;
    g_video.height = height;
    g_video.currentFormat = VideoFormat::UNKNOWN;
    g_video.nativeWindow = nullptr;
    g_video.frameCount = 0;
    g_video.totalFramesRendered = 0;
    g_video.totalBytesProcessed = 0;

    // Pre-allocate conversion buffer
    ensure_conversion_buffer(width, height);

    g_video.initialized = true;
    return true;
}

// Shutdown video subsystem
extern "C" void video_shutdown(void) {
    if (!g_video.initialized) {
        return;
    }

    if (g_video.nativeWindow) {
        ANativeWindow_release(g_video.nativeWindow);
        g_video.nativeWindow = nullptr;
    }

    g_video.conversionBuffer.clear();
    g_video.conversionBufferSize = 0;

    std::memset(&g_video, 0, sizeof(g_video));
}

// Convert video format (source to destination)
extern "C" bool video_convert_format(const VideoSample& src, VideoFormat dstFormat,
                                     uint8_t* dstData, uint32_t dstSize) {
    if (!g_video.initialized || !src.data || !dstData) {
        return false;
    }

    uint32_t width = src.width;
    uint32_t height = src.height;
    uint32_t requiredSize = width * height * 4;  // RGBA output

    if (dstSize < requiredSize) {
        return false;  // Destination buffer too small
    }

    ensure_conversion_buffer(width, height);
    uint8_t* rgbaBuffer = g_video.conversionBuffer.data();

    // Convert source format to RGBA
    switch (src.format) {
        case VideoFormat::I420:
            i420_to_rgba(src.data, rgbaBuffer, width, height);
            break;
        case VideoFormat::NV12:
            nv12_to_rgba(src.data, rgbaBuffer, width, height);
            break;
        case VideoFormat::NV21:
            nv21_to_rgba(src.data, rgbaBuffer, width, height);
            break;
        case VideoFormat::RGBA:
            rgba_to_rgba(src.data, rgbaBuffer, width, height);
            break;
        case VideoFormat::BGRX:
            bgrx_to_rgba(src.data, rgbaBuffer, width, height);
            break;
        case VideoFormat::BGR:
            bgr_to_rgba(src.data, rgbaBuffer, width, height);
            break;
        default:
            return false;  // Unsupported format
    }

    // Convert RGBA to destination format if needed
    if (dstFormat == VideoFormat::RGBA) {
        std::memcpy(dstData, rgbaBuffer, requiredSize);
    } else if (dstFormat == VideoFormat::BGRX) {
        // RGBA to BGRX
        for (uint32_t i = 0; i < width * height; ++i) {
            dstData[i * 4] = rgbaBuffer[i * 4 + 2];
            dstData[i * 4 + 1] = rgbaBuffer[i * 4 + 1];
            dstData[i * 4 + 2] = rgbaBuffer[i * 4];
            dstData[i * 4 + 3] = 0;  // X (unused)
        }
    } else if (dstFormat == VideoFormat::BGR) {
        // RGBA to BGR
        for (uint32_t i = 0; i < width * height; ++i) {
            dstData[i * 3] = rgbaBuffer[i * 4 + 2];
            dstData[i * 3 + 1] = rgbaBuffer[i * 4 + 1];
            dstData[i * 3 + 2] = rgbaBuffer[i * 4];
        }
    } else {
        return false;  // Unsupported destination format
    }

    g_video.totalBytesProcessed += src.dataSize;
    return true;
}

// Set the native window for rendering
extern "C" void video_set_native_window(void* window) {
    ANativeWindow* nativeWindow = static_cast<ANativeWindow*>(window);
    if (g_video.nativeWindow) {
        ANativeWindow_release(g_video.nativeWindow);
    }
    g_video.nativeWindow = nativeWindow;
    if (nativeWindow) {
        ANativeWindow_acquire(nativeWindow);
        ANativeWindow_setBuffersGeometry(nativeWindow, g_video.width, g_video.height, WINDOW_FORMAT_RGBA_8888);
    }
}

// Render a video frame to the screen
extern "C" bool video_render_frame(const VideoSample& sample) {
    if (!g_video.initialized || !sample.data) {
        return false;
    }

    // Convert to RGBA if needed
    ensure_conversion_buffer(sample.width, sample.height);
    uint8_t* rgbaBuffer = g_video.conversionBuffer.data();

    switch (sample.format) {
        case VideoFormat::I420:
            i420_to_rgba(sample.data, rgbaBuffer, sample.width, sample.height);
            break;
        case VideoFormat::NV12:
            nv12_to_rgba(sample.data, rgbaBuffer, sample.width, sample.height);
            break;
        case VideoFormat::NV21:
            nv21_to_rgba(sample.data, rgbaBuffer, sample.width, sample.height);
            break;
        case VideoFormat::RGBA:
            rgba_to_rgba(sample.data, rgbaBuffer, sample.width, sample.height);
            break;
        case VideoFormat::BGRX:
            bgrx_to_rgba(sample.data, rgbaBuffer, sample.width, sample.height);
            break;
        case VideoFormat::BGR:
            bgr_to_rgba(sample.data, rgbaBuffer, sample.width, sample.height);
            break;
        default:
            return false;
    }

    // Render to native window if available
    if (g_video.nativeWindow) {
        // Resize window if dimensions changed
        if (sample.width != g_video.width || sample.height != g_video.height) {
            g_video.width = sample.width;
            g_video.height = sample.height;
            ANativeWindow_setBuffersGeometry(g_video.nativeWindow, g_video.width, g_video.height, WINDOW_FORMAT_RGBA_8888);
            ensure_conversion_buffer(g_video.width, g_video.height);
        }

        // Lock window buffer
        ANativeWindow_Buffer buffer;
        if (ANativeWindow_lock(g_video.nativeWindow, &buffer, nullptr) == 0) {
            // Copy RGBA to window buffer
            uint32_t copyWidth = std::min(sample.width, static_cast<uint32_t>(buffer.width));
            uint32_t copyHeight = std::min(sample.height, static_cast<uint32_t>(buffer.height));

            for (uint32_t y = 0; y < copyHeight; ++y) {
                std::memcpy(
                    static_cast<uint8_t*>(buffer.bits) + y * buffer.stride * 4,
                    rgbaBuffer + y * sample.width * 4,
                    copyWidth * 4
                );
            }

            ANativeWindow_unlockAndPost(g_video.nativeWindow);
        }
    }

    // Also forward to graphics subsystem if available
    graphics_render_frame(sample);

    g_video.totalFramesRendered++;
    g_video.totalBytesProcessed += sample.dataSize;
    g_video.lastFrameTime = sample.timestamp;
    g_video.frameCount++;

    return true;
}

// Get the current video frame
extern "C" bool video_get_sample(VideoSample& sample) {
    if (!g_video.initialized) {
        return false;
    }

    // Return info about last rendered frame
    sample.format = g_video.currentFormat;
    sample.width = g_video.width;
    sample.height = g_video.height;
    sample.timestamp = g_video.lastFrameTime;
    sample.data = g_video.conversionBuffer.empty() ? nullptr : g_video.conversionBuffer.data();
    sample.dataSize = g_video.conversionBufferSize;

    return true;
}

// Set the video window size
extern "C" void video_set_size(uint32_t width, uint32_t height) {
    if (!g_video.initialized) {
        return;
    }

    g_video.width = width;
    g_video.height = height;

    if (g_video.nativeWindow) {
        ANativeWindow_setBuffersGeometry(g_video.nativeWindow, width, height, WINDOW_FORMAT_RGBA_8888);
    }

    ensure_conversion_buffer(width, height);
}

// Get video statistics
extern "C" void video_get_stats(uint64_t* framesRendered, uint64_t* bytesProcessed, uint32_t* frameCount) {
    if (framesRendered) *framesRendered = g_video.totalFramesRendered;
    if (bytesProcessed) *bytesProcessed = g_video.totalBytesProcessed;
    if (frameCount) *frameCount = g_video.frameCount;
}

// --- Media Foundation / IMFTransform Stubs ---
// These would be called by lifted game code for video decoding

extern "C" void* mf_create_transform(void* clsid, void** transform) {
    (void)clsid; (void)transform;
    // Create Media Foundation transform (decoder)
    return nullptr;
}

extern "C" void* mf_transform_process_input(void* transform, uint32_t inputStreamId,
                                             void* sample, uint32_t flags) {
    (void)transform; (void)inputStreamId; (void)sample; (void)flags;
    // Process input sample
    return nullptr;
}

extern "C" void* mf_transform_process_output(void* transform, uint32_t flags,
                                              uint32_t outputBufferCount, void* outputBuffers,
                                              uint32_t* status) {
    (void)transform; (void)flags; (void)outputBufferCount; (void)outputBuffers; (void)status;
    // Process output
    return nullptr;
}

extern "C" void* mf_transform_set_input_type(void* transform, uint32_t inputStreamId,
                                              void* type, uint32_t flags) {
    (void)transform; (void)inputStreamId; (void)type; (void)flags;
    // Set input media type
    return nullptr;
}

extern "C" void* mf_transform_set_output_type(void* transform, uint32_t outputStreamId,
                                               void* type, uint32_t flags) {
    (void)transform; (void)outputStreamId; (void)type; (void)flags;
    // Set output media type
    return nullptr;
}