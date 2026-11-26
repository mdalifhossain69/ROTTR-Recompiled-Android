// performance.h
// Unified performance monitoring API

#pragma once

#include <cstdint>

struct PerfStats {
    uint64_t frameCount;
    double currentFPS;
    double avgFrameTimeMs;
    double minFrameTimeMs;
    double maxFrameTimeMs;
    double gpuFrameTimeMs;
    double cpuFrameTimeMs;
    double audioBufferMs;
    uint64_t audioUnderruns;
    uint64_t memoryUsedBytes;
    uint64_t memoryPeakBytes;
};

// Initialize performance monitoring
extern "C" void perf_init(void);

// Shutdown performance monitoring
extern "C" void perf_shutdown(void);

// Record frame timing (called once per frame)
extern "C" void perf_record_frame(double frameTimeMs);

// Record GPU frame time
extern "C" void perf_record_gpu_time(double gpuTimeMs);

// Record CPU frame time
extern "C" void perf_record_cpu_time(double cpuTimeMs);

// Record audio buffer latency
extern "C" void perf_record_audio_buffer(double bufferMs);

// Record audio underrun
extern "C" void perf_record_audio_underrun(void);

// Record memory usage
extern "C" void perf_record_memory(uint64_t usedBytes, uint64_t peakBytes);

// Get performance stats
extern "C" void perf_get_stats(PerfStats* outStats);

// Get FPS
extern "C" double perf_get_fps(void);

// Get average frame time
extern "C" double perf_get_avg_frame_time(void);

// Reset performance stats
extern "C" void perf_reset_stats(void);

// Print performance report (for debugging)
extern "C" void perf_print_report(void);