#pragma once

#include "../audio/AudioFormat.h"
#include <cstdint>
#include <cstddef>
#include <vector>

namespace syncwave {

struct PulseParameters {
    uint32_t leadInFrames = 24000;   // 500 ms at 48 kHz
    uint32_t pulseDurationFrames = 48; // 1.0 ms at 48 kHz
    uint32_t leadOutFrames = 24000;  // 500 ms at 48 kHz
    float peakAmplitude = 1.0f;      // Full-scale pulse peak
};

class SyncPulseGenerator {
public:
    explicit SyncPulseGenerator(const PulseParameters& params = {});

    void reset();

    // Returns exact master frame index where the impulse starts
    [[nodiscard]] uint64_t pulseMasterFrameIndex() const;

    // Total sequence length in frames
    [[nodiscard]] uint64_t totalFrames() const;

    // Frames generated so far
    [[nodiscard]] uint64_t framesGenerated() const;

    // True when all sequence frames have been delivered
    [[nodiscard]] bool isComplete() const;

    // Generates interleaved Float32 frames directly into destination buffer
    size_t generateFrames(float* destinationBuffer, size_t frameCount, uint32_t channels);

    // Generates interleaved frames formatted according to AudioFormat
    size_t generateFrames(uint8_t* destinationBuffer, size_t frameCount, const AudioFormat& format);

private:
    PulseParameters params_;
    uint64_t currentFrame_ = 0;
};

} // namespace syncwave
