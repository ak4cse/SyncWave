#include "VlcMediaEngine.h"
#include "../windows/ComHelper.h"
#include <vlc/vlc.h>
#include <iostream>
#include <filesystem>
#include <vector>
#include <cstring>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace syncwave {

namespace {

// C-style callback wrappers for libvlc
void vlcAudioPlayCallback(void* data, const void* samples, unsigned int count, int64_t pts) {
    if (data) {
        static_cast<VlcMediaEngine*>(data)->handleAudioPlay(samples, count, pts);
    }
}

void vlcAudioPauseCallback(void* data, int64_t pts) {
    if (data) {
        static_cast<VlcMediaEngine*>(data)->handleAudioPause(pts);
    }
}

void vlcAudioResumeCallback(void* data, int64_t pts) {
    if (data) {
        static_cast<VlcMediaEngine*>(data)->handleAudioResume(pts);
    }
}

void vlcAudioFlushCallback(void* data, int64_t pts) {
    if (data) {
        static_cast<VlcMediaEngine*>(data)->handleAudioFlush(pts);
    }
}

void vlcAudioDrainCallback(void* data) {
    if (data) {
        static_cast<VlcMediaEngine*>(data)->handleAudioDrain();
    }
}

int vlcAudioSetupCallback(void** data, char* format, unsigned* rate, unsigned* channels) {
    if (!data || !*data) return -1;
    // We negotiate S16N (Signed 16-bit Native PCM), 48000 Hz, stereo.
    // LibVLC will decode/downmix any input format (e.g. E-AC-3, AC-3, DTS, AAC, FLAC, Vorbis, MP3)
    // to 2-channel 48000 Hz 16-bit PCM.
    std::memcpy(format, "S16N", 4);
    *channels = 2;
    *rate = 48000;
    return 0;
}

void vlcAudioCleanupCallback(void* /*data*/) {
}

} // namespace

VlcMediaEngine::VlcMediaEngine(MasterAudioBus& masterBus, OutputRouter& outputRouter)
    : masterBus_(masterBus),
      outputRouter_(outputRouter),
      timeline_(masterBus.format().sampleRate)
{
    initLibVlc();
}

VlcMediaEngine::~VlcMediaEngine() {
    stop();
    cleanupLibVlc();
}

void VlcMediaEngine::initLibVlc() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (vlcInstance_) return;

    // Ensure VLC plugin directory is known if installed in standard location.
    // libvlccore.dll (msvcrt.dll) reads its own CRT environ via getenv("VLC_PLUGIN_PATH"),
    // so update Win32 env, UCRT env, and msvcrt.dll's _putenv if loaded.
    const char* defaultPluginsDir = "C:\\Program Files\\VideoLAN\\VLC\\plugins";
    if (std::filesystem::exists(defaultPluginsDir)) {
        SetEnvironmentVariableA("VLC_PLUGIN_PATH", defaultPluginsDir);
        const std::string envEntry = std::string("VLC_PLUGIN_PATH=") + defaultPluginsDir;
        _putenv(envEntry.c_str());
        if (HMODULE hMsvcrt = GetModuleHandleA("msvcrt.dll")) {
            using PutEnvFn = int(__cdecl*)(const char*);
            auto msvcrtPutEnv = reinterpret_cast<PutEnvFn>(
                reinterpret_cast<void*>(GetProcAddress(hMsvcrt, "_putenv")));
            if (msvcrtPutEnv) {
                msvcrtPutEnv(envEntry.c_str());
            }
        }
    }

    const char* const vlcArgs[] = {
        "--no-video",
        "--no-video-title-show",
        "--no-spdif",
        "--aout=amem" // Memory audio output plugin
    };

    vlcInstance_ = libvlc_new(sizeof(vlcArgs) / sizeof(vlcArgs[0]), vlcArgs);
    if (!vlcInstance_) {
        // Fallback without extra arguments
        vlcInstance_ = libvlc_new(0, nullptr);
    }
    if (!vlcInstance_) {
        const char* err = libvlc_errmsg();
        lastError_ = std::string("libvlc_new failed: ") + (err ? err : "check VLC plugins path");
    }
}

void VlcMediaEngine::cleanupLibVlc() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (mediaPlayer_) {
        libvlc_media_player_stop(mediaPlayer_);
        libvlc_media_player_release(mediaPlayer_);
        mediaPlayer_ = nullptr;
    }
    if (currentMedia_) {
        libvlc_media_release(currentMedia_);
        currentMedia_ = nullptr;
    }
    if (vlcInstance_) {
        libvlc_release(vlcInstance_);
        vlcInstance_ = nullptr;
    }
}

bool VlcMediaEngine::load(const std::string& filePathOrUrl) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!vlcInstance_) {
        if (lastError_.empty()) {
            lastError_ = "VLC engine instance is not initialized.";
        }
        return false;
    }
    lastError_.clear();

    if (mediaPlayer_) {
        libvlc_media_player_stop(mediaPlayer_);
        libvlc_media_player_release(mediaPlayer_);
        mediaPlayer_ = nullptr;
    }
    if (currentMedia_) {
        libvlc_media_release(currentMedia_);
        currentMedia_ = nullptr;
    }

    if (filePathOrUrl.empty()) {
        lastError_ = "Media path is empty.";
        return false;
    }

    currentPath_ = filePathOrUrl;
    bool isUrl = (filePathOrUrl.find("://") != std::string::npos);
    if (!isUrl) {
        std::filesystem::path fsPath(utf8ToWide(filePathOrUrl));
        if (!std::filesystem::exists(fsPath)) {
            lastError_ = "Local media file not found: " + filePathOrUrl;
            return false;
        }
        std::filesystem::path absPath = std::filesystem::absolute(fsPath);
        std::string absPathUtf8 = wideToUtf8(absPath.wstring());
        currentMedia_ = libvlc_media_new_path(vlcInstance_, absPathUtf8.c_str());
    } else {
        currentMedia_ = libvlc_media_new_location(vlcInstance_, filePathOrUrl.c_str());
    }

    if (!currentMedia_) {
        const char* err = libvlc_errmsg();
        lastError_ = "Failed to create VLC media from: " + filePathOrUrl + 
                     (err ? " (" + std::string(err) + ")" : "");
        return false;
    }

    mediaPlayer_ = libvlc_media_player_new_from_media(currentMedia_);
    if (!mediaPlayer_) {
        const char* err = libvlc_errmsg();
        lastError_ = "Failed to create VLC media player: " + 
                     (err ? std::string(err) : "unknown error");
        return false;
    }

    // Register real-time audio callbacks
    libvlc_audio_set_callbacks(
        mediaPlayer_,
        vlcAudioPlayCallback,
        vlcAudioPauseCallback,
        vlcAudioResumeCallback,
        vlcAudioFlushCallback,
        vlcAudioDrainCallback,
        this
    );

    // Register format negotiation callbacks for multi-format decoding (E-AC-3, AC-3, AAC, etc.)
    libvlc_audio_set_format_callbacks(
        mediaPlayer_,
        vlcAudioSetupCallback,
        vlcAudioCleanupCallback
    );

    isLoaded_.store(true, std::memory_order_release);
    return true;
}

bool VlcMediaEngine::play() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!mediaPlayer_) {
        lastError_ = "No media loaded to play.";
        return false;
    }

    int res = libvlc_media_player_play(mediaPlayer_);
    if (res == 0) {
        isPlaying_.store(true, std::memory_order_release);
        isPaused_.store(false, std::memory_order_release);
        lastError_.clear();
        return true;
    }

    const char* err = libvlc_errmsg();
    lastError_ = "Failed to start VLC media playback: " + 
                 (err ? std::string(err) : "unknown error");
    return false;
}

void VlcMediaEngine::pause() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!mediaPlayer_) return;

    libvlc_media_player_set_pause(mediaPlayer_, 1);
    isPaused_.store(true, std::memory_order_release);
}

void VlcMediaEngine::resume() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!mediaPlayer_) return;

    libvlc_media_player_set_pause(mediaPlayer_, 0);
    isPaused_.store(false, std::memory_order_release);
}

void VlcMediaEngine::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!mediaPlayer_) return;

    libvlc_media_player_stop(mediaPlayer_);
    isPlaying_.store(false, std::memory_order_release);
    isPaused_.store(false, std::memory_order_release);
    resetAudioState();
}

bool VlcMediaEngine::seek(double targetTimeMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!mediaPlayer_) return false;

    int64_t targetUs = static_cast<int64_t>(targetTimeMs * 1000.0);
    uint64_t curFrame = masterBus_.totalFramesWritten();
    timeline_.onSeek(targetUs, curFrame);

    libvlc_media_player_set_time(mediaPlayer_, static_cast<libvlc_time_t>(targetTimeMs));
    return true;
}

void VlcMediaEngine::setPlaybackRate(float rate) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (mediaPlayer_ && rate > 0.1f && rate < 4.0f) {
        libvlc_media_player_set_rate(mediaPlayer_, rate);
    }
}

float VlcMediaEngine::playbackRate() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return mediaPlayer_ ? libvlc_media_player_get_rate(mediaPlayer_) : 1.0f;
}

double VlcMediaEngine::currentMediaTimeMs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!mediaPlayer_) return 0.0;
    libvlc_time_t t = libvlc_media_player_get_time(mediaPlayer_);
    return static_cast<double>(t >= 0 ? t : 0);
}

double VlcMediaEngine::durationMs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!mediaPlayer_) return 0.0;
    libvlc_time_t d = libvlc_media_player_get_length(mediaPlayer_);
    return static_cast<double>(d >= 0 ? d : 0);
}

std::string VlcMediaEngine::stateString() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!mediaPlayer_) return "Stopped";
    libvlc_state_t state = libvlc_media_player_get_state(mediaPlayer_);
    switch (state) {
        case libvlc_NothingSpecial: return "Idle";
        case libvlc_Opening:        return "Opening";
        case libvlc_Buffering:      return "Buffering";
        case libvlc_Playing:        return "Playing";
        case libvlc_Paused:         return "Paused";
        case libvlc_Stopped:        return "Stopped";
        case libvlc_Ended:          return "Ended";
        case libvlc_Error:          return "Error";
        default:                    return "Unknown";
    }
}

bool VlcMediaEngine::isPlaying() const {
    return isPlaying_.load(std::memory_order_acquire);
}

bool VlcMediaEngine::isPaused() const {
    return isPaused_.load(std::memory_order_acquire);
}

std::string VlcMediaEngine::lastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

void VlcMediaEngine::resetAudioState() {
    masterBus_.reset();
    timeline_.reset();
    totalAudioFramesIngested_.store(0, std::memory_order_relaxed);
    lastAudioPts_.store(0, std::memory_order_relaxed);
}

AVSyncState VlcMediaEngine::getSyncState() const {
    AVSyncState state;
    state.videoPositionMs = currentMediaTimeMs();
    state.mediaPositionMs = state.videoPositionMs;
    state.playbackState = stateString();
    state.playbackRate = static_cast<double>(playbackRate());

    uint64_t renderedFrames = outputRouter_.totalFramesDistributed();
    state.audioMasterPositionMs = timeline_.masterFrameToMediaMs(renderedFrames);

    // Calculate acoustic arrival by adding average output latency
    auto models = outputRouter_.getLatencyModels();
    double avgLatencyMs = 0.0;
    if (!models.empty()) {
        double sum = 0.0;
        for (const auto& m : models) {
            sum += m.effectiveLatencyMs;
        }
        avgLatencyMs = sum / models.size();
    }
    state.audioAcousticPositionMs = state.audioMasterPositionMs + avgLatencyMs;

    // Signed offset: audio - video
    state.audioVideoOffsetMs = TimelineModel::calculateAvOffsetMs(state.audioMasterPositionMs, state.videoPositionMs);
    state.isSynchronized = std::abs(state.audioVideoOffsetMs) < 40.0; // Within 40 ms lip-sync threshold
    return state;
}

void VlcMediaEngine::handleAudioPlay(const void* samples, unsigned int count, int64_t pts) {
    if (!samples || count == 0) return;

    lastAudioPts_.store(pts, std::memory_order_relaxed);
    totalAudioFramesIngested_.fetch_add(count, std::memory_order_relaxed);

    // Convert S16N (int16_t) stereo samples to Float32 [-1.0f, +1.0f] for MasterAudioBus
    const int16_t* pcm16 = static_cast<const int16_t*>(samples);
    size_t totalSamples = static_cast<size_t>(count) * 2;

    thread_local std::vector<float> conversionBuffer;
    if (conversionBuffer.size() < totalSamples) {
        conversionBuffer.resize(totalSamples);
    }

    constexpr float kNorm16 = 1.0f / 32768.0f;
    for (size_t i = 0; i < totalSamples; ++i) {
        conversionBuffer[i] = static_cast<float>(pcm16[i]) * kNorm16;
    }

    masterBus_.write(conversionBuffer.data(), count);
    outputRouter_.dispatch(masterBus_);
}

void VlcMediaEngine::handleAudioPause(int64_t /*pts*/) {
    isPaused_.store(true, std::memory_order_relaxed);
}

void VlcMediaEngine::handleAudioResume(int64_t /*pts*/) {
    isPaused_.store(false, std::memory_order_relaxed);
}

void VlcMediaEngine::handleAudioFlush(int64_t pts) {
    // Media source seek or discontinuity occurred: flush unread buffers and re-anchor
    masterBus_.flush();
    uint64_t currentFrame = masterBus_.totalFramesWritten();
    timeline_.onDiscontinuity(pts, currentFrame);
}

void VlcMediaEngine::handleAudioDrain() {
    // Stream end reached
}

} // namespace syncwave
