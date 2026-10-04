#include "SyncPulseGenerator.h"
#include <algorithm>
#include <cstring>
#include <cmath>

namespace syncwave {

SyncPulseGenerator::SyncPulseGenerator(const PulseParameters& params)
    : params_(params) {}

void SyncPulseGenerator::reset() {
    currentFrame_ = 0;
}

uint64_t SyncPulseGenerator::pulseMasterFrameIndex() const {
    return params_.leadInFrames;
}

uint64_t SyncPulseGenerator::totalFrames() const {
    return static_cast<uint64_t>(params_.leadInFrames) +
           static_cast<uint64_t>(params_.pulseDurationFrames) +
           static_cast<uint64_t>(params_.leadOutFrames);
}

uint64_t SyncPulseGenerator::framesGenerated() const {
    return currentFrame_;
}

bool SyncPulseGenerator::isComplete() const {
    return currentFrame_ >= totalFrames();
}

size_t SyncPulseGenerator::generateFrames(float* destinationBuffer, size_t frameCount, uint32_t channels) {
    if (!destinationBuffer || frameCount == 0 || channels == 0) {
        return 0;
    }

    const uint64_t total = totalFrames();
    if (currentFrame_ >= total) {
        std::memset(destinationBuffer, 0, frameCount * channels * sizeof(float));
        return 0;
    }

    const uint64_t pulseStart = params_.leadInFrames;
    const uint64_t pulseEnd = pulseStart + params_.pulseDurationFrames;

    size_t framesWritten = 0;

    for (size_t f = 0; f < frameCount; ++f) {
        if (currentFrame_ >= total) {
            for (size_t rem = f; rem < frameCount; ++rem) {
                for (uint32_t c = 0; c < channels; ++c) {
                    destinationBuffer[rem * channels + c] = 0.0f;
                }
            }
            break;
        }

        float sampleVal = 0.0f;
        if (currentFrame_ >= pulseStart && currentFrame_ < pulseEnd) {
            // Pulse window: half-cycle sine window for bandlimited clean transient
            double rel = static_cast<double>(currentFrame_ - pulseStart) / static_cast<double>(params_.pulseDurationFrames);
            sampleVal = static_cast<float>(params_.peakAmplitude * std::sin(rel * 3.14159265358979323846));
            if (sampleVal == 0.0f && params_.pulseDurationFrames == 1) {
                sampleVal = params_.peakAmplitude;
            }
        } else {
            // Lead-in or lead-out silence
            sampleVal = 0.0f;
        }

        for (uint32_t c = 0; c < channels; ++c) {
            destinationBuffer[framesWritten * channels + c] = sampleVal;
        }

        currentFrame_++;
        framesWritten++;
    }

    return framesWritten;
}

size_t SyncPulseGenerator::generateFrames(uint8_t* destinationBuffer, size_t frameCount, const AudioFormat& format) {
    if (!destinationBuffer || frameCount == 0 || format.channels == 0) {
        return 0;
    }

    std::vector<float> floatBuffer(frameCount * format.channels);
    size_t generated = generateFrames(floatBuffer.data(), frameCount, format.channels);

    // Convert to target format
    if (format.sampleType == SampleType::Float32) {
        std::memcpy(destinationBuffer, floatBuffer.data(), generated * format.bytesPerFrame());
    } else if (format.sampleType == SampleType::Int16) {
        auto* dest16 = reinterpret_cast<int16_t*>(destinationBuffer);
        for (size_t i = 0; i < generated * format.channels; ++i) {
            float val = std::clamp(floatBuffer[i], -1.0f, 1.0f);
            dest16[i] = static_cast<int16_t>(val * 32767.0f);
        }
    } else if (format.sampleType == SampleType::Int32) {
        auto* dest32 = reinterpret_cast<int32_t*>(destinationBuffer);
        for (size_t i = 0; i < generated * format.channels; ++i) {
            float val = std::clamp(floatBuffer[i], -1.0f, 1.0f);
            dest32[i] = static_cast<int32_t>(val * 2147483647.0f);
        }
    }

    return generated;
}

} // namespace syncwave
