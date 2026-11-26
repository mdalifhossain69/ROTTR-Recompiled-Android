// audio.cpp
// XAudio2 → OpenSL ES/AAudio translation layer implementation
// Provides the function declarations from audio_types.h

#include "audio/audio_types.h"
#include <cstdint>
#include <cstring>
#include <mutex>
#include <queue>
#include <vector>

// Platform-specific includes for Android
#if defined(__ANDROID__)
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <aaudio/AAudio.h>
#else
// Stub definitions for non-Android builds
typedef void* SLObjectItf;
typedef void* SLEngineItf;
typedef void* SLPlayItf;
typedef void* SLVolumeItf;
typedef void* SLAndroidSimpleBufferQueueItf;
typedef int SLresult;
#define SL_RESULT_SUCCESS 0
#define SL_BOOLEAN_TRUE 1
#define SL_BOOLEAN_FALSE 0
#define SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE 0
#define SL_DATALOCATOR_OUTPUTMIX 0
#define SL_DATAFORMAT_PCM 0
#define SL_IID_ENGINE 0
#define SL_IID_PLAY 0
#define SL_IID_VOLUME 0
#define SL_IID_ANDROIDSIMPLEBUFFERQUEUE 0
#define SL_PLAYSTATE_PLAYING 0
#define SL_PLAYSTATE_PAUSED 0
#define SL_PLAYSTATE_STOPPED 0
typedef void* AAudioStream;
typedef void* AAudioStreamBuilder;
#define AAUDIO_OK 0
#define AAUDIO_DIRECTION_OUTPUT 0
#define AAUDIO_SHARING_MODE_SHARED 0
#define AAUDIO_PERFORMANCE_MODE_LOW_LATENCY 0
#define AAUDIO_FORMAT_PCM_I16 0
#define AAUDIO_FORMAT_PCM_FLOAT 0
#define AAUDIO_STREAM_STATE_STARTED 0
#define AAUDIO_STREAM_STATE_STOPPED 0
#endif

// Internal audio engine state
struct AudioEngineState {
    bool initialized = false;
    uint32_t sampleRate = 48000;
    AudioChannel channels = AudioChannel::STEREO;
    AudioFormat format = AudioFormat::FLOAT;
    float volume = 1.0f;
    uint64_t framesWritten = 0;

#if defined(__ANDROID__)
    // OpenSL ES objects
    SLObjectItf engineObject = nullptr;
    SLEngineItf engineEngine = nullptr;
    SLObjectItf outputMixObject = nullptr;
    SLObjectItf playerObject = nullptr;
    SLPlayItf playerPlay = nullptr;
    SLVolumeItf playerVolume = nullptr;
    SLAndroidSimpleBufferQueueItf playerBufferQueue = nullptr;

    // AAudio objects (alternative, lower latency)
    AAudioStreamBuilder* streamBuilder = nullptr;
    AAudioStream* stream = nullptr;

    // Buffer queue for OpenSL ES
    std::vector<int16_t> bufferPool;
    std::queue<std::vector<int16_t>> freeBuffers;
    std::queue<std::vector<int16_t>> filledBuffers;
    std::mutex bufferMutex;
#else
    // Non-Android stub
#endif
};

static AudioEngineState g_audio;

// OpenSL ES buffer queue callback
#if defined(__ANDROID__)
static void buffer_queue_callback(SLAndroidSimpleBufferQueueItf bq, void* context) {
    AudioEngineState* state = static_cast<AudioEngineState*>(context);
    std::lock_guard<std::mutex> lock(state->bufferMutex);

    if (!state->filledBuffers.empty()) {
        auto buffer = std::move(state->filledBuffers.front());
        state->filledBuffers.pop();
        (*bq)->Enqueue(bq, buffer.data(), buffer.size() * sizeof(int16_t));
        state->freeBuffers.push(std::move(buffer));
    } else {
        // Queue empty - submit silence
        static std::vector<int16_t> silence(1024, 0);
        (*bq)->Enqueue(bq, silence.data(), silence.size() * sizeof(int16_t));
    }
}
#endif

// Initialize audio subsystem
extern "C" bool audio_init(uint32_t sampleRate, AudioChannel channels, AudioFormat format) {
    if (g_audio.initialized) {
        return true;
    }

    g_audio.sampleRate = sampleRate;
    g_audio.channels = channels;
    g_audio.format = format;
    g_audio.framesWritten = 0;

#if defined(__ANDROID__)
    // Create OpenSL ES engine
    SLresult result;

    result = slCreateEngine(&g_audio.engineObject, 0, nullptr, 0, nullptr, nullptr);
    if (result != SL_RESULT_SUCCESS) return false;

    result = (*g_audio.engineObject)->Realize(g_audio.engineObject, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) return false;

    result = (*g_audio.engineObject)->GetInterface(g_audio.engineObject, SL_IID_ENGINE, &g_audio.engineEngine);
    if (result != SL_RESULT_SUCCESS) return false;

    // Create output mix
    result = (*g_audio.engineEngine)->CreateOutputMix(g_audio.engineEngine, &g_audio.outputMixObject, 0, nullptr, nullptr);
    if (result != SL_RESULT_SUCCESS) return false;

    result = (*g_audio.outputMixObject)->Realize(g_audio.outputMixObject, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) return false;

    // Configure audio source
    SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {
        SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 16  // 16 buffers in queue
    };

    SLDataFormat_PCM format_pcm = {
        SL_DATAFORMAT_PCM,
        static_cast<SLuint32>(channels),
        static_cast<SLuint32>(sampleRate * 1000),  // mHz
        SL_PCMSAMPLEFORMAT_FIXED_16,
        SL_PCMSAMPLEFORMAT_FIXED_16,
        SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT,
        SL_BYTEORDER_LITTLEENDIAN
    };

    SLDataSource audioSrc = { &loc_bufq, &format_pcm };

    // Configure audio sink
    SLDataLocator_OutputMix loc_outmix = { SL_DATALOCATOR_OUTPUTMIX, g_audio.outputMixObject };
    SLDataSink audioSnk = { &loc_outmix, nullptr };

    // Create audio player
    const SLInterfaceID ids[2] = { SL_IID_VOLUME, SL_IID_ANDROIDSIMPLEBUFFERQUEUE };
    const SLboolean req[2] = { SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE };

    result = (*g_audio.engineEngine)->CreateAudioPlayer(g_audio.engineEngine, &g_audio.playerObject,
                                                         &audioSrc, &audioSnk, 2, ids, req);
    if (result != SL_RESULT_SUCCESS) return false;

    result = (*g_audio.playerObject)->Realize(g_audio.playerObject, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) return false;

    result = (*g_audio.playerObject)->GetInterface(g_audio.playerObject, SL_IID_PLAY, &g_audio.playerPlay);
    if (result != SL_RESULT_SUCCESS) return false;

    result = (*g_audio.playerObject)->GetInterface(g_audio.playerObject, SL_IID_VOLUME, &g_audio.playerVolume);
    if (result != SL_RESULT_SUCCESS) return false;

    result = (*g_audio.playerObject)->GetInterface(g_audio.playerObject, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &g_audio.playerBufferQueue);
    if (result != SL_RESULT_SUCCESS) return false;

    // Register callback
    result = (*g_audio.playerBufferQueue)->RegisterCallback(g_audio.playerBufferQueue, buffer_queue_callback, &g_audio);
    if (result != SL_RESULT_SUCCESS) return false;

    // Pre-fill buffer pool
    for (int i = 0; i < 16; ++i) {
        g_audio.freeBuffers.emplace(1024, 0);  // 1024 samples per buffer
    }

    // Start playing
    result = (*g_audio.playerPlay)->SetPlayState(g_audio.playerPlay, SL_PLAYSTATE_PLAYING);
    if (result != SL_RESULT_SUCCESS) return false;

#else
    // Non-Android: stub implementation
#endif

    g_audio.initialized = true;
    return true;
}

// Shutdown audio subsystem
extern "C" void audio_shutdown(void) {
    if (!g_audio.initialized) {
        return;
    }

#if defined(__ANDROID__)
    // Stop playback
    if (g_audio.playerPlay) {
        (*g_audio.playerPlay)->SetPlayState(g_audio.playerPlay, SL_PLAYSTATE_STOPPED);
    }

    // Destroy objects in reverse order
    if (g_audio.playerObject) {
        (*g_audio.playerObject)->Destroy(g_audio.playerObject);
        g_audio.playerObject = nullptr;
    }

    if (g_audio.outputMixObject) {
        (*g_audio.outputMixObject)->Destroy(g_audio.outputMixObject);
        g_audio.outputMixObject = nullptr;
    }

    if (g_audio.engineObject) {
        (*g_audio.engineObject)->Destroy(g_audio.engineObject);
        g_audio.engineObject = nullptr;
    }

    g_audio.engineEngine = nullptr;
    g_audio.playerPlay = nullptr;
    g_audio.playerVolume = nullptr;
    g_audio.playerBufferQueue = nullptr;

    // Clear buffer queues
    std::lock_guard<std::mutex> lock(g_audio.bufferMutex);
    while (!g_audio.freeBuffers.empty()) g_audio.freeBuffers.pop();
    while (!g_audio.filledBuffers.empty()) g_audio.filledBuffers.pop();
    g_audio.bufferPool.clear();
#endif

    std::memset(&g_audio, 0, sizeof(g_audio));
}

// Write audio data to the buffer
extern "C" uint32_t audio_write(const int16_t* samples, uint32_t sampleCount) {
    if (!g_audio.initialized || !samples || sampleCount == 0) {
        return 0;
    }

#if defined(__ANDROID__)
    std::lock_guard<std::mutex> lock(g_audio.bufferMutex);

    if (g_audio.freeBuffers.empty()) {
        // Buffer queue full - drop samples or wait
        return 0;
    }

    auto buffer = std::move(g_audio.freeBuffers.front());
    g_audio.freeBuffers.pop();

    uint32_t toWrite = std::min(sampleCount, static_cast<uint32_t>(buffer.size()));
    std::memcpy(buffer.data(), samples, toWrite * sizeof(int16_t));
    buffer.resize(toWrite);

    g_audio.filledBuffers.push(std::move(buffer));
    g_audio.framesWritten += toWrite / static_cast<uint32_t>(g_audio.channels);

    return toWrite;
#else
    // Non-Android: just count frames
    g_audio.framesWritten += sampleCount / static_cast<uint32_t>(g_audio.channels);
    return sampleCount;
#endif
}

// Read audio data from the buffer (for capture - not typically used in games)
extern "C" uint32_t audio_read(int16_t* samples, uint32_t sampleCount) {
    if (!g_audio.initialized || !samples || sampleCount == 0) {
        return 0;
    }

    // Most games don't read audio, but provide silence for completeness
    std::memset(samples, 0, sampleCount * sizeof(int16_t));
    return sampleCount;
}

// Get current audio position (in milliseconds)
extern "C" uint32_t audio_get_position(void) {
    if (!g_audio.initialized) {
        return 0;
    }

    uint64_t frames = g_audio.framesWritten;
    if (g_audio.sampleRate > 0) {
        return static_cast<uint32_t>((frames * 1000) / g_audio.sampleRate);
    }
    return 0;
}

// Set audio volume (0.0 to 1.0)
extern "C" void audio_set_volume(float volume) {
    if (!g_audio.initialized) {
        return;
    }

    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;

    g_audio.volume = volume;

#if defined(__ANDROID__)
    if (g_audio.playerVolume) {
        // OpenSL ES volume is in millibels (-10000 to 0)
        // Convert 0.0-1.0 to millibels (approximate)
        SLmillibel mb = static_cast<SLmillibel>(volume * volume * -10000.0f);  // quadratic for perceived loudness
        if (mb > 0) mb = 0;
        if (mb < -10000) mb = -10000;
        (*g_audio.playerVolume)->SetVolumeLevel(g_audio.playerVolume, mb);
    }
#endif
}

// --- XAudio2 Emulation Stubs ---

// Map XAudio2 flags to native audio flags
extern "C" uint32_t audio_map_xaudio2_flags(uint32_t xaudio2Flags) {
    // XAudio2 flags mapping to OpenSL ES/AAudio equivalents
    // This is a simplified mapping
    uint32_t nativeFlags = 0;

    // Common XAudio2 flags:
    // XAUDIO2_VOICE_NOSAMPLESPLAYED - not directly mappable
    // XAUDIO2_VOICE_NOPITCH - handled by not setting pitch
    // XAUDIO2_VOICE_USEFILTER - handled by filter support

    (void)xaudio2Flags;
    return nativeFlags;
}

// Convert AudioFormat to native format
extern "C" AudioFormat audio_convert_format(uint32_t formatTag) {
    // WAVE_FORMAT_* tags to AudioFormat
    switch (formatTag) {
        case 0x0001:  // WAVE_FORMAT_PCM
            return AudioFormat::PCM;
        case 0x0003:  // WAVE_FORMAT_IEEE_FLOAT
            return AudioFormat::FLOAT;
        case 0xFFFE:  // WAVE_FORMAT_EXTENSIBLE
            return AudioFormat::PCM;  // Could be anything
        case 0x0002:  // WAVE_FORMAT_ADPCM
            return AudioFormat::ADPCM;
        default:
            return AudioFormat::UNKNOWN;
    }
}

// --- XAudio2 Interface Stubs ---
// These would be called by lifted game code that uses XAudio2

struct IXAudio2 {
    virtual ~IXAudio2() = default;
};

struct IXAudio2MasteringVoice {
    virtual ~IXAudio2MasteringVoice() = default;
};

struct IXAudio2SourceVoice {
    virtual ~IXAudio2SourceVoice() = default;
};

struct IXAudio2SubmixVoice {
    virtual ~IXAudio2SubmixVoice() = default;
};

extern "C" void* xaudio2_create(uint32_t flags, uint32_t processor, void** engine) {
    (void)flags; (void)processor; (void)engine;
    // Create XAudio2 engine
    // In our implementation, this just initializes the audio subsystem
    if (!g_audio.initialized) {
        audio_init(48000, AudioChannel::STEREO, AudioFormat::FLOAT);
    }
    return reinterpret_cast<void*>(&g_audio);  // Return pointer to our state
}

extern "C" void xaudio2_destroy(void* engine) {
    (void)engine;
    audio_shutdown();
}

extern "C" void* xaudio2_create_mastering_voice(void* engine, void** voice,
                                                 uint32_t inputChannels, uint32_t inputSampleRate,
                                                 uint32_t flags, void* deviceId, void* effectChain) {
    (void)engine; (void)voice; (void)inputChannels; (void)inputSampleRate;
    (void)flags; (void)deviceId; (void)effectChain;
    // Mastering voice is essentially the output mix
    return nullptr;
}

extern "C" void* xaudio2_create_source_voice(void* engine, void** voice,
                                              void* sourceFormat, uint32_t flags,
                                              float maxFrequencyRatio, void* callback,
                                              void* sendList, void* effectChain) {
    (void)engine; (void)voice; (void)sourceFormat; (void)flags;
    (void)maxFrequencyRatio; (void)callback; (void)sendList; (void)effectChain;
    // Source voice maps to our player/buffer queue
    return nullptr;
}

extern "C" void xaudio2_source_voice_start(void* voice, uint32_t flags, uint32_t operationSet) {
    (void)voice; (void)flags; (void)operationSet;
    // Start playback
}

extern "C" void xaudio2_source_voice_stop(void* voice, uint32_t flags, uint32_t operationSet) {
    (void)voice; (void)flags; (void)operationSet;
    // Stop playback
}

extern "C" void xaudio2_source_voice_submit_source_buffer(void* voice, void* buffer, void* context) {
    (void)voice; (void)buffer; (void)context;
    // Submit buffer to queue
}

extern "C" void xaudio2_source_voice_flush_source_buffers(void* voice) {
    (void)voice;
    // Flush buffers
}

extern "C" void xaudio2_source_voice_set_volume(void* voice, float volume, uint32_t operationSet) {
    (void)voice; (void)operationSet;
    audio_set_volume(volume);
}

extern "C" void xaudio2_source_voice_set_frequency_ratio(void* voice, float ratio, uint32_t operationSet) {
    (void)voice; (void)ratio; (void)operationSet;
    // Pitch shifting - not implemented
}

extern "C" void xaudio2_mastering_voice_set_volume(void* voice, float volume, uint32_t operationSet) {
    (void)voice; (void)operationSet;
    audio_set_volume(volume);
}