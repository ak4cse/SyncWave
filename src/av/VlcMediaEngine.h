#pragma once

#include "AudioTimestamp.h"
#include "TimelineModel.h"
#include "../audio/MasterAudioBus.h"
#include "../audio/OutputRouter.h"
#include <memory>
#include <string>
#include <mutex>
#include <atomic>

// Forward declare libvlc types to avoid exposing VLC internals directly in header
struct libvlc_instance_t;
struct libvlc_media_player_t;
struct libvlc_media_t;

namespace syncwave {

class VlcMediaEngine {
public:
    explicit VlcMediaEngine(MasterAudioBus& masterBus, OutputRouter& outputRouter);
    ~VlcMediaEngine();

    VlcMediaEngine(const VlcMediaEngine&) = delete;
    VlcMediaEngine& operator=(const VlcMediaEngine&) = delete;

    // Load a media file or URL for synchronized playback
    bool load(const std::string& filePathOrUrl);

    // Playback controls
    bool play();
    void pause();
    void resume();
    void stop();
    bool seek(double targetTimeMs);

    // Dynamic playback rate control (e.g. 1.0, 1.01)
    void setPlaybackRate(float rate);
    [[nodiscard]] float playbackRate() const;

    // Current media playback state
    [[nodiscard]] double currentMediaTimeMs() const;
    [[nodiscard]] double durationMs() const;
    [[nodiscard]] std::string stateString() const;
    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] bool isPaused() const;

    // Current A/V synchronization state query
    [[nodiscard]] AVSyncState getSyncState() const;

    // Timeline access
    [[nodiscard]] TimelineModel& timeline() { return timeline_; }
    [[nodiscard]] const TimelineModel& timeline() const { return timeline_; }

    // Direct audio callback handlers called from libvlc
    void handleAudioPlay(const void* samples, unsigned int count, int64_t pts);
    void handleAudioPause(int64_t pts);
    void handleAudioResume(int64_t pts);
    void handleAudioFlush(int64_t pts);
    void handleAudioDrain();

private:
    void initLibVlc();
    void cleanupLibVlc();
    void resetAudioState();

    MasterAudioBus& masterBus_;
    OutputRouter& outputRouter_;
    TimelineModel timeline_;

    libvlc_instance_t* vlcInstance_ = nullptr;
    libvlc_media_player_t* mediaPlayer_ = nullptr;
    libvlc_media_t* currentMedia_ = nullptr;

    mutable std::mutex mutex_;
    std::string currentPath_;
    std::atomic<bool> isLoaded_{false};
    std::atomic<bool> isPlaying_{false};
    std::atomic<bool> isPaused_{false};
    std::atomic<int64_t> lastAudioPts_{0};
    std::atomic<uint64_t> totalAudioFramesIngested_{0};
};

} // namespace syncwave
