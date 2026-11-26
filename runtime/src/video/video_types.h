// video_types.h
// Type definitions for the Video Subsystem (Video format conversion and rendering)

#pragma once

#include <cstdint>

// --- Windows basic types ---
typedef uint32_t UINT32;
typedef uint64_t UINT64;

// --- Video Format ---
enum class VideoFormat {
    UNKNOWN = 0,
    I420 = 1,   // YUV 4:2:0 planar
    NV12 = 2,   // YUV 4:2:0 semi-planar
    NV21 = 3,   // YUV 4:2:0 semi-planar (swapped UV)
    RGBA = 4,   // RGBA 8888
    BGRX = 5,   // BGRX 8888
    BGR = 6,    // BGR 888
    // ... more formats
};

// --- Video Sample ---
struct VideoSample {
    VideoFormat format;
    UINT32 width;
    UINT32 height;
    UINT64 timestamp;
    uint8_t* data;  // Pointer to pixel data
    UINT32 dataSize;  // Size of pixel data in bytes
};

// --- Video Renderer ---
struct VideoRenderer {
    bool initialized;
    UINT32 screenWidth;
    UINT32 screenHeight;
    VideoFormat currentFormat;
    void* nativeWindow;  // Native window handle
};

// --- Function Declarations ---
// Initialize video subsystem
extern "C" bool video_init(UINT32 width, UINT32 height);

// Shutdown video subsystem
extern "C" void video_shutdown(void);

// Convert video format (source to destination)
extern "C" bool video_convert_format(const VideoSample& src, VideoFormat dstFormat, uint8_t* dstData, UINT32 dstSize);

// Render a video frame to the screen
extern "C" bool video_render_frame(const VideoSample& sample);

// Get the current video frame
extern "C" bool video_get_sample(VideoSample& sample);

// Set the video window size
extern "C" void video_set_size(UINT32 width, UINT32 height);

// Set the native window for rendering
extern "C" void video_set_native_window(void* window);

// Get video statistics
extern "C" void video_get_stats(UINT64* framesRendered, UINT64* bytesProcessed, UINT32* frameCount);

// --- Media Foundation / IMFTransform Stubs ---
// These would be called by lifted game code for video decoding

struct IMFTransform {
    virtual ~IMFTransform() = default;
};

struct IMFMediaType {
    virtual ~IMFMediaType() = default;
};

struct IMFSample {
    virtual ~IMFSample() = default;
};

extern "C" void* mf_create_transform(void* clsid, void** transform);
extern "C" void* mf_transform_process_input(void* transform, UINT32 inputStreamId, void* sample, UINT32 flags);
extern "C" void* mf_transform_process_output(void* transform, UINT32 flags, UINT32 outputBufferCount, void* outputBuffers, UINT32* status);
extern "C" void* mf_transform_set_input_type(void* transform, UINT32 inputStreamId, void* type, UINT32 flags);
extern "C" void* mf_transform_set_output_type(void* transform, UINT32 outputStreamId, void* type, UINT32 flags);