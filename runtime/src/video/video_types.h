// video_types.h
// Type definitions for the Video Subsystem (Video format conversion and rendering)

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
    uint32_t width;
    uint32_t height;
    uint64_t timestamp;
    uint8_t* data;  // Pointer to pixel data
    uint32_t dataSize;  // Size of pixel data in bytes
};

// --- Video Renderer ---
struct VideoRenderer {
    bool initialized;
    uint32_t screenWidth;
    uint32_t screenHeight;
    VideoFormat currentFormat;
    void* nativeWindow;  // Native window handle
};

// --- Function Declarations ---
// Initialize video subsystem
extern "C" bool video_init(uint32_t width, uint32_t height);

// Shutdown video subsystem
extern "C" void video_shutdown(void);

// Convert video format (source to destination)
extern "C" bool video_convert_format(const VideoSample& src, VideoFormat dstFormat, uint8_t* dstData, uint32_t dstSize);

// Render a video frame to the screen
extern "C" bool video_render_frame(const VideoSample& sample);

// Get the current video frame
extern "C" bool video_get_sample(VideoSample& sample);

// Set the video window size
extern "C" void video_set_size(uint32_t width, uint32_t height);
