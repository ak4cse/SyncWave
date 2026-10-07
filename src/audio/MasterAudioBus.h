#pragma once

#include "AudioFormat.h"
#include "RingBuffer.h"
#include <memory>
#include <cstdint>

namespace syncwave {

class MasterAudioBus {
public:
    explicit MasterAudioBus(const AudioFormat& format = canonicalFormat(), size_t capacityFrames = 48000);
    ~MasterAudioBus();

    MasterAudioBus(const MasterAudioBus&) = delete;
    MasterAudioBus& operator=(const MasterAudioBus&) = delete;
    MasterAudioBus(MasterAudioBus&&) noexcept;
    MasterAudioBus& operator=(MasterAudioBus&&) noexcept;

    // Standard canonical master format: Float32, 48000 Hz, stereo
    static AudioFormat canonicalFormat();

    // Reconfigure format and ring buffer capacity
    void initialize(const AudioFormat& format, size_t capacityFrames);

    // Producer interface: writes PCM audio frames into the master bus
    size_t write(const float* source, size_t frames);

    // Consumer interface: reads PCM audio frames from the master bus
    size_t read(float* destination, size_t frames);

    // Reset buffer state
    void reset();

    // Flush pending unread frames without resetting cumulative written frame counters
    void flush();

    [[nodiscard]] size_t availableFrames() const;
    [[nodiscard]] size_t freeFrames() const;
    [[nodiscard]] size_t capacityFrames() const;
    [[nodiscard]] AudioFormat format() const { return format_; }

    [[nodiscard]] uint64_t totalFramesWritten() const;
    [[nodiscard]] uint64_t totalFramesRead() const;
    [[nodiscard]] uint64_t underruns() const;
    [[nodiscard]] uint64_t overruns() const;

private:
    AudioFormat format_;
    RingBuffer ringBuffer_;
};

} // namespace syncwave
