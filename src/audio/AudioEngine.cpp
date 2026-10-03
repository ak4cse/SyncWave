#include "AudioEngine.h"
#include "../windows/WasapiOutput.h"
#include "../windows/WasapiCapture.h"
#include <chrono>

namespace syncwave {

AudioEngine::AudioEngine()
    : output_(std::make_unique<WasapiOutput>()),
      capture_(std::make_unique<WasapiCapture>()),
      toneGen_(440.0, 0.25),
      masterBus_(MasterAudioBus::canonicalFormat(), 48000),
      resampler_(48000, 48000, 2) {}

AudioEngine::~AudioEngine() {
    stop();
}

void AudioEngine::producerLoop() {
    constexpr size_t CHUNK_SIZE = 480; // ~10ms chunk
    const auto busFormat = masterBus_.format();
    std::vector<float> chunk(CHUNK_SIZE * busFormat.channels, 0.0f);

    while (producerRunning_.load(std::memory_order_acquire)) {
        size_t freeSpace = masterBus_.freeFrames();
        if (freeSpace >= CHUNK_SIZE) {
            toneGen_.generateFrames(reinterpret_cast<uint8_t*>(chunk.data()), CHUNK_SIZE, busFormat);
            masterBus_.write(chunk.data(), CHUNK_SIZE);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
}

bool AudioEngine::startTone(const AudioDevice& device, const ToneParameters& params) {
    return startTone(device.id, params);
}

bool AudioEngine::startTone(const std::string& deviceId, const ToneParameters& params) {
    stop();
    isCaptureMode_ = false;

    currentParams_ = params;
    toneGen_.setFrequency(params.frequencyHz);
    toneGen_.setVolume(params.volume);
    toneGen_.resetPhase();

    if (!output_->open(deviceId)) {
        return false;
    }

    if (!output_->initialize()) {
        output_->close();
        return false;
    }

    const auto outFmt = output_->format();
    masterBus_.initialize(outFmt, outFmt.sampleRate);
    masterBus_.reset();

    // Pre-fill bus with ~100ms of initial audio
    size_t prefillFrames = outFmt.sampleRate / 10;
    std::vector<float> prefill(prefillFrames * outFmt.channels);
    toneGen_.generateFrames(reinterpret_cast<uint8_t*>(prefill.data()), prefillFrames, outFmt);
    masterBus_.write(prefill.data(), prefillFrames);

    // Launch background producer thread feeding MasterAudioBus
    producerRunning_.store(true, std::memory_order_release);
    producerThread_ = std::thread(&AudioEngine::producerLoop, this);

    // Start WASAPI output reading directly from MasterAudioBus
    bool started = output_->start([this](uint8_t* destinationBuffer, uint32_t frameCount, const AudioFormat& /*format*/) {
        masterBus_.read(reinterpret_cast<float*>(destinationBuffer), frameCount);
    });

    if (!started) {
        stop();
        return false;
    }

    return true;
}

bool AudioEngine::startCapture(const AudioDevice& captureDevice, const AudioDevice& outputDevice) {
    return startCapture(captureDevice.id, outputDevice.id);
}

bool AudioEngine::startCapture(const std::string& captureDeviceId, const std::string& outputDeviceId) {
    stop();
    isCaptureMode_ = true;

    // 1. Open and initialize WASAPI loopback capture client
    if (!capture_->open(captureDeviceId)) {
        return false;
    }
    if (!capture_->initialize()) {
        capture_->close();
        return false;
    }

    // 2. Open and initialize WASAPI output renderer client
    if (!output_->open(outputDeviceId)) {
        capture_->close();
        return false;
    }
    if (!output_->initialize()) {
        output_->close();
        capture_->close();
        return false;
    }

    const auto capFmt = capture_->format();
    const auto outFmt = output_->format();

    // 3. Configure MasterAudioBus: canonical Float32 stereo at capture sample rate with 1.0s capacity
    masterBus_.initialize(capFmt, capFmt.sampleRate);
    masterBus_.reset();

    // 4. Configure sample rate resampler
    resampler_.reset(capFmt.sampleRate, outFmt.sampleRate, capFmt.channels);

    // 5. Start capture client pushing into MasterAudioBus
    bool capStarted = capture_->start([this](const float* sourceBuffer, uint32_t frameCount, const AudioFormat& /*format*/) {
        masterBus_.write(sourceBuffer, frameCount);
    });

    if (!capStarted) {
        stop();
        return false;
    }

    // 6. Start output renderer pulling from MasterAudioBus (via resampler if sample rates differ)
    bool outStarted = output_->start([this, capFmt, outFmt](uint8_t* destinationBuffer, uint32_t frameCount, const AudioFormat& /*format*/) {
        float* dest = reinterpret_cast<float*>(destinationBuffer);
        if (capFmt.sampleRate == outFmt.sampleRate) {
            masterBus_.read(dest, frameCount);
        } else {
            resampler_.pull(masterBus_, dest, frameCount);
        }
    });

    if (!outStarted) {
        stop();
        return false;
    }

    return true;
}

void AudioEngine::stop() {
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

    if (output_) {
        output_->stop();
        output_->close();
    }

    masterBus_.reset();
    resampler_.resetState();
    isCaptureMode_ = false;
}

bool AudioEngine::isRunning() const {
    return output_ && output_->state() == OutputState::Running;
}

EngineDiagnostics AudioEngine::getDiagnostics() const {
    if (!isRunning() && (lastDiag_.busFramesWritten > 0 || lastDiag_.framesCaptured > 0)) {
        return lastDiag_;
    }

    EngineDiagnostics diag;
    if (output_) {
        diag.deviceName = output_->deviceName();
        diag.deviceId = output_->deviceId();
        diag.format = output_->format();
        diag.bufferFrameCount = output_->bufferFrameCount();
        diag.framesRendered = output_->framesRendered();
        diag.outputUnderruns = output_->underruns();
        auto clock = output_->getClockPosition();
        diag.clockPosition = clock.first;
        diag.clockFrequency = clock.second;
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
    diag.busUnderruns = masterBus_.underruns();
    diag.busOverruns = masterBus_.overruns();

    diag.isRunning = isRunning();
    diag.isCaptureMode = isCaptureMode_;

    return diag;
}

} // namespace syncwave
