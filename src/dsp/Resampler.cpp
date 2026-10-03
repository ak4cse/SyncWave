#include "Resampler.h"
#include "../audio/MasterAudioBus.h"
#include "../audio/RingBuffer.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace syncwave {

Resampler::Resampler(uint32_t inSampleRate, uint32_t outSampleRate, uint32_t channels) {
    reset(inSampleRate, outSampleRate, channels);
}

void Resampler::reset(uint32_t inSampleRate, uint32_t outSampleRate, uint32_t channels) {
    inRate_ = (inSampleRate > 0) ? inSampleRate : 48000;
    outRate_ = (outSampleRate > 0) ? outSampleRate : 48000;
    channels_ = (channels > 0) ? channels : 2;
    ratio_ = static_cast<double>(inRate_) / static_cast<double>(outRate_);

    lastSample_.assign(channels_, 0.0f);
    hasLastSample_ = false;
    phase_ = 0.0;

    // FIFO capacity: 16384 frames (~350ms of audio buffer)
    constexpr size_t FIFO_FRAMES = 16384;
    fifoBuffer_.assign(FIFO_FRAMES * channels_, 0.0f);
    fifoReadIndex_ = 0;
    fifoWriteIndex_ = 0;
    fifoCount_ = 0;

    busChunkBuffer_.assign(512 * channels_, 0.0f);
    resampledChunkBuffer_.assign(1024 * channels_, 0.0f);
}

void Resampler::resetState() {
    std::fill(lastSample_.begin(), lastSample_.end(), 0.0f);
    hasLastSample_ = false;
    phase_ = 0.0;
    fifoReadIndex_ = 0;
    fifoWriteIndex_ = 0;
    fifoCount_ = 0;
}

size_t Resampler::process(const float* inBuffer, size_t inFrames, float* outBuffer, size_t maxOutFrames) {
    if (!inBuffer || inFrames == 0 || !outBuffer || maxOutFrames == 0) {
        return 0;
    }

    if (inRate_ == outRate_) {
        size_t framesToCopy = std::min(inFrames, maxOutFrames);
        std::memcpy(outBuffer, inBuffer, framesToCopy * channels_ * sizeof(float));
        return framesToCopy;
    }

    const double M = static_cast<double>(inFrames);
    double pos = phase_;
    size_t outIdx = 0;

    while (outIdx < maxOutFrames) {
        if (pos < 0.0) {
            // Interpolate between lastSample_ (at t = -1) and inBuffer[0] (at t = 0)
            const float alpha = static_cast<float>(pos + 1.0);
            const float oneMinusAlpha = 1.0f - alpha;

            for (uint32_t c = 0; c < channels_; ++c) {
                const float s0 = hasLastSample_ ? lastSample_[c] : inBuffer[c];
                const float s1 = inBuffer[c];
                outBuffer[outIdx * channels_ + c] = s0 * oneMinusAlpha + s1 * alpha;
            }
            pos += ratio_;
            ++outIdx;
        } else if (pos + 1.0 < M) {
            // Interpolate within current inBuffer
            const size_t idx = static_cast<size_t>(pos);
            const float alpha = static_cast<float>(pos - static_cast<double>(idx));
            const float oneMinusAlpha = 1.0f - alpha;

            for (uint32_t c = 0; c < channels_; ++c) {
                const float s0 = inBuffer[idx * channels_ + c];
                const float s1 = inBuffer[(idx + 1) * channels_ + c];
                outBuffer[outIdx * channels_ + c] = s0 * oneMinusAlpha + s1 * alpha;
            }
            pos += ratio_;
            ++outIdx;
        } else {
            // Need next chunk to continue
            break;
        }
    }

    // Save boundary state for next chunk
    if (inFrames > 0) {
        const size_t lastFrameOffset = (inFrames - 1) * channels_;
        for (uint32_t c = 0; c < channels_; ++c) {
            lastSample_[c] = inBuffer[lastFrameOffset + c];
        }
        hasLastSample_ = true;
    }
    phase_ = pos - M;

    return outIdx;
}

void Resampler::fifoPush(const float* data, size_t frames) {
    const size_t fifoCapacityFrames = fifoBuffer_.size() / channels_;
    const size_t framesToWrite = std::min(frames, fifoCapacityFrames - fifoCount_);

    for (size_t i = 0; i < framesToWrite; ++i) {
        const size_t destOffset = fifoWriteIndex_ * channels_;
        const size_t srcOffset = i * channels_;
        for (uint32_t c = 0; c < channels_; ++c) {
            fifoBuffer_[destOffset + c] = data[srcOffset + c];
        }
        fifoWriteIndex_ = (fifoWriteIndex_ + 1) % fifoCapacityFrames;
    }
    fifoCount_ += framesToWrite;
}

size_t Resampler::fifoPop(float* dest, size_t frames) {
    const size_t fifoCapacityFrames = fifoBuffer_.size() / channels_;
    const size_t framesToRead = std::min(frames, fifoCount_);

    for (size_t i = 0; i < framesToRead; ++i) {
        const size_t srcOffset = fifoReadIndex_ * channels_;
        const size_t destOffset = i * channels_;
        for (uint32_t c = 0; c < channels_; ++c) {
            dest[destOffset + c] = fifoBuffer_[srcOffset + c];
        }
        fifoReadIndex_ = (fifoReadIndex_ + 1) % fifoCapacityFrames;
    }
    fifoCount_ -= framesToRead;

    // Fill remaining requested frames with silence
    if (framesToRead < frames) {
        std::memset(dest + framesToRead * channels_, 0, (frames - framesToRead) * channels_ * sizeof(float));
    }

    return framesToRead;
}

size_t Resampler::pull(RingBuffer& queue, float* outBuffer, size_t outFrames) {
    if (!outBuffer || outFrames == 0) {
        return 0;
    }

    if (inRate_ == outRate_) {
        return queue.read(outBuffer, outFrames);
    }

    constexpr size_t IN_CHUNK_FRAMES = 512;

    // Pull from queue and resample until we have enough frames in FIFO to satisfy outFrames
    while (fifoCount_ < outFrames && queue.availableToRead() > 0) {
        size_t framesToRead = std::min(IN_CHUNK_FRAMES, queue.availableToRead());
        size_t readCount = queue.read(busChunkBuffer_.data(), framesToRead);
        if (readCount == 0) break;

        size_t maxResampled = resampledChunkBuffer_.size() / channels_;
        size_t produced = process(busChunkBuffer_.data(), readCount, resampledChunkBuffer_.data(), maxResampled);
        if (produced > 0) {
            fifoPush(resampledChunkBuffer_.data(), produced);
        }
    }

    return fifoPop(outBuffer, outFrames);
}

size_t Resampler::pull(MasterAudioBus& bus, float* outBuffer, size_t outFrames) {
    if (!outBuffer || outFrames == 0) {
        return 0;
    }

    if (inRate_ == outRate_) {
        return bus.read(outBuffer, outFrames);
    }

    constexpr size_t IN_CHUNK_FRAMES = 512;

    // Pull from bus and resample until we have enough frames in FIFO to satisfy outFrames
    while (fifoCount_ < outFrames && bus.availableFrames() > 0) {
        size_t framesToRead = std::min(IN_CHUNK_FRAMES, bus.availableFrames());
        size_t readCount = bus.read(busChunkBuffer_.data(), framesToRead);
        if (readCount == 0) break;

        size_t maxResampled = resampledChunkBuffer_.size() / channels_;
        size_t produced = process(busChunkBuffer_.data(), readCount, resampledChunkBuffer_.data(), maxResampled);
        if (produced > 0) {
            fifoPush(resampledChunkBuffer_.data(), produced);
        }
    }

    return fifoPop(outBuffer, outFrames);
}

} // namespace syncwave
