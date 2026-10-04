#include "DelayBuffer.h"
#include <algorithm>
#include <cstring>

namespace syncwave {

DelayBuffer::DelayBuffer(size_t maxCapacityFrames)
    : maxCapacityFrames_(std::max<size_t>(1024, maxCapacityFrames)),
      buffer_(maxCapacityFrames_ * 2, 0.0f),
      writePos_(0) {
    delayFrames_.store(0, std::memory_order_relaxed);
}

DelayBuffer::DelayBuffer(DelayBuffer&& other) noexcept
    : maxCapacityFrames_(other.maxCapacityFrames_),
      delayFrames_(other.delayFrames_.load(std::memory_order_relaxed)),
      buffer_(std::move(other.buffer_)),
      writePos_(other.writePos_) {
    other.maxCapacityFrames_ = 0;
    other.delayFrames_.store(0, std::memory_order_relaxed);
    other.writePos_ = 0;
}

DelayBuffer& DelayBuffer::operator=(DelayBuffer&& other) noexcept {
    if (this != &other) {
        maxCapacityFrames_ = other.maxCapacityFrames_;
        delayFrames_.store(other.delayFrames_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        buffer_ = std::move(other.buffer_);
        writePos_ = other.writePos_;

        other.maxCapacityFrames_ = 0;
        other.delayFrames_.store(0, std::memory_order_relaxed);
        other.writePos_ = 0;
    }
    return *this;
}

void DelayBuffer::reset() {
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    writePos_ = 0;
}

void DelayBuffer::setDelayFrames(size_t frames) {
    // Clamp to available history capacity - 1
    size_t limit = (maxCapacityFrames_ > 1) ? (maxCapacityFrames_ - 1) : 0;
    size_t clamped = std::min(frames, limit);
    delayFrames_.store(clamped, std::memory_order_release);
}

void DelayBuffer::setDelayMs(double delayMs, uint32_t sampleRate) {
    if (sampleRate == 0 || delayMs <= 0.0) {
        setDelayFrames(0);
        return;
    }
    double framesExact = (delayMs * static_cast<double>(sampleRate)) / 1000.0;
    size_t frames = static_cast<size_t>(std::round(framesExact));
    setDelayFrames(frames);
}

size_t DelayBuffer::delayFrames() const {
    return delayFrames_.load(std::memory_order_acquire);
}

double DelayBuffer::delayMs(uint32_t sampleRate) const {
    if (sampleRate == 0) {
        return 0.0;
    }
    return (static_cast<double>(delayFrames()) * 1000.0) / static_cast<double>(sampleRate);
}

size_t DelayBuffer::maxCapacityFrames() const {
    return maxCapacityFrames_;
}

void DelayBuffer::process(const float* input, float* output, size_t frameCount) {
    if (!input || !output || frameCount == 0 || maxCapacityFrames_ == 0) {
        return;
    }

    const size_t d = delayFrames_.load(std::memory_order_acquire);

    // Fast-path: 0 delay is pure passthrough
    if (d == 0) {
        if (output != input) {
            std::memcpy(output, input, frameCount * 2 * sizeof(float));
        }
        return;
    }

    const size_t cap = maxCapacityFrames_;
    const size_t maskCap = cap; // modulo base

    for (size_t i = 0; i < frameCount; ++i) {
        // Read sample from d frames ago in the circular buffer
        size_t readPos = (writePos_ + cap - d) % maskCap;

        output[i * 2 + 0] = buffer_[readPos * 2 + 0];
        output[i * 2 + 1] = buffer_[readPos * 2 + 1];

        // Store new input sample into circular history
        buffer_[writePos_ * 2 + 0] = input[i * 2 + 0];
        buffer_[writePos_ * 2 + 1] = input[i * 2 + 1];

        writePos_ = (writePos_ + 1) % maskCap;
    }
}

} // namespace syncwave
