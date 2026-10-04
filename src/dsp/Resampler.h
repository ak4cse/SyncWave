#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <atomic>

namespace syncwave {

class MasterAudioBus;
class RingBuffer;

class Resampler {
public:
    Resampler(uint32_t inSampleRate = 48000, uint32_t outSampleRate = 48000, uint32_t channels = 2);
    ~Resampler() = default;

    // Reconfigure sample rates and channels
    void reset(uint32_t inSampleRate, uint32_t outSampleRate, uint32_t channels);

    // Reset internal phase and residual buffers
    void resetState();

    // Pull exactly outFrames into outBuffer by reading from MasterAudioBus and resampling
    size_t pull(MasterAudioBus& bus, float* outBuffer, size_t outFrames);

    // Pull exactly outFrames into outBuffer by reading from a lock-free RingBuffer and resampling
    size_t pull(RingBuffer& queue, float* outBuffer, size_t outFrames);

    // Direct block processing (inBuffer -> outBuffer)
    // Returns actual number of output frames written
    size_t process(const float* inBuffer, size_t inFrames, float* outBuffer, size_t maxOutFrames);

    [[nodiscard]] uint32_t inSampleRate() const { return inRate_; }
    [[nodiscard]] uint32_t outSampleRate() const { return outRate_; }
    [[nodiscard]] uint32_t channels() const { return channels_; }
    [[nodiscard]] double ratio() const { return ratio_; }
    [[nodiscard]] double baseRatio() const { return baseRatio_; }
    [[nodiscard]] double targetRateAdjustmentPpm() const { return targetAdjustmentPpm_.load(std::memory_order_relaxed); }
    [[nodiscard]] double currentRateAdjustmentPpm() const { return currentAdjustmentPpm_; }
    [[nodiscard]] bool hasRateAdjustment() const {
        return (targetAdjustmentPpm_.load(std::memory_order_relaxed) != 0.0) || (currentAdjustmentPpm_ != 0.0);
    }
    [[nodiscard]] size_t fifoCount() const { return fifoCount_; }

    // Configure rate adjustment in parts-per-million (PPM).
    // If immediate is true, slewing is bypassed and applied instantly (useful for testing).
    void setRateAdjustmentPpm(double ppm, bool immediate = false);

    // Configure maximum slew rate in PPM per second (default: 5.0 ppm/s)
    void setSlewRatePpmPerSecond(double slewRatePpmPerSec);
    [[nodiscard]] double slewRatePpmPerSecond() const { return slewRatePpmPerSec_; }

private:
    uint32_t inRate_ = 48000;
    uint32_t outRate_ = 48000;
    uint32_t channels_ = 2;
    double baseRatio_ = 1.0; // inRate / outRate
    double ratio_ = 1.0;     // effective ratio incorporating rate adjustment
    double phase_ = 0.0;     // fractional phase in [0, 1)

    std::atomic<double> targetAdjustmentPpm_{0.0};
    double currentAdjustmentPpm_ = 0.0;
    double slewRatePpmPerSec_ = 5.0; // 5 ppm/s max rate of change
    double maxSlewPpmPerFrame_ = 5.0 / 48000.0;

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
