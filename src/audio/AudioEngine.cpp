#include "AudioEngine.h"
#include "../windows/WasapiCapture.h"
#include <chrono>

namespace syncwave {

AudioEngine::AudioEngine()
    : capture_(std::make_unique<WasapiCapture>()),
      toneGen_(440.0, 0.25),
      masterBus_(MasterAudioBus::canonicalFormat(), 48000) {}

AudioEngine::~AudioEngine() {
    stop();
}

void AudioEngine::producerLoop() {
    constexpr size_t CHUNK_SIZE = 480; // ~10ms chunk @ 48kHz
    const auto busFormat = masterBus_.format();
    std::vector<float> chunk(CHUNK_SIZE * busFormat.channels, 0.0f);
    const auto chunkDuration = std::chrono::microseconds(1000000ULL * CHUNK_SIZE / busFormat.sampleRate);

    auto nextTick = std::chrono::steady_clock::now();
    uint32_t loopTick = 0;

    while (producerRunning_.load(std::memory_order_acquire)) {
        size_t freeSpace = masterBus_.freeFrames();
        if (freeSpace >= CHUNK_SIZE) {
            if (isPulseMode_) {
                pulseGen_.generateFrames(chunk.data(), CHUNK_SIZE, busFormat.channels);
            } else {
                toneGen_.generateFrames(reinterpret_cast<uint8_t*>(chunk.data()), CHUNK_SIZE, busFormat);
            }
            masterBus_.write(chunk.data(), CHUNK_SIZE);
            router_.dispatch(masterBus_);
        }

        if (++loopTick % 10 == 0) {
            router_.sampleAllClocks();
        }

        nextTick += chunkDuration;
        std::this_thread::sleep_until(nextTick);
    }
}

bool AudioEngine::startTone(const AudioDevice& device, const ToneParameters& params) {
    return startTone(std::vector<AudioDevice>{device}, params);
}

bool AudioEngine::startTone(const std::string& deviceId, const ToneParameters& params) {
    return startTone(std::vector<std::string>{deviceId}, params);
}

bool AudioEngine::startTone(const std::vector<std::string>& deviceIds, const ToneParameters& params) {
    std::vector<AudioDevice> devices;
    devices.reserve(deviceIds.size());
    for (const auto& id : deviceIds) {
        AudioDevice dev;
        dev.id = id;
        dev.name = id.empty() ? "Default Audio Endpoint" : ("Device " + id);
        dev.isActive = true;
        devices.push_back(dev);
    }
    return startTone(devices, params);
}

bool AudioEngine::startTone(const std::vector<AudioDevice>& devices, const ToneParameters& params) {
    stop();
    isCaptureMode_ = false;

    if (devices.empty()) {
        return false;
    }

    currentParams_ = params;
    toneGen_.setFrequency(params.frequencyHz);
    toneGen_.setVolume(params.volume);
    toneGen_.resetPhase();

    // Standard canonical master format: 48,000 Hz Float32 stereo
    const auto masterFmt = MasterAudioBus::canonicalFormat();
    masterBus_.initialize(masterFmt, masterFmt.sampleRate);
    masterBus_.reset();

    router_.clearOutputs();
    for (const auto& dev : devices) {
        router_.addOutput(dev);
    }

    if (!router_.initializeOutputs(masterFmt.sampleRate, masterFmt.channels)) {
        router_.closeOutputs();
        return false;
    }

    applyPendingSync();

    // Pre-fill bus with ~100ms of initial audio
    size_t prefillFrames = masterFmt.sampleRate / 10;
    std::vector<float> prefill(prefillFrames * masterFmt.channels);
    toneGen_.generateFrames(reinterpret_cast<uint8_t*>(prefill.data()), prefillFrames, masterFmt);
    masterBus_.write(prefill.data(), prefillFrames);
    router_.dispatch(masterBus_);

    // Start all output render threads
    if (!router_.startOutputs()) {
        stop();
        return false;
    }

    // Launch background producer thread feeding MasterAudioBus
    producerRunning_.store(true, std::memory_order_release);
    producerThread_ = std::thread(&AudioEngine::producerLoop, this);

    return true;
}

bool AudioEngine::startPulse(const std::vector<std::string>& deviceIds, const PulseParameters& params) {
    std::vector<AudioDevice> devices;
    devices.reserve(deviceIds.size());
    for (const auto& id : deviceIds) {
        AudioDevice dev;
        dev.id = id;
        dev.name = id.empty() ? "Default Audio Endpoint" : ("Device " + id);
        dev.isActive = true;
        devices.push_back(dev);
    }
    return startPulse(devices, params);
}

bool AudioEngine::startPulse(const std::vector<AudioDevice>& devices, const PulseParameters& params) {
    stop();
    isCaptureMode_ = false;
    isPulseMode_ = true;

    if (devices.empty()) {
        return false;
    }

    pulseGen_ = SyncPulseGenerator(params);
    pulseGen_.reset();

    const auto masterFmt = MasterAudioBus::canonicalFormat();
    masterBus_.initialize(masterFmt, masterFmt.sampleRate);
    masterBus_.reset();

    router_.clearOutputs();
    for (const auto& dev : devices) {
        router_.addOutput(dev);
    }

    if (!router_.initializeOutputs(masterFmt.sampleRate, masterFmt.channels)) {
        router_.closeOutputs();
        return false;
    }

    applyPendingSync();

    if (!router_.startOutputs()) {
        stop();
        return false;
    }

    producerRunning_.store(true, std::memory_order_release);
    producerThread_ = std::thread(&AudioEngine::producerLoop, this);

    return true;
}

bool AudioEngine::startCapture(const std::string& captureDeviceId, const std::string& outputDeviceId) {
    return startCapture(captureDeviceId, std::vector<std::string>{outputDeviceId});
}

bool AudioEngine::startCapture(const AudioDevice& captureDevice, const AudioDevice& outputDevice) {
    return startCapture(captureDevice, std::vector<AudioDevice>{outputDevice});
}

bool AudioEngine::startCapture(const std::string& captureDeviceId, const std::vector<std::string>& outputDeviceIds) {
    std::vector<AudioDevice> devices;
    devices.reserve(outputDeviceIds.size());
    for (const auto& id : outputDeviceIds) {
        AudioDevice dev;
        dev.id = id;
        dev.name = id.empty() ? "Default Audio Endpoint" : ("Device " + id);
        dev.isActive = true;
        devices.push_back(dev);
    }

    AudioDevice capDev;
    capDev.id = captureDeviceId;
    capDev.name = captureDeviceId.empty() ? "Default Capture Endpoint" : ("Device " + captureDeviceId);
    capDev.isActive = true;

    return startCapture(capDev, devices);
}

bool AudioEngine::startCapture(const AudioDevice& captureDevice, const std::vector<AudioDevice>& outputDevices) {
    stop();
    isCaptureMode_ = true;

    if (outputDevices.empty()) {
        return false;
    }

    // 1. Open and initialize WASAPI loopback capture client
    if (!capture_->open(captureDevice.id)) {
        return false;
    }
    if (!capture_->initialize()) {
        capture_->close();
        return false;
    }

    const auto capFmt = capture_->format();

    // 2. Configure MasterAudioBus: Float32 stereo at capture sample rate with 1.0s capacity
    masterBus_.initialize(capFmt, capFmt.sampleRate);
    masterBus_.reset();

    // 3. Register and initialize outputs in OutputRouter
    router_.clearOutputs();
    for (const auto& dev : outputDevices) {
        router_.addOutput(dev);
    }

    if (!router_.initializeOutputs(capFmt.sampleRate, capFmt.channels)) {
        capture_->close();
        router_.closeOutputs();
        return false;
    }

    applyPendingSync();

    // 4. Start all outputs
    if (!router_.startOutputs()) {
        stop();
        return false;
    }

    // 5. Start capture client pushing into MasterAudioBus and dispatching to OutputRouter
    bool capStarted = capture_->start([this](const float* sourceBuffer, uint32_t frameCount, const AudioFormat& /*format*/) {
        masterBus_.write(sourceBuffer, frameCount);
        router_.dispatch(masterBus_);
    });

    if (!capStarted) {
        stop();
        return false;
    }

    return true;
}

void AudioEngine::onDeviceDisconnected(const std::string& deviceId) {
    router_.onDeviceDisconnected(deviceId);
}

void AudioEngine::sampleClocks() {
    router_.sampleAllClocks();
}

void AudioEngine::stop() {
    sampleClocks();
    lastDiag_ = getDiagnostics();
    lastDiag_.isRunning = false;

    if (producerRunning_.exchange(false, std::memory_order_acq_rel)) {
        if (producerThread_.joinable()) {
            producerThread_.join();
        }
    }

    if (capture_) {
        capture_->stop();
        capture_->close();
    }

    router_.stopOutputs();
    router_.closeOutputs();

    masterBus_.reset();
    isCaptureMode_ = false;
    isPulseMode_ = false;
}

bool AudioEngine::isRunning() const {
    return router_.anyRunning() || (capture_ && capture_->state() == CaptureState::Running);
}

std::vector<PairwiseDriftEstimate> AudioEngine::getPairwiseDriftEstimatesOverWindow(double windowSec) const {
    return router_.getPairwiseDriftEstimatesOverWindow(windowSec);
}

EngineDiagnostics AudioEngine::getDiagnostics() const {
    if (!isRunning() && (lastDiag_.busFramesWritten > 0 || lastDiag_.framesCaptured > 0)) {
        return lastDiag_;
    }

    EngineDiagnostics diag;
    diag.outputs = router_.getOutputTelemetry();
    diag.pairwiseDrift = router_.getPairwiseDriftEstimates();
    diag.routerFramesDistributed = router_.totalFramesDistributed();
    diag.isPulseMode = isPulseMode_;
    diag.isPulseComplete = pulseGen_.isComplete();
    diag.pulseMasterFrameIndex = pulseGen_.pulseMasterFrameIndex();

    // Populate primary output fields for backwards compatibility
    if (!diag.outputs.empty()) {
        const auto& primary = diag.outputs[0];
        diag.deviceName = primary.deviceName;
        diag.deviceId = primary.deviceId;
        diag.format = primary.format;
        diag.bufferFrameCount = primary.bufferFrameCount;
        diag.framesRendered = primary.framesSubmitted;
        diag.outputUnderruns = primary.wasapiUnderruns;
        diag.clockPosition = primary.clockPosition;
        diag.clockFrequency = primary.clockFrequency;
    }

    if (capture_) {
        diag.captureDeviceName = capture_->deviceName();
        diag.captureDeviceId = capture_->deviceId();
        diag.captureFormat = capture_->format();
        diag.framesCaptured = capture_->framesCaptured();
        diag.packetsCaptured = capture_->packetsCaptured();
        diag.silencePackets = capture_->silencePackets();
        diag.discontinuities = capture_->discontinuities();
        diag.captureErrors = capture_->captureErrors();
    }

    diag.masterFormat = masterBus_.format();
    diag.busCapacityFrames = masterBus_.capacityFrames();
    diag.busAvailableFrames = masterBus_.availableFrames();
    diag.busFramesWritten = masterBus_.totalFramesWritten();
    diag.busFramesRead = masterBus_.totalFramesRead();
    diag.latencyModels = router_.getLatencyModels();
    diag.activeSyncPlan = activeSyncPlan_;
    if (!diag.outputs.empty()) {
        diag.syncState = diag.outputs[0].syncState;
    }

    diag.isRunning = isRunning();
    diag.isCaptureMode = isCaptureMode_;

    return diag;
}

void AudioEngine::setManualDelays(const std::vector<double>& delaysMs) {
    autoSyncRequested_ = false;
    pendingManualDelays_ = delaysMs;
    auto models = router_.getLatencyModels();
    activeSyncPlan_ = SyncController::computeManualPlan(models, delaysMs);
    router_.applySyncPlan(activeSyncPlan_);
}

void AudioEngine::setCalibrationOffsets(const std::vector<double>& offsetsMs) {
    pendingCalibrationOffsets_ = offsetsMs;
    for (size_t i = 0; i < offsetsMs.size(); ++i) {
        router_.setDeviceCalibrationOffsetMs(i, offsetsMs[i]);
    }
}

SyncPlan AudioEngine::alignSoftwareLatencies() {
    autoSyncRequested_ = true;
    router_.sampleAllClocks();
    auto models = router_.getLatencyModels();
    activeSyncPlan_ = SyncController::computeSoftwareAlignmentPlan(models);
    if (activeSyncPlan_.isValid) {
        router_.applySyncPlan(activeSyncPlan_);
    }
    return activeSyncPlan_;
}

void AudioEngine::applyPendingSync() {
    for (size_t i = 0; i < pendingCalibrationOffsets_.size(); ++i) {
        router_.setDeviceCalibrationOffsetMs(i, pendingCalibrationOffsets_[i]);
    }

    if (!pendingManualDelays_.empty()) {
        auto models = router_.getLatencyModels();
        activeSyncPlan_ = SyncController::computeManualPlan(models, pendingManualDelays_);
        router_.applySyncPlan(activeSyncPlan_);
    } else if (autoSyncRequested_) {
        activeSyncPlan_ = alignSoftwareLatencies();
    } else {
        router_.setSyncStateAll(SyncState::Disabled);
    }
}

} // namespace syncwave
