#include "ToneGenerator.h"
#include <algorithm>
#include <cstring>

namespace syncwave {

constexpr double TWO_PI = 6.28318530717958647692;

ToneGenerator::ToneGenerator(double frequency, double volume) {
    setFrequency(frequency);
    setVolume(volume);
}

void ToneGenerator::setFrequency(double frequency) {
    frequency_ = std::clamp(frequency, 1.0, 24000.0);
}

void ToneGenerator::setVolume(double volume) {
    volume_ = std::clamp(volume, 0.0, 1.0);
}

void ToneGenerator::resetPhase() {
    phase_ = 0.0;
}

void ToneGenerator::generateFrames(uint8_t* destinationBuffer, uint32_t frameCount, const AudioFormat& format) {
    if (!destinationBuffer || frameCount == 0 || format.sampleRate == 0 || format.channels == 0) {
        return;
    }

    const double phaseIncrement = (TWO_PI * frequency_) / static_cast<double>(format.sampleRate);
    const uint16_t channels = format.channels;

    if (format.sampleType == SampleType::Float32) {
        auto* out = reinterpret_cast<float*>(destinationBuffer);
        for (uint32_t i = 0; i < frameCount; ++i) {
            float sample = static_cast<float>(volume_ * std::sin(phase_));
            for (uint16_t ch = 0; ch < channels; ++ch) {
                *out++ = sample;
            }
            phase_ += phaseIncrement;
            if (phase_ >= TWO_PI) {
                phase_ -= TWO_PI;
            }
        }
    } else if (format.sampleType == SampleType::Int16) {
        auto* out = reinterpret_cast<int16_t*>(destinationBuffer);
        for (uint32_t i = 0; i < frameCount; ++i) {
            int16_t sample = static_cast<int16_t>(volume_ * std::sin(phase_) * 32767.0);
            for (uint16_t ch = 0; ch < channels; ++ch) {
                *out++ = sample;
            }
            phase_ += phaseIncrement;
            if (phase_ >= TWO_PI) {
                phase_ -= TWO_PI;
            }
        }
    } else if (format.sampleType == SampleType::Int32 || format.sampleType == SampleType::Int24In32) {
        auto* out = reinterpret_cast<int32_t*>(destinationBuffer);
        for (uint32_t i = 0; i < frameCount; ++i) {
            int32_t sample = static_cast<int32_t>(volume_ * std::sin(phase_) * 2147483647.0);
            for (uint16_t ch = 0; ch < channels; ++ch) {
                *out++ = sample;
            }
            phase_ += phaseIncrement;
            if (phase_ >= TWO_PI) {
                phase_ -= TWO_PI;
            }
        }
    } else {
        // Fallback: fill with silence
        std::memset(destinationBuffer, 0, frameCount * format.bytesPerFrame());
    }
}

} // namespace syncwave
