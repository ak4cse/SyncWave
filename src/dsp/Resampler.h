#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace syncwave {

class MasterAudioBus;

class Resampler {
public:
    Resampler(uint32_t inSampleRate = 48000, uint32_t outSampleRate = 48000, uint32_t channels = 2);
    ~Resampler() = default;

    // Reconfigure sample rates and channels
    void reset(uint32_t inSampleRate, uint32_t outSampleRate, uint32_t channels);

    // Reset internal phase and residual buffers
    void resetState();

    // Pull exactly outFrames into outBuffer by reading from MasterAudioBus and resampling
    // If inSampleRate == outSampleRate, directly reads from bus
    size_t pull(MasterAudioBus& bus, float* outBuffer, size_t outFrames);

    // Direct block processing (inBuffer -> outBuffer)
    // Returns actual number of output frames written
    size_t process(const float* inBuffer, size_t inFrames, float* outBuffer, size_t maxOutFrames);

    [[nodiscard]] uint32_t inSampleRate() const { return inRate_; }
    [[nodiscard]] uint32_t outSampleRate() const { return outRate_; }
    [[nodiscard]] uint32_t channels() const { return channels_; }
    [[nodiscard]] double ratio() const { return ratio_; }

private:
    uint32_t inRate_ = 48000;
    uint32_t outRate_ = 48000;
    uint32_t channels_ = 2;
    double ratio_ = 1.0;     // inRate / outRate
    double phase_ = 0.0;     // fractional phase in [0, 1)

    std::vector<float> lastSample_; // channels_ samples from previous input block
    bool hasLastSample_ = false;

    // Internal FIFO for pull mode
    std::vector<float> fifoBuffer_;
    size_t fifoReadIndex_ = 0;
    size_t fifoWriteIndex_ = 0;
    size_t fifoCount_ = 0;

    void fifoPush(const float* data, size_t frames);
    size_t fifoPop(float* dest, size_t frames);

    // Scratch buffers for chunked reading during pull
    std::vector<float> busChunkBuffer_;
    std::vector<float> resampledChunkBuffer_;
};

} // namespace syncwave
