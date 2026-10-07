#pragma once

#include "AudioTimestamp.h"
#include <cstdint>
#include <mutex>
#include <atomic>

namespace syncwave {

// Deterministic timeline mapper and converter connecting media presentation
// time (PTS) with SyncWave's MasterAudioBus frame sequence.
class TimelineModel {
public:
    explicit TimelineModel(uint32_t masterSampleRate = 48000);
    ~TimelineModel() = default;

    void setSampleRate(uint32_t sampleRate);
    [[nodiscard]] uint32_t sampleRate() const { return sampleRate_; }

    // Map a media presentation timestamp (microseconds) to a master bus frame index
    [[nodiscard]] uint64_t mediaUsToMasterFrame(int64_t mediaUs) const;

    // Convert master bus frame index to media presentation timestamp (microseconds)
    [[nodiscard]] int64_t masterFrameToMediaUs(uint64_t masterFrame) const;

    // Convert master bus frame index to media timestamp in milliseconds
    [[nodiscard]] double masterFrameToMediaMs(uint64_t masterFrame) const;

    // Convert media milliseconds to master frames
    [[nodiscard]] uint64_t mediaMsToMasterFrame(double mediaMs) const;

    // Calculate signed A/V offset in milliseconds
    // Sign convention: offset = audioTimeMs - videoTimeMs
    // Positive: Audio leads video
    // Negative: Audio lags video
    [[nodiscard]] static double calculateAvOffsetMs(double audioTimeMs, double videoTimeMs);

    // Timeline anchor operations (Seek, Flush, Restart)
    void reset();
    void onSeek(int64_t newMediaUs, uint64_t currentMasterFrame);
    void onDiscontinuity(int64_t newMediaUs, uint64_t currentMasterFrame);

    // Current timeline anchor
    [[nodiscard]] int64_t anchorMediaUs() const;
    [[nodiscard]] uint64_t anchorMasterFrame() const;
    [[nodiscard]] bool hasAnchor() const;

private:
    uint32_t sampleRate_ = 48000;
    mutable std::mutex mutex_;
    int64_t anchorMediaUs_ = 0;
    uint64_t anchorMasterFrame_ = 0;
    bool hasAnchor_ = false;
};

} // namespace syncwave
