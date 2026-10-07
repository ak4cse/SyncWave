#include "RingBuffer.h"

namespace syncwave {

RingBuffer::RingBuffer(size_t capacityFrames, uint16_t channels) {
    initialize(capacityFrames, channels);
}

RingBuffer::~RingBuffer() = default;

RingBuffer::RingBuffer(RingBuffer&& other) noexcept {
    *this = std::move(other);
}

RingBuffer& RingBuffer::operator=(RingBuffer&& other) noexcept {
    if (this != &other) {
        capacityFrames_ = other.capacityFrames_;
        channels_ = other.channels_;
        buffer_ = std::move(other.buffer_);
        writeIndex_.store(other.writeIndex_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        readIndex_.store(other.readIndex_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        totalFramesWritten_.store(other.totalFramesWritten_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        totalFramesRead_.store(other.totalFramesRead_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        underruns_.store(other.underruns_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        overruns_.store(other.overruns_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    return *this;
}

void RingBuffer::initialize(size_t capacityFrames, uint16_t channels) {
    capacityFrames_ = (capacityFrames > 0) ? capacityFrames : 48000;
    channels_ = (channels > 0) ? channels : 2;
    buffer_.assign(capacityFrames_ * channels_, 0.0f);
    reset();
}

void RingBuffer::reset() {
    writeIndex_.store(0, std::memory_order_release);
    readIndex_.store(0, std::memory_order_release);
    totalFramesWritten_.store(0, std::memory_order_relaxed);
    totalFramesRead_.store(0, std::memory_order_relaxed);
    underruns_.store(0, std::memory_order_relaxed);
    overruns_.store(0, std::memory_order_relaxed);
    if (!buffer_.empty()) {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    }
}

void RingBuffer::flush() {
    size_t w = writeIndex_.load(std::memory_order_acquire);
    readIndex_.store(w, std::memory_order_release);
}

size_t RingBuffer::availableToRead() const {
    size_t w = writeIndex_.load(std::memory_order_acquire);
    size_t r = readIndex_.load(std::memory_order_relaxed);
    return (w >= r) ? (w - r) : 0;
}

size_t RingBuffer::availableToWrite() const {
    size_t inUse = availableToRead();
    return (capacityFrames_ > inUse) ? (capacityFrames_ - inUse) : 0;
}

size_t RingBuffer::write(const float* source, size_t frames) {
    if (!source || frames == 0 || capacityFrames_ == 0) {
        return 0;
    }

    size_t w = writeIndex_.load(std::memory_order_relaxed);
    size_t r = readIndex_.load(std::memory_order_acquire);
    size_t inUse = (w >= r) ? (w - r) : 0;
    size_t freeSpace = (capacityFrames_ > inUse) ? (capacityFrames_ - inUse) : 0;

    size_t toWrite = std::min(frames, freeSpace);
    if (toWrite < frames) {
        overruns_.fetch_add(frames - toWrite, std::memory_order_relaxed);
    }

    if (toWrite == 0) {
        return 0;
    }

    size_t writeOffset = w % capacityFrames_;
    size_t chunk1 = std::min(toWrite, capacityFrames_ - writeOffset);
    size_t chunk2 = toWrite - chunk1;

    std::memcpy(&buffer_[writeOffset * channels_], source, chunk1 * channels_ * sizeof(float));

    if (chunk2 > 0) {
        std::memcpy(&buffer_[0], source + (chunk1 * channels_), chunk2 * channels_ * sizeof(float));
    }

    writeIndex_.store(w + toWrite, std::memory_order_release);
    totalFramesWritten_.fetch_add(toWrite, std::memory_order_relaxed);
    return toWrite;
}

size_t RingBuffer::read(float* destination, size_t frames) {
    if (!destination || frames == 0 || capacityFrames_ == 0) {
        return 0;
    }

    size_t w = writeIndex_.load(std::memory_order_acquire);
    size_t r = readIndex_.load(std::memory_order_relaxed);
    size_t available = (w >= r) ? (w - r) : 0;

    size_t toRead = std::min(frames, available);

    if (toRead > 0) {
        size_t readOffset = r % capacityFrames_;
        size_t chunk1 = std::min(toRead, capacityFrames_ - readOffset);
        size_t chunk2 = toRead - chunk1;

        std::memcpy(destination, &buffer_[readOffset * channels_], chunk1 * channels_ * sizeof(float));

        if (chunk2 > 0) {
            std::memcpy(destination + (chunk1 * channels_), &buffer_[0], chunk2 * channels_ * sizeof(float));
        }

        readIndex_.store(r + toRead, std::memory_order_release);
        totalFramesRead_.fetch_add(toRead, std::memory_order_relaxed);
    }

    if (toRead < frames) {
        size_t missing = frames - toRead;
        std::memset(destination + (toRead * channels_), 0, missing * channels_ * sizeof(float));
        underruns_.fetch_add(missing, std::memory_order_relaxed);
    }

    return toRead;
}

} // namespace syncwave
