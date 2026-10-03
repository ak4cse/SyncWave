#include "AudioEngine.h"
#include "../windows/WasapiOutput.h"
#include <chrono>

namespace syncwave {

AudioEngine::AudioEngine()
    : output_(std::make_unique<WasapiOutput>()),
      toneGen_(440.0, 0.25),
      masterBus_(MasterAudioBus::canonicalFormat(), 48000) {}

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
    // Initialize MasterAudioBus with matching channel count and rate for 1.0 second capacity
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

void AudioEngine::stop() {
    if (producerRunning_.exchange(false, std::memory_order_acq_rel)) {
        if (producerThread_.joinable()) {
            producerThread_.join();
        }
    }

    if (output_) {
        lastDiag_ = getDiagnostics();
        output_->stop();
        output_->close();
    }

    masterBus_.reset();
}

bool AudioEngine::isRunning() const {
    return output_ && output_->state() == OutputState::Running;
}

EngineDiagnostics AudioEngine::getDiagnostics() const {
    if (!isRunning() && lastDiag_.busFramesWritten > 0) {
        return lastDiag_;
    }

    EngineDiagnostics diag;
    if (!output_) return diag;

    diag.deviceName = output_->deviceName();
    diag.deviceId = output_->deviceId();
    diag.format = output_->format();
    diag.masterFormat = masterBus_.format();
    diag.bufferFrameCount = output_->bufferFrameCount();
    diag.framesRendered = output_->framesRendered();
    diag.outputUnderruns = output_->underruns();
    auto clock = output_->getClockPosition();
    diag.clockPosition = clock.first;
    diag.clockFrequency = clock.second;

    diag.busCapacityFrames = masterBus_.capacityFrames();
    diag.busAvailableFrames = masterBus_.availableFrames();
    diag.busFramesWritten = masterBus_.totalFramesWritten();
    diag.busFramesRead = masterBus_.totalFramesRead();
    diag.busUnderruns = masterBus_.underruns();
    diag.busOverruns = masterBus_.overruns();

    diag.isRunning = isRunning();

    return diag;
}

} // namespace syncwave
