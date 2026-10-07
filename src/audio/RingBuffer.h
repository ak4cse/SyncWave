#pragma once

#include <vector>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <algorithm>

namespace syncwave {

class RingBuffer {
public:
    explicit RingBuffer(size_t capacityFrames = 48000, uint16_t channels = 2);
    ~RingBuffer();

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&&) noexcept;
    RingBuffer& operator=(RingBuffer&&) noexcept;

    // Reallocate storage and reset indices
    void initialize(size_t capacityFrames, uint16_t channels);

    // Single-producer write: writes up to available capacity, returns frames written.
    // If frames > available space, excess frames are dropped and recorded as overruns.
    size_t write(const float* source, size_t frames);

    // Single-consumer read: reads up to available frames, returns frames actually read.
    // If frames > available, remaining buffer is padded with silence (zeros) and recorded as underruns.
    size_t read(float* destination, size_t frames);

    // Reset read and write pointers, clearing the buffer
    void reset();

    // Flush pending unread frames without resetting monotonic total counters
    void flush();

    [[nodiscard]] size_t availableToRead() const;
    [[nodiscard]] size_t availableToWrite() const;
    [[nodiscard]] size_t capacityFrames() const { return capacityFrames_; }
    [[nodiscard]] uint16_t channels() const { return channels_; }

    [[nodiscard]] uint64_t totalFramesWritten() const { return totalFramesWritten_.load(std::memory_order_relaxed); }
    [[nodiscard]] uint64_t totalFramesRead() const { return totalFramesRead_.load(std::memory_order_relaxed); }
    [[nodiscard]] uint64_t underruns() const { return underruns_.load(std::memory_order_relaxed); }
    [[nodiscard]] uint64_t overruns() const { return overruns_.load(std::memory_order_relaxed); }

private:
    size_t capacityFrames_ = 0;
    uint16_t channels_ = 2;
    std::vector<float> buffer_;

    // Monotonic indices with cache-line alignment to eliminate false sharing
    alignas(64) std::atomic<size_t> writeIndex_{0};
    alignas(64) std::atomic<size_t> readIndex_{0};

    std::atomic<uint64_t> totalFramesWritten_{0};
    std::atomic<uint64_t> totalFramesRead_{0};
    std::atomic<uint64_t> underruns_{0};
    std::atomic<uint64_t> overruns_{0};
};

} // namespace syncwave
