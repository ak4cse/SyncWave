#include "DeviceOutput.h"
#include <algorithm>

namespace syncwave {

DeviceOutput::DeviceOutput(const AudioDevice& device)
    : device_(device),
      wasapiOutput_(std::make_unique<WasapiOutput>()),
      clock_(device.id, 48000),
      driftEstimator_(5.0, 30, 500.0, 0.15, 0.20),
      driftController_() {}

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

    // Dedicated delay buffer: up to 5.0 seconds at device sample rate
    const size_t delayCapacity = format_.sampleRate * 5;
    delayBuffer_ = std::make_unique<DelayBuffer>(delayCapacity);

    // Preallocated render scratch buffer (8192 frames stereo) to ensure zero allocations in callback
    renderScratch_.assign(8192 * format_.channels, 0.0f);

    clock_ = DeviceClock(device_.id, format_.sampleRate);
    resetDriftCorrection();

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

    const size_t delayCapacity = format_.sampleRate * 5;
    delayBuffer_ = std::make_unique<DelayBuffer>(delayCapacity);
    renderScratch_.assign(8192 * format_.channels, 0.0f);

    clock_ = DeviceClock(device_.id, format_.sampleRate);
    resetDriftCorrection();

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
        const uint32_t channels = outFormat.channels > 0 ? outFormat.channels : 2;

        if (renderScratch_.size() < frameCount * channels) {
            renderScratch_.resize(frameCount * channels, 0.0f);
        }
        float* intermediate = renderScratch_.data();

        if (masterSampleRate_ == outFormat.sampleRate && (!resampler_ || !resampler_->hasRateAdjustment())) {
            size_t readCount = queue_->read(intermediate, frameCount);
            framesConsumed_.fetch_add(readCount, std::memory_order_relaxed);
            if (readCount < frameCount) {
                std::fill(intermediate + (readCount * channels), intermediate + (frameCount * channels), 0.0f);
                queueUnderruns_.fetch_add(frameCount - readCount, std::memory_order_relaxed);
            }
        } else if (resampler_) {
            size_t produced = resampler_->pull(*queue_, intermediate, frameCount);
            framesResampled_.fetch_add(produced, std::memory_order_relaxed);
            if (produced < frameCount) {
                std::fill(intermediate + (produced * channels), intermediate + (frameCount * channels), 0.0f);
                queueUnderruns_.fetch_add(frameCount - produced, std::memory_order_relaxed);
            }
        }

        if (delayBuffer_) {
            delayBuffer_->process(intermediate, dest, frameCount);
        } else {
            std::memcpy(dest, intermediate, frameCount * channels * sizeof(float));
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
    if (delayBuffer_) {
        delayBuffer_->reset();
    }
    resetDriftCorrection();
}

size_t DeviceOutput::push(const float* sourceFrames, size_t frameCount) {
    if (!queue_ || !isAvailable_.load(std::memory_order_relaxed)) {
        return 0;
    }
    size_t written = queue_->write(sourceFrames, frameCount);
    framesRouted_.fetch_add(written, std::memory_order_relaxed);
    if (written < frameCount) {
        queueOverruns_.fetch_add(frameCount - written, std::memory_order_relaxed);
    }
    return written;
}

OutputState DeviceOutput::state() const {
    if (!isAvailable_.load(std::memory_order_relaxed)) {
        return OutputState::Error;
    }
    if (wasapiOutput_) {
        return wasapiOutput_->state();
    }
    return OutputState::Uninitialized;
}

AudioFormat DeviceOutput::format() const {
    return format_;
}

void DeviceOutput::markUnavailable() {
    isAvailable_.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lock(syncErrorMutex_);
    driftController_.setState(DriftCorrectionState::Disconnected);
}

DeviceClockSample DeviceOutput::sampleClock(std::chrono::steady_clock::time_point timestamp) {
    if (wasapiOutput_ && wasapiOutput_->state() == OutputState::Running) {
        auto snap = wasapiOutput_->getClockSnapshot();
        clock_.recordSnapshot(snap, timestamp);
        auto s = clock_.latestSample();
        return s ? *s : DeviceClockSample{};
    }
    auto latest = clock_.latestSample();
    return latest.value_or(DeviceClockSample{});
}

DeviceOutputTelemetry DeviceOutput::getTelemetry() const {
    DeviceOutputTelemetry t;
    t.deviceId = device_.id;
    t.deviceName = device_.name;
    t.state = state();
    t.isAvailable = isAvailable_.load(std::memory_order_relaxed);
    t.format = format_;
    t.masterSampleRate = masterSampleRate_;

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

    t.latencyModel = getLatencyModel();
    t.configuredDelayMs = t.latencyModel.configuredDelayMs;
    t.appliedDelayFrames = t.latencyModel.appliedDelayFrames;
    t.syncState = t.latencyModel.syncState;
    if (resampler_) {
        t.targetRateAdjustmentPpm = resampler_->targetRateAdjustmentPpm();
        t.currentRateAdjustmentPpm = resampler_->currentRateAdjustmentPpm();
    }

    {
        std::lock_guard<std::mutex> lock(syncErrorMutex_);
        t.driftState = driftController_.state();
        t.syncError = latestSyncError_;
        t.rawDriftPpm = latestSyncError_.rawDriftPpm;
        t.filteredDriftPpm = latestSyncError_.filteredDriftPpm;
        t.phaseErrorMs = latestSyncError_.phaseErrorMs;
        t.filteredPhaseErrorMs = latestSyncError_.filteredPhaseErrorMs;
        t.driftConfidence = latestSyncError_.confidence;
    }

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

void DeviceOutput::setDelayMs(double delayMs) {
    if (delayBuffer_) {
        uint32_t sRate = format_.sampleRate > 0 ? format_.sampleRate : 48000;
        delayBuffer_->setDelayMs(delayMs, sRate);
        syncState_.store(SyncState::Manual, std::memory_order_relaxed);
    }
}

void DeviceOutput::setDelayFrames(size_t frames) {
    if (delayBuffer_) {
        delayBuffer_->setDelayFrames(frames);
        syncState_.store(SyncState::Manual, std::memory_order_relaxed);
    }
}

double DeviceOutput::configuredDelayMs() const {
    if (delayBuffer_) {
        uint32_t sRate = format_.sampleRate > 0 ? format_.sampleRate : 48000;
        return delayBuffer_->delayMs(sRate);
    }
    return 0.0;
}

size_t DeviceOutput::configuredDelayFrames() const {
    return delayBuffer_ ? delayBuffer_->delayFrames() : 0;
}

void DeviceOutput::setCalibrationOffsetMs(double offsetMs) {
    calibrationOffsetMs_.store(offsetMs, std::memory_order_relaxed);
}

double DeviceOutput::calibrationOffsetMs() const {
    return calibrationOffsetMs_.load(std::memory_order_relaxed);
}

void DeviceOutput::setSyncState(SyncState state) {
    syncState_.store(state, std::memory_order_relaxed);
}

SyncState DeviceOutput::syncState() const {
    return syncState_.load(std::memory_order_relaxed);
}

OutputLatencyModel DeviceOutput::getLatencyModel() const {
    OutputLatencyModel model;
    model.deviceId = device_.id;
    model.deviceName = device_.name;
    model.sampleRate = format_.sampleRate > 0 ? format_.sampleRate : 48000;

    auto snap = clock_.latestSample();
    if (snap) {
        model.wasapiStreamLatencyMs = snap->streamLatencyMs();
        model.wasapiPaddingMs = snap->paddingMs();
    }
    if (queue_) {
        size_t avail = queue_->availableToRead();
        model.queueLatencyMs = (masterSampleRate_ > 0) ? (static_cast<double>(avail) * 1000.0 / masterSampleRate_) : 0.0;
    }
    if (resampler_ && masterSampleRate_ != format_.sampleRate) {
        model.resamplerLatencyMs = (format_.sampleRate > 0) ? (0.5 * 1000.0 / format_.sampleRate) : 0.0;
    }
    if (delayBuffer_) {
        model.configuredDelayMs = delayBuffer_->delayMs(model.sampleRate);
        model.appliedDelayFrames = delayBuffer_->delayFrames();
    }
    model.optionalCalibrationOffsetMs = calibrationOffsetMs_.load(std::memory_order_relaxed);
    model.syncState = syncState_.load(std::memory_order_relaxed);
    model.updateTotals();
    return model;
}

void DeviceOutput::setRateAdjustmentPpm(double ppm, bool immediate) {
    if (resampler_) {
        resampler_->setRateAdjustmentPpm(ppm, immediate);
    }
}

double DeviceOutput::rateAdjustmentPpm() const {
    return resampler_ ? resampler_->targetRateAdjustmentPpm() : 0.0;
}

double DeviceOutput::currentRateAdjustmentPpm() const {
    return resampler_ ? resampler_->currentRateAdjustmentPpm() : 0.0;
}

bool DeviceOutput::hasRateAdjustment() const {
    return resampler_ ? resampler_->hasRateAdjustment() : false;
}

void DeviceOutput::setDriftCorrectionEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(syncErrorMutex_);
    driftController_.setEnabled(enabled);
    if (!enabled) {
        setRateAdjustmentPpm(0.0, true);
    }
}

bool DeviceOutput::isDriftCorrectionEnabled() const {
    std::lock_guard<std::mutex> lock(syncErrorMutex_);
    return driftController_.isEnabled();
}

void DeviceOutput::setDriftControllerConfig(const DriftControllerConfig& config) {
    std::lock_guard<std::mutex> lock(syncErrorMutex_);
    driftController_.setConfig(config);
    if (resampler_) {
        resampler_->setSlewRatePpmPerSecond(config.maxSlewRatePpmPerSec);
    }
}

const DriftControllerConfig& DeviceOutput::driftControllerConfig() const {
    std::lock_guard<std::mutex> lock(syncErrorMutex_);
    return driftController_.config();
}

DriftCorrectionState DeviceOutput::driftCorrectionState() const {
    std::lock_guard<std::mutex> lock(syncErrorMutex_);
    return driftController_.state();
}

SyncError DeviceOutput::latestSyncError() const {
    std::lock_guard<std::mutex> lock(syncErrorMutex_);
    return latestSyncError_;
}

DriftCorrectionOutput DeviceOutput::latestDriftCorrection() const {
    std::lock_guard<std::mutex> lock(syncErrorMutex_);
    return latestCorrection_;
}

void DeviceOutput::resetDriftCorrection() {
    std::lock_guard<std::mutex> lock(syncErrorMutex_);
    driftEstimator_.reset();
    driftController_.reset();
    latestSyncError_ = SyncError{};
    latestCorrection_ = DriftCorrectionOutput{};
    setRateAdjustmentPpm(0.0, true);
}

void DeviceOutput::updateDriftCorrection(double masterTimelineSec, uint64_t masterTimelineFrames, double targetLatencySec) {
    if (!isAvailable_.load(std::memory_order_relaxed)) {
        std::lock_guard<std::mutex> lock(syncErrorMutex_);
        driftController_.setState(DriftCorrectionState::Disconnected);
        return;
    }

    double outPlayheadSec = wasapiClockPlayheadSeconds();
    if (outPlayheadSec <= 0.0) {
        outPlayheadSec = estimatedAppPlayheadSeconds();
    }

    double targetSec = 0.0;
    if (targetLatencySec >= 0.0) {
        targetSec = masterTimelineSec - targetLatencySec;
    } else {
        auto model = getLatencyModel();
        targetSec = masterTimelineSec - (model.effectiveLatencyMs / 1000.0);
    }
    if (targetSec < 0.0) {
        targetSec = 0.0;
    }

    std::lock_guard<std::mutex> lock(syncErrorMutex_);
    latestSyncError_ = driftEstimator_.updateSyncError(
        device_.id,
        device_.name,
        clock_,
        masterTimelineSec,
        masterTimelineFrames,
        targetSec,
        outPlayheadSec,
        masterSampleRate_);

    if (driftController_.isEnabled()) {
        latestCorrection_ = driftController_.calculateCorrection(latestSyncError_, isAvailable());
        setRateAdjustmentPpm(latestCorrection_.targetRateAdjustmentPpm);
    }
}

} // namespace syncwave
