#pragma once

#include <cstdint>
#include <string>

namespace syncwave {

// Public and internal audio timestamp contract formalizing the relationship
// between media presentation time and the authoritative Master Audio Timeline.
struct AudioTimestamp {
    // Authoritative frame counter on MasterAudioBus
    uint64_t masterFrame = 0;

    // Nominal sample rate of the master bus (typically 48000 Hz)
    uint32_t sampleRate = 48000;

    // Source media presentation timestamp in microseconds (PTS from demuxer)
    int64_t mediaTimestampUs = 0;

    // Windows QueryPerformanceCounter tick at sample instant
    uint64_t qpcTimestamp = 0;

    // True if this block immediately follows a seek, flush, or stream discontinuity
    bool isDiscontinuity = false;

    [[nodiscard]] double masterTimeSec() const {
        return (sampleRate > 0) ? (static_cast<double>(masterFrame) / sampleRate) : 0.0;
    }

    [[nodiscard]] double mediaTimeSec() const {
        return static_cast<double>(mediaTimestampUs) / 1000000.0;
    }

    [[nodiscard]] double mediaTimeMs() const {
        return static_cast<double>(mediaTimestampUs) / 1000.0;
    }
};

// Represents the instantaneous Audio/Video synchronization state
struct AVSyncState {
    // Current media playback position reported by media source (ms)
    double mediaPositionMs = 0.0;

    // Position of authoritative audio master timeline (ms)
    double audioMasterPositionMs = 0.0;

    // Estimated synchronized acoustic arrival position (ms)
    double audioAcousticPositionMs = 0.0;

    // Current video presentation timestamp (ms)
    double videoPositionMs = 0.0;

    // Audio/Video offset: audio_position - video_position (ms)
    // Positive: Audio is ahead of Video (lips move after speech)
    // Negative: Audio is behind Video (speech heard after lips move)
    double audioVideoOffsetMs = 0.0;

    // Configured target offset (e.g. manual user offset or acoustic compensation)
    double targetOffsetMs = 0.0;

    // Status: Playing, Paused, Seeking, Stopped
    std::string playbackState = "Stopped";

    // Playback speed rate (e.g. 1.0)
    double playbackRate = 1.0;

    bool isSynchronized = false;
};

} // namespace syncwave
