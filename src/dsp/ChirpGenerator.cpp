#include "ChirpGenerator.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace syncwave {

static constexpr double PI = 3.14159265358979323846;

ChirpGenerator::ChirpGenerator(const ChirpParameters& params)
    : params_(params), currentFrame_(0)
{
    reset();
}

void ChirpGenerator::reset() {
    currentFrame_ = 0;

    const size_t mFrames = static_cast<size_t>(std::round(params_.durationSec * params_.sampleRate));
    precomputedChirp_.resize(mFrames, 0.0f);

    if (mFrames == 0 || params_.durationSec <= 0.0) {
        return;
    }

    const double f0 = params_.startFreqHz;
    const double f1 = params_.endFreqHz;
    const double T = params_.durationSec;
    const double k = (f1 - f0) / T; // Linear chirp sweep rate

    // Calculate window ramp frames for Tukey window
    const size_t rampFrames = static_cast<size_t>(std::round(params_.windowRampSec * params_.sampleRate));

    for (size_t m = 0; m < mFrames; ++m) {
        const double t = static_cast<double>(m) / params_.sampleRate;
        const double phase = 2.0 * PI * (f0 * t + 0.5 * k * t * t);
        const double rawSample = std::sin(phase);

        // Tukey window
        double window = 1.0;
        if (rampFrames > 0) {
            if (m < rampFrames) {
                window = 0.5 * (1.0 - std::cos(PI * static_cast<double>(m) / rampFrames));
            } else if (m >= mFrames - rampFrames) {
                window = 0.5 * (1.0 - std::cos(PI * static_cast<double>(mFrames - 1 - m) / rampFrames));
            }
        }

        precomputedChirp_[m] = static_cast<float>(params_.amplitude * window * rawSample);
    }
}

uint64_t ChirpGenerator::chirpMasterFrameIndex() const {
    return static_cast<uint64_t>(std::round(params_.leadInSec * params_.sampleRate));
}

uint64_t ChirpGenerator::chirpFrames() const {
    return precomputedChirp_.size();
}

uint64_t ChirpGenerator::totalFrames() const {
    const uint64_t leadIn = chirpMasterFrameIndex();
    const uint64_t chirp = chirpFrames();
    const uint64_t leadOut = static_cast<uint64_t>(std::round(params_.leadOutSec * params_.sampleRate));
    return leadIn + chirp + leadOut;
}

uint64_t ChirpGenerator::framesGenerated() const {
    return currentFrame_;
}

bool ChirpGenerator::isComplete() const {
    return currentFrame_ >= totalFrames();
}

std::vector<float> ChirpGenerator::generateReferenceChirp() const {
    return precomputedChirp_;
}

size_t ChirpGenerator::generateFrames(float* destinationBuffer, size_t frameCount, uint32_t channels) {
    if (!destinationBuffer || frameCount == 0 || channels == 0) {
        return 0;
    }

    const uint64_t total = totalFrames();
    const uint64_t leadIn = chirpMasterFrameIndex();
    const uint64_t chirpEnd = leadIn + chirpFrames();

    size_t framesDelivered = 0;

    for (size_t i = 0; i < frameCount; ++i) {
        float sampleVal = 0.0f;
        if (currentFrame_ < total) {
            if (currentFrame_ >= leadIn && currentFrame_ < chirpEnd) {
                size_t chirpIdx = static_cast<size_t>(currentFrame_ - leadIn);
                sampleVal = precomputedChirp_[chirpIdx];
            }
            ++currentFrame_;
        }

        for (uint32_t c = 0; c < channels; ++c) {
            destinationBuffer[i * channels + c] = sampleVal;
        }
        ++framesDelivered;
    }

    return framesDelivered;
}

size_t ChirpGenerator::generateFrames(uint8_t* destinationBuffer, size_t frameCount, const AudioFormat& format) {
    if (!destinationBuffer || frameCount == 0 || format.channels == 0) {
        return 0;
    }

    std::vector<float> tempFloat(frameCount * format.channels);
    size_t delivered = generateFrames(tempFloat.data(), frameCount, format.channels);

    if (format.sampleType == SampleType::Float32) {
        std::memcpy(destinationBuffer, tempFloat.data(), delivered * format.blockAlign);
    } else if (format.sampleType == SampleType::Int16) {
        int16_t* dest16 = reinterpret_cast<int16_t*>(destinationBuffer);
        for (size_t s = 0; s < delivered * format.channels; ++s) {
            float val = std::clamp(tempFloat[s], -1.0f, 1.0f);
            dest16[s] = static_cast<int16_t>(val * 32767.0f);
        }
    } else if (format.sampleType == SampleType::Int32 || format.sampleType == SampleType::Int24In32) {
        int32_t* dest32 = reinterpret_cast<int32_t*>(destinationBuffer);
        for (size_t s = 0; s < delivered * format.channels; ++s) {
            float val = std::clamp(tempFloat[s], -1.0f, 1.0f);
            dest32[s] = static_cast<int32_t>(val * 2147483647.0);
        }
    } else {
        std::memset(destinationBuffer, 0, delivered * format.blockAlign);
    }

    return delivered;
}

} // namespace syncwave
