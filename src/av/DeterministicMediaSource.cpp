#include "DeterministicMediaSource.h"
#include <cmath>
#include <numbers>

namespace syncwave {

DeterministicMediaSource::DeterministicMediaSource(uint32_t sampleRate, uint32_t fps, double durationSec)
    : sampleRate_(sampleRate > 0 ? sampleRate : 48000),
      fps_(fps > 0 ? fps : 30),
      durationSec_(durationSec > 0.0 ? durationSec : 10.0),
      currentMediaUs_(0),
      currentVideoFrame_(0) {}

void DeterministicMediaSource::reset() {
    currentMediaUs_ = 0;
    currentVideoFrame_ = 0;
}

void DeterministicMediaSource::seekUs(int64_t mediaUs) {
    currentMediaUs_ = std::max<int64_t>(0, std::min<int64_t>(mediaUs, static_cast<int64_t>(durationSec_ * 1000000.0)));
    currentVideoFrame_ = static_cast<uint32_t>((static_cast<double>(currentMediaUs_) * fps_) / 1000000.0);
}

void DeterministicMediaSource::seekMs(double mediaMs) {
    seekUs(static_cast<int64_t>(mediaMs * 1000.0));
}

bool DeterministicMediaSource::isEndOfStream() const {
    return currentMediaUs_ >= static_cast<int64_t>(durationSec_ * 1000000.0);
}

bool DeterministicMediaSource::isMarkerTimestamp(int64_t timestampUs, int64_t blockDurationUs) {
    // A marker occurs at every 1.0 second boundary (1,000,000 us, 2,000,000 us, etc.)
    int64_t secBoundary = ((timestampUs + blockDurationUs) / 1000000) * 1000000;
    return (secBoundary > timestampUs && secBoundary <= timestampUs + blockDurationUs);
}

DeterministicMediaBlock DeterministicMediaSource::readNextBlock() {
    DeterministicMediaBlock block;
    if (isEndOfStream()) {
        return block;
    }

    block.mediaTimestampUs = currentMediaUs_;
    block.videoFrameNumber = currentVideoFrame_;
    block.videoTimestampMs = static_cast<double>(currentMediaUs_) / 1000.0;

    // Block duration in microseconds (1 video frame duration)
    int64_t blockDurationUs = static_cast<int64_t>(std::llround(1000000.0 / fps_));
    size_t audioFrames = static_cast<size_t>(std::llround(static_cast<double>(sampleRate_) / fps_));
    block.audioFrameCount = audioFrames;
    block.audioPcm.resize(audioFrames * 2, 0.0f); // Stereo

    // Check if this block spans an exact integer second boundary (Marker Beep)
    bool hasMarker = isMarkerTimestamp(currentMediaUs_, blockDurationUs);
    block.hasAudioMarker = hasMarker;

    double basePhase = 2.0 * std::numbers::pi * 1000.0 / sampleRate_; // 1 kHz marker tone
    double bgPhase = 2.0 * std::numbers::pi * 220.0 / sampleRate_;   // 220 Hz gentle carrier

    for (size_t i = 0; i < audioFrames; ++i) {
        int64_t frameUs = currentMediaUs_ + static_cast<int64_t>((static_cast<double>(i) * 1000000.0) / sampleRate_);
        float sample = 0.0f;
        if (hasMarker && (frameUs % 1000000 < 50000)) { // 50 ms marker pulse at second boundary
            sample = 0.5f * static_cast<float>(std::sin(basePhase * i));
        } else {
            sample = 0.05f * static_cast<float>(std::sin(bgPhase * i));
        }
        block.audioPcm[i * 2 + 0] = sample;
        block.audioPcm[i * 2 + 1] = sample;
    }

    currentMediaUs_ += blockDurationUs;
    currentVideoFrame_++;
    return block;
}

} // namespace syncwave
