#pragma once

#include "../audio/AudioFormat.h"
#include <vector>
#include <cstdint>
#include <cstddef>

namespace syncwave {

struct ChirpParameters {
    uint32_t sampleRate = 48000;
    double durationSec = 0.150;      // 150 ms chirp
    double startFreqHz = 300.0;      // 300 Hz start frequency
    double endFreqHz = 8000.0;       // 8 kHz end frequency
    double leadInSec = 0.500;        // 500 ms lead-in silence
    double leadOutSec = 1.000;       // 1000 ms lead-out silence
    float amplitude = 0.25f;         // Safe, conservative amplitude
    double windowRampSec = 0.010;    // 10 ms cosine ramp-in and ramp-out
};

class ChirpGenerator {
public:
    explicit ChirpGenerator(const ChirpParameters& params = {});

    void reset();

    // Returns exact master frame index where the active chirp begins (after lead-in silence)
    [[nodiscard]] uint64_t chirpMasterFrameIndex() const;

    // Total sequence length in frames (leadIn + chirp + leadOut)
    [[nodiscard]] uint64_t totalFrames() const;

    // Active chirp portion length in frames
    [[nodiscard]] uint64_t chirpFrames() const;

    // Frames generated so far
    [[nodiscard]] uint64_t framesGenerated() const;

    // True when all sequence frames have been delivered
    [[nodiscard]] bool isComplete() const;

    // Generates the isolated reference chirp template (mono Float32) for cross-correlation
    [[nodiscard]] std::vector<float> generateReferenceChirp() const;

    // Generates interleaved Float32 frames directly into destination buffer
    size_t generateFrames(float* destinationBuffer, size_t frameCount, uint32_t channels);

    // Generates interleaved frames formatted according to AudioFormat
    size_t generateFrames(uint8_t* destinationBuffer, size_t frameCount, const AudioFormat& format);

    [[nodiscard]] const ChirpParameters& parameters() const { return params_; }

private:
    ChirpParameters params_;
    uint64_t currentFrame_ = 0;
    std::vector<float> precomputedChirp_;
};

} // namespace syncwave
