#pragma once

#include <vector>
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <cmath>

namespace syncwave {

class DelayBuffer {
public:
    // Construct a delay buffer with maximum capacity in frames (default 5.0s @ 48 kHz = 240,000 frames)
    explicit DelayBuffer(size_t maxCapacityFrames = 240000);
    ~DelayBuffer() = default;

    DelayBuffer(const DelayBuffer&) = delete;
    DelayBuffer& operator=(const DelayBuffer&) = delete;
    DelayBuffer(DelayBuffer&&) noexcept;
    DelayBuffer& operator=(DelayBuffer&&) noexcept;

    // Reset buffer contents to silence and reset circular pointers
    void reset();

    // Configure delay in audio frames (atomic, thread-safe for audio thread)
    void setDelayFrames(size_t frames);

    // Configure delay in milliseconds based on sample rate
    void setDelayMs(double delayMs, uint32_t sampleRate);

    [[nodiscard]] size_t delayFrames() const;
    [[nodiscard]] double delayMs(uint32_t sampleRate) const;
    [[nodiscard]] size_t maxCapacityFrames() const;

    // Process a block of interleaved stereo Float32 frames.
    // If delay == 0: passes through input to output immediately.
    // If delay > 0: outputs samples from D frames ago, and stores input into circular history.
    // Real-time safe: strictly no heap allocations, no locks, and constant-time execution.
    void process(const float* input, float* output, size_t frameCount);

private:
    size_t maxCapacityFrames_ = 240000;
    std::atomic<size_t> delayFrames_{0};
    std::vector<float> buffer_; // Interleaved stereo buffer of size maxCapacityFrames_ * 2
    size_t writePos_ = 0;       // Current write position in frames
};

} // namespace syncwave
