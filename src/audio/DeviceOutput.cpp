#include "DeviceOutput.h"
#include <algorithm>

namespace syncwave {

DeviceOutput::DeviceOutput(const AudioDevice& device)
    : device_(device),
      wasapiOutput_(std::make_unique<WasapiOutput>()) {}

DeviceOutput::~DeviceOutput() {
    close();
}

bool DeviceOutput::initialize(uint32_t masterSampleRate, uint32_t masterChannels) {
    close();

    masterSampleRate_ = (masterSampleRate > 0) ? masterSampleRate : 48000;
    masterChannels_ = (masterChannels > 0) ? masterChannels : 2;

    if (!wasapiOutput_->open(device_.id)) {
        return false;
    }

    if (!wasapiOutput_->initialize()) {
        wasapiOutput_->close();
        return false;
    }

    format_ = wasapiOutput_->format();

    // SPSC Queue: 1.0 second buffer at master sample rate
    const size_t queueCapacity = masterSampleRate_;
    queue_ = std::make_unique<RingBuffer>(queueCapacity, masterChannels_);

    // Dedicated output resampler (master sample rate -> device sample rate)
    resampler_ = std::make_unique<Resampler>(masterSampleRate_, format_.sampleRate, masterChannels_);

    framesRouted_.store(0, std::memory_order_relaxed);
    framesConsumed_.store(0, std::memory_order_relaxed);
    framesResampled_.store(0, std::memory_order_relaxed);
    queueUnderruns_.store(0, std::memory_order_relaxed);
    queueOverruns_.store(0, std::memory_order_relaxed);
    isAvailable_.store(true, std::memory_order_release);

    return true;
}

bool DeviceOutput::initializeForTesting(uint32_t masterSampleRate, uint32_t masterChannels, uint32_t deviceSampleRate) {
    close();

    masterSampleRate_ = (masterSampleRate > 0) ? masterSampleRate : 48000;
    masterChannels_ = (masterChannels > 0) ? masterChannels : 2;

    format_.sampleRate = (deviceSampleRate > 0) ? deviceSampleRate : 48000;
    format_.channels = masterChannels_;
    format_.bitsPerSample = 32;
    format_.validBitsPerSample = 32;
    format_.blockAlign = masterChannels_ * sizeof(float);
    format_.sampleType = SampleType::Float32;

    const size_t queueCapacity = masterSampleRate_;
    queue_ = std::make_unique<RingBuffer>(queueCapacity, masterChannels_);
    resampler_ = std::make_unique<Resampler>(masterSampleRate_, format_.sampleRate, masterChannels_);

    framesRouted_.store(0, std::memory_order_relaxed);
    framesConsumed_.store(0, std::memory_order_relaxed);
    framesResampled_.store(0, std::memory_order_relaxed);
    queueUnderruns_.store(0, std::memory_order_relaxed);
    queueOverruns_.store(0, std::memory_order_relaxed);
    isAvailable_.store(true, std::memory_order_release);

    return true;
}

bool DeviceOutput::start() {
    if (!wasapiOutput_ || !queue_) {
        return false;
    }

    bool started = wasapiOutput_->start([this](uint8_t* destinationBuffer, uint32_t frameCount, const AudioFormat& outFormat) {
        float* dest = reinterpret_cast<float*>(destinationBuffer);
        if (masterSampleRate_ == outFormat.sampleRate) {
            size_t readCount = queue_->read(dest, frameCount);
            framesConsumed_.fetch_add(readCount, std::memory_order_relaxed);
            if (readCount < frameCount) {
                queueUnderruns_.fetch_add(frameCount - readCount, std::memory_order_relaxed);
            }
        } else {
            size_t produced = resampler_->pull(*queue_, dest, frameCount);
            framesResampled_.fetch_add(produced, std::memory_order_relaxed);
            if (produced < frameCount) {
                queueUnderruns_.fetch_add(frameCount - produced, std::memory_order_relaxed);
            }
        }
    });

    return started;
}

void DeviceOutput::stop() {
    if (wasapiOutput_) {
        wasapiOutput_->stop();
    }
}

void DeviceOutput::close() {
    stop();
    if (wasapiOutput_) {
        wasapiOutput_->close();
    }
    if (queue_) {
        queue_->reset();
    }
    if (resampler_) {
        resampler_->resetState();
    }
}

size_t DeviceOutput::push(const float* sourceFrames, size_t frameCount) {
    if (!queue_ || !isAvailable()) {
        return 0;
    }

    size_t free = queue_->availableToWrite();
    if (free < frameCount) {
        queueOverruns_.fetch_add(frameCount - free, std::memory_order_relaxed);
    }

    size_t written = queue_->write(sourceFrames, frameCount);
    framesRouted_.fetch_add(written, std::memory_order_relaxed);
    return written;
}

OutputState DeviceOutput::state() const {
    return wasapiOutput_ ? wasapiOutput_->state() : OutputState::Uninitialized;
}

AudioFormat DeviceOutput::format() const {
    return format_;
}

DeviceOutputTelemetry DeviceOutput::getTelemetry() const {
    DeviceOutputTelemetry t;
    t.deviceId = device_.id;
    t.deviceName = device_.name;
    t.state = state();
    t.format = format_;
    t.masterSampleRate = masterSampleRate_;
    t.isAvailable = isAvailable();

    if (wasapiOutput_) {
        t.bufferFrameCount = wasapiOutput_->bufferFrameCount();
        t.framesSubmitted = wasapiOutput_->framesRendered();
        t.wasapiUnderruns = wasapiOutput_->underruns();
        auto clock = wasapiOutput_->getClockPosition();
        t.clockPosition = clock.first;
        t.clockFrequency = clock.second;
    }

    if (queue_) {
        t.queueCapacity = queue_->capacityFrames();
        t.queueAvailable = queue_->availableToRead();
    }

    t.framesRouted = framesRouted_.load(std::memory_order_relaxed);
    t.framesConsumed = framesConsumed_.load(std::memory_order_relaxed);
    t.framesResampled = framesResampled_.load(std::memory_order_relaxed);
    t.queueUnderruns = queueUnderruns_.load(std::memory_order_relaxed);
    t.queueOverruns = queueOverruns_.load(std::memory_order_relaxed);

    return t;
}

} // namespace syncwave
