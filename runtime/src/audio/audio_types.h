// audio_types.h
// Type definitions for the Audio Subsystem (XAudio2 → OpenSL ES/AAudio translation layer)

#pragma once

#include <cstdint>

// --- Windows basic types ---
typedef int32_t BOOL;
typedef uint32_t UINT;
typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;

// --- Core Types ---
enum class AudioFormat {
    UNKNOWN = 0,
    FLOAT = 1,
    INT16 = 2,
    INT32 = 3,
    ADPCM = 4,
    PCM = 5,
};

// --- Channel Config ---
enum class AudioChannel {
    MONO = 1,
    STEREO = 2,
    QUAD = 4,
    SURROUND = 5,
    QUINT = 6,
    CHANNELS_7POINT1 = 8,
};

// --- Wave Format Ex ---
struct AudioFormatEx {
    UINT16 wFormatTag;       // Format tag (AudioFormat)
    UINT16 nChannels;        // Number of channels
    UINT32 nSamplesPerSec;   // Sample rate
    UINT32 nAvgBytesPerSec;  // Average bytes per second
    UINT16 nBlockAlign;      // Block align
    UINT16 wBitsPerSample;   // Bits per sample
    UINT16 cbSize;           // Size of extra waveformatex data
};

// --- Buffer Description ---
struct AudioBufferDesc {
    UINT32 byteWidth;         // Buffer size in bytes
    UINT32 usageFlags;        // Usage flags
    UINT32 cpuAccessFlags;    // CPU access flags
};

// --- Audio Engine ---
struct AudioEngine {
    bool initialized;
    UINT32 sampleRate;
    AudioChannel channels;
    AudioFormat format;
    void* nativeEngine;  // Native audio engine handle
};

// --- Function Declarations ---
// Initialize audio subsystem
extern "C" bool audio_init(UINT32 sampleRate, AudioChannel channels, AudioFormat format);

// Shutdown audio subsystem
extern "C" void audio_shutdown(void);

// Write audio data to the buffer
extern "C" UINT32 audio_write(const int16_t* samples, UINT32 sampleCount);

// Read audio data from the buffer
extern "C" UINT32 audio_read(int16_t* samples, UINT32 sampleCount);

// Get current audio position (in milliseconds)
extern "C" UINT32 audio_get_position(void);

// Set audio volume (0.0 to 1.0)
extern "C" void audio_set_volume(float volume);

// --- XAudio2 Emulation Stubs ---
// Map XAudio2 flags to native audio flags
extern "C" UINT32 audio_map_xaudio2_flags(UINT32 xaudio2Flags);

// Convert AudioFormat to native format
extern "C" AudioFormat audio_convert_format(UINT32 formatTag);

// Phase 6: Low-latency audio control
extern "C" void audio_set_low_latency(bool enable);
extern "C" int audio_get_backend(void);  // 0=unknown, 1=OpenSL ES, 2=AAudio
extern "C" uint64_t audio_get_underruns(void);