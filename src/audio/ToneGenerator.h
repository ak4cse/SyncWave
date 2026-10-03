#pragma once

#include "AudioFormat.h"
#include <cstdint>
#include <cmath>

namespace syncwave {

class ToneGenerator {
public:
    explicit ToneGenerator(double frequency = 440.0, double volume = 0.25);

    void setFrequency(double frequency);
    [[nodiscard]] double frequency() const { return frequency_; }

    void setVolume(double volume);
    [[nodiscard]] double volume() const { return volume_; }

    void resetPhase();
    [[nodiscard]] double currentPhase() const { return phase_; }

    // Generates interleaved audio frames directly into destination buffer
    void generateFrames(uint8_t* destinationBuffer, uint32_t frameCount, const AudioFormat& format);

private:
    double frequency_ = 440.0;
    double volume_ = 0.25;
    double phase_ = 0.0;
};

} // namespace syncwave
