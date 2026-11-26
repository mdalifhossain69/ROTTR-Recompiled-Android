// audio_types.h
// Type definitions for the Audio Subsystem (XAudio2 → OpenSL ES/ALSA translation layer)

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
    7POINT1 = 8,
};

// --- Wave Format Ex ---
struct AudioFormatEx {
    uint16_t wFormatTag;       // Format tag (AudioFormat)
    uint16_t nChannels;        // Number of channels
    uint32_t nSamplesPerSec;   // Sample rate
    uint32_t nAvgBytesPerSec;  // Average bytes per second
    uint16_t nBlockAlign;      // Block align
    uint16_t wBitsPerSample;   // Bits per sample
    uint16_t cbSize;           // Size of extra waveformatex data
};

// --- Buffer Description ---
struct AudioBufferDesc {
    uint32_t byteWidth;         // Buffer size in bytes
    uint32_t usageFlags;        // Usage flags
    uint32_t cpuAccessFlags;    // CPU access flags
};

// --- Audio Engine ---
struct AudioEngine {
    bool initialized;
    uint32_t sampleRate;
    AudioChannel channels;
    AudioFormat format;
    void* nativeEngine;  // Native audio engine handle
};

// --- Function Declarations ---
// Initialize audio subsystem
extern "C" bool audio_init(uint32_t sampleRate, AudioChannel channels, AudioFormat format);

// Shutdown audio subsystem
extern "C" void audio_shutdown(void);

// Write audio data to the buffer
extern "C" uint32_t audio_write(const int16_t* samples, uint32_t sampleCount);

// Read audio data from the buffer
extern "C" uint32_t audio_read(int16_t* samples, uint32_t sampleCount);

// Get current audio position (in milliseconds)
extern "C" uint32_t audio_get_position(void);

// Set audio volume (0.0 to 1.0)
extern "C" void audio_set_volume(float volume);

// --- XAudio2 Emulation Stubs ---
// Map XAudio2 flags to native audio flags
extern "C" uint32_t audio_map_xaudio2_flags(uint32_t xaudio2Flags);

// Convert AudioFormat to native format
extern "C" AudioFormat audio_convert_format(uint32_t formatTag);
