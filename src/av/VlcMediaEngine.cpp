#include "VlcMediaEngine.h"
#include <vlc/vlc.h>
#include <iostream>
#include <filesystem>

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

    // Ensure VLC plugin directory is known if installed in standard location
    const char* defaultVlcDir = "C:\\Program Files\\VideoLAN\\VLC";
    if (std::filesystem::exists(defaultVlcDir)) {
        SetEnvironmentVariableA("VLC_PLUGIN_PATH", "C:\\Program Files\\VideoLAN\\VLC\\plugins");
    }

    const char* const vlcArgs[] = {
        "--no-video",
        "--no-video-title-show",
        "--aout=amem" // Memory audio output plugin
    };

    vlcInstance_ = libvlc_new(sizeof(vlcArgs) / sizeof(vlcArgs[0]), vlcArgs);
    if (!vlcInstance_) {
        // Fallback without extra arguments
        vlcInstance_ = libvlc_new(0, nullptr);
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
    if (!vlcInstance_) return false;

    if (mediaPlayer_) {
        libvlc_media_player_stop(mediaPlayer_);
        libvlc_media_player_release(mediaPlayer_);
        mediaPlayer_ = nullptr;
    }
    if (currentMedia_) {
        libvlc_media_release(currentMedia_);
        currentMedia_ = nullptr;
    }

    currentPath_ = filePathOrUrl;
    bool isUrl = (filePathOrUrl.find("://") != std::string::npos);
    if (!isUrl && !std::filesystem::exists(filePathOrUrl)) {
        return false;
    }

    if (!isUrl && std::filesystem::exists(filePathOrUrl)) {
        std::string absPath = std::filesystem::absolute(filePathOrUrl).string();
        currentMedia_ = libvlc_media_new_path(vlcInstance_, absPath.c_str());
    } else {
        currentMedia_ = libvlc_media_new_location(vlcInstance_, filePathOrUrl.c_str());
    }

    if (!currentMedia_) return false;

    mediaPlayer_ = libvlc_media_player_new_from_media(currentMedia_);
    if (!mediaPlayer_) return false;

    // Configure memory audio output format matching MasterAudioBus canonical format (Float32, 48000Hz, stereo)
    uint32_t sRate = masterBus_.format().sampleRate > 0 ? masterBus_.format().sampleRate : 48000;
    libvlc_audio_set_format(mediaPlayer_, "FL32", sRate, 2);

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

    isLoaded_.store(true, std::memory_order_release);
    return true;
}

bool VlcMediaEngine::play() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!mediaPlayer_) return false;

    int res = libvlc_media_player_play(mediaPlayer_);
    if (res == 0) {
        isPlaying_.store(true, std::memory_order_release);
        isPaused_.store(false, std::memory_order_release);
        return true;
    }
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

    const float* floatSamples = static_cast<const float*>(samples);
    masterBus_.write(floatSamples, count);
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
