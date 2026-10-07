#pragma once

#include "TimelineModel.h"
#include <vector>
#include <cstdint>
#include <string>

namespace syncwave {

// A deterministic media test block representing video frames and synchronous audio markers
struct DeterministicMediaBlock {
    int64_t mediaTimestampUs = 0;       // Media PTS
    uint32_t videoFrameNumber = 0;      // Monotonic video frame index (e.g. 30 fps)
    double videoTimestampMs = 0.0;      // Video presentation time (ms)
    bool hasAudioMarker = false;        // True if this block contains an audio sync beep/chirp
    std::vector<float> audioPcm;        // Interleaved stereo PCM frames (Float32)
    size_t audioFrameCount = 0;
};

// Generates synthetic, deterministic audio/video streams with known periodic sync markers
// for regression testing seeking, pausing, and A/V offset accuracy without human perception.
class DeterministicMediaSource {
public:
    DeterministicMediaSource(uint32_t sampleRate = 48000, uint32_t fps = 30, double durationSec = 10.0);
    ~DeterministicMediaSource() = default;

    // Reset playback position to beginning or specific media timestamp
    void reset();
    void seekUs(int64_t mediaUs);
    void seekMs(double mediaMs);

    // Fetch next block of media frames (typically ~1 video frame duration of audio)
    [[nodiscard]] DeterministicMediaBlock readNextBlock();

    [[nodiscard]] bool isEndOfStream() const;
    [[nodiscard]] int64_t currentMediaUs() const { return currentMediaUs_; }
    [[nodiscard]] double currentMediaMs() const { return static_cast<double>(currentMediaUs_) / 1000.0; }
    [[nodiscard]] double durationSec() const { return durationSec_; }
    [[nodiscard]] uint32_t sampleRate() const { return sampleRate_; }
    [[nodiscard]] uint32_t fps() const { return fps_; }

    // Generates a reference pulse or tone at every whole second mark (1.0s, 2.0s, etc.)
    static bool isMarkerTimestamp(int64_t timestampUs, int64_t blockDurationUs);

private:
    uint32_t sampleRate_ = 48000;
    uint32_t fps_ = 30;
    double durationSec_ = 10.0;
    int64_t currentMediaUs_ = 0;
    uint32_t currentVideoFrame_ = 0;
};

} // namespace syncwave
