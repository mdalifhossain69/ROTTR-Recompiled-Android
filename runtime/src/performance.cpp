// performance.cpp
// Unified performance monitoring for all subsystems
// Phase 6: FPS, frame time, GPU/CPU usage tracking

#include "performance.h"
#include "graphics/graphics_types.h"
#include "audio/audio_types.h"
#include "input/input_types.h"
#include "video/video_types.h"
#include <cstdint>
#include <cstring>

// Performance state
struct PerfState {
    bool initialized = false;

    // Frame timing
    uint64_t frameCount = 0;
    double currentFPS = 0.0;
    double avgFrameTimeMs = 0.0;
    double minFrameTimeMs = 9999.0;
    double maxFrameTimeMs = 0.0;

    // Subsystem stats
    double gpuFrameTimeMs = 0.0;
    double cpuFrameTimeMs = 0.0;
    double audioBufferMs = 0.0;
    uint64_t audioUnderruns = 0;

    // Memory
    uint64_t memoryUsedBytes = 0;
    uint64_t memoryPeakBytes = 0;

    // Timestamp for FPS calculation
    uint64_t lastFPSTimestamp = 0;
    uint64_t framesSinceFPSTimestamp = 0;
};

static PerfState g_perf;

// Initialize performance monitoring
extern "C" void perf_init(void) {
    std::memset(&g_perf, 0, sizeof(g_perf));
    g_perf.minFrameTimeMs = 9999.0;
    g_perf.initialized = true;
}

// Shutdown performance monitoring
extern "C" void perf_shutdown(void) {
    g_perf.initialized = false;
}

// Record frame timing (called once per frame)
extern "C" void perf_record_frame(double frameTimeMs) {
    if (!g_perf.initialized) return;

    g_perf.frameCount++;
    g_perf.framesSinceFPSTimestamp++;

    if (frameTimeMs < g_perf.minFrameTimeMs) g_perf.minFrameTimeMs = frameTimeMs;
    if (frameTimeMs > g_perf.maxFrameTimeMs) g_perf.maxFrameTimeMs = frameTimeMs;

    // Running average (exponential moving average)
    if (g_perf.avgFrameTimeMs == 0.0) {
        g_perf.avgFrameTimeMs = frameTimeMs;
    } else {
        g_perf.avgFrameTimeMs = g_perf.avgFrameTimeMs * 0.9 + frameTimeMs * 0.1;
    }

    // Update FPS every second (assuming 1000ms timestamps)
    // In a real implementation, use actual time
    if (g_perf.framesSinceFPSTimestamp >= 60) {  // Approximate 1 second at 60 FPS
        g_perf.currentFPS = g_perf.framesSinceFPSTimestamp * (1000.0 / (g_perf.avgFrameTimeMs * g_perf.framesSinceFPSTimestamp));
        g_perf.framesSinceFPSTimestamp = 0;
    }
}

// Record GPU frame time
extern "C" void perf_record_gpu_time(double gpuTimeMs) {
    if (!g_perf.initialized) return;
    g_perf.gpuFrameTimeMs = gpuTimeMs;
}

// Record CPU frame time
extern "C" void perf_record_cpu_time(double cpuTimeMs) {
    if (!g_perf.initialized) return;
    g_perf.cpuFrameTimeMs = cpuTimeMs;
}

// Record audio buffer latency
extern "C" void perf_record_audio_buffer(double bufferMs) {
    if (!g_perf.initialized) return;
    g_perf.audioBufferMs = bufferMs;
}

// Record audio underrun
extern "C" void perf_record_audio_underrun(void) {
    if (!g_perf.initialized) return;
    g_perf.audioUnderruns++;
}

// Record memory usage
extern "C" void perf_record_memory(uint64_t usedBytes, uint64_t peakBytes) {
    if (!g_perf.initialized) return;
    g_perf.memoryUsedBytes = usedBytes;
    if (peakBytes > g_perf.memoryPeakBytes) {
        g_perf.memoryPeakBytes = peakBytes;
    }
}

// Get performance stats
extern "C" void perf_get_stats(PerfStats* outStats) {
    if (!g_perf.initialized || !outStats) return;

    outStats->frameCount = g_perf.frameCount;
    outStats->currentFPS = g_perf.currentFPS;
    outStats->avgFrameTimeMs = g_perf.avgFrameTimeMs;
    outStats->minFrameTimeMs = g_perf.minFrameTimeMs;
    outStats->maxFrameTimeMs = g_perf.maxFrameTimeMs;
    outStats->gpuFrameTimeMs = g_perf.gpuFrameTimeMs;
    outStats->cpuFrameTimeMs = g_perf.cpuFrameTimeMs;
    outStats->audioBufferMs = g_perf.audioBufferMs;
    outStats->audioUnderruns = g_perf.audioUnderruns;
    outStats->memoryUsedBytes = g_perf.memoryUsedBytes;
    outStats->memoryPeakBytes = g_perf.memoryPeakBytes;
}

// Get FPS
extern "C" double perf_get_fps(void) {
    if (!g_perf.initialized) return 0.0;
    return g_perf.currentFPS;
}

// Get average frame time
extern "C" double perf_get_avg_frame_time(void) {
    if (!g_perf.initialized) return 0.0;
    return g_perf.avgFrameTimeMs;
}

// Reset performance stats
extern "C" void perf_reset_stats(void) {
    if (!g_perf.initialized) return;
    g_perf.frameCount = 0;
    g_perf.currentFPS = 0.0;
    g_perf.avgFrameTimeMs = 0.0;
    g_perf.minFrameTimeMs = 9999.0;
    g_perf.maxFrameTimeMs = 0.0;
    g_perf.framesSinceFPSTimestamp = 0;
}

// Print performance report (for debugging)
extern "C" void perf_print_report(void) {
    if (!g_perf.initialized) return;

    // In a real implementation, this would use android log
    // For now, it's a placeholder
}