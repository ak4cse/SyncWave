#include "DeviceOutput.h"
#include <algorithm>

namespace syncwave {

DeviceOutput::DeviceOutput(const AudioDevice& device)
    : device_(device),
      wasapiOutput_(std::make_unique<WasapiOutput>()),
      clock_(device.id, 48000) {}

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

    clock_ = DeviceClock(device_.id, format_.sampleRate);

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

    clock_ = DeviceClock(device_.id, format_.sampleRate);

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

DeviceClockSample DeviceOutput::sampleClock(std::chrono::steady_clock::time_point timestamp) {
    if (wasapiOutput_) {
        auto snap = wasapiOutput_->getClockSnapshot();
        clock_.recordSnapshot(snap, timestamp);
    }
    auto latest = clock_.latestSample();
    return latest.value_or(DeviceClockSample{});
}

DeviceOutputTelemetry DeviceOutput::getTelemetry() const {
    DeviceOutputTelemetry t;
    t.deviceId = device_.id;
    t.deviceName = device_.name;
    t.state = state();
    t.format = format_;
    t.masterSampleRate = masterSampleRate_;
    t.isAvailable = isAvailable();

    if (wasapiOutput_ && wasapiOutput_->state() == OutputState::Running) {
        t.bufferFrameCount = wasapiOutput_->bufferFrameCount();
        t.framesSubmitted = wasapiOutput_->framesRendered();
        t.wasapiUnderruns = wasapiOutput_->underruns();
        auto snap = wasapiOutput_->getClockSnapshot();
        if (snap.isValid) {
            t.clockPosition = snap.position;
            t.clockFrequency = snap.frequency;
            t.currentPadding = snap.currentPadding;
            t.streamLatencyMs = snap.streamLatencyHns / 10000.0;
        }
    } else {
        auto latest = clock_.latestSample();
        if (latest && latest->isValid) {
            t.clockPosition = latest->clockPosition;
            t.clockFrequency = latest->clockFrequency;
            t.currentPadding = latest->currentPadding;
            t.streamLatencyMs = latest->streamLatencyMs();
            t.bufferFrameCount = latest->bufferFrameCount;
        }
    }

    auto rateEst = clock_.estimateRate();
    if (rateEst.isValid) {
        t.estimatedClockRateHz = rateEst.estimatedRate;
        t.rateErrorPpm = rateEst.rateErrorPpm;
        t.measurementDurationSec = rateEst.measurementDurationSec;
        t.clockSampleCount = rateEst.sampleCount;
    } else {
        t.clockSampleCount = clock_.sampleCount();
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

    // Application-level estimated playhead (frames submitted - current padding)
    uint64_t submitted = t.framesSubmitted;
    uint32_t pad = t.currentPadding;
    t.estimatedAppPlayheadFrames = (submitted > pad) ? (submitted - pad) : 0;
    t.estimatedAppPlayheadSec = (format_.sampleRate > 0)
                                    ? (static_cast<double>(t.estimatedAppPlayheadFrames) / format_.sampleRate)
                                    : 0.0;

    // Hardware clock playhead (ticks / clock frequency)
    t.wasapiClockPlayheadSec = (t.clockFrequency > 0)
                                   ? (static_cast<double>(t.clockPosition) / static_cast<double>(t.clockFrequency))
                                   : 0.0;

    // Discrepancy between application and hardware clock playheads
    t.playheadDiscrepancyMs = (t.estimatedAppPlayheadSec - t.wasapiClockPlayheadSec) * 1000.0;

    // Master timeline position for this output
    uint64_t routed = t.framesRouted;
    uint64_t qAvail = t.queueAvailable;
    t.masterTimelineFrames = (routed > qAvail) ? (routed - qAvail) : 0;
    t.masterTimelineSec = (masterSampleRate_ > 0)
                              ? (static_cast<double>(t.masterTimelineFrames) / masterSampleRate_)
                              : 0.0;

    return t;
}

uint64_t DeviceOutput::estimatedAppPlayheadFrames() const {
    uint64_t submitted = wasapiOutput_ ? wasapiOutput_->framesRendered() : 0;
    uint32_t pad = 0;
    if (wasapiOutput_) {
        auto snap = wasapiOutput_->getClockSnapshot();
        pad = snap.currentPadding;
    } else {
        auto latest = clock_.latestSample();
        if (latest) {
            pad = latest->currentPadding;
            submitted = latest->framesRendered;
        }
    }
    return (submitted > pad) ? (submitted - pad) : 0;
}

double DeviceOutput::estimatedAppPlayheadSeconds() const {
    if (format_.sampleRate == 0) return 0.0;
    return static_cast<double>(estimatedAppPlayheadFrames()) / static_cast<double>(format_.sampleRate);
}

double DeviceOutput::wasapiClockPlayheadSeconds() const {
    if (wasapiOutput_) {
        auto snap = wasapiOutput_->getClockSnapshot();
        if (snap.isValid && snap.frequency > 0) {
            return static_cast<double>(snap.position) / static_cast<double>(snap.frequency);
        }
    }
    auto latest = clock_.latestSample();
    if (latest && latest->isValid) {
        return latest->positionSeconds();
    }
    return 0.0;
}

double DeviceOutput::playheadDiscrepancyMs() const {
    return (estimatedAppPlayheadSeconds() - wasapiClockPlayheadSeconds()) * 1000.0;
}

uint64_t DeviceOutput::masterTimelineFrames() const {
    uint64_t routed = framesRouted_.load(std::memory_order_relaxed);
    uint64_t qAvail = queue_ ? queue_->availableToRead() : 0;
    return (routed > qAvail) ? (routed - qAvail) : 0;
}

double DeviceOutput::masterTimelineSeconds() const {
    if (masterSampleRate_ == 0) return 0.0;
    return static_cast<double>(masterTimelineFrames()) / static_cast<double>(masterSampleRate_);
}

} // namespace syncwave
