#include "AudioEngine.h"
#include "../windows/WasapiOutput.h"

namespace syncwave {

AudioEngine::AudioEngine()
    : output_(std::make_unique<WasapiOutput>()), toneGen_(440.0, 0.25) {}

AudioEngine::~AudioEngine() {
    stop();
}

AudioEngine::AudioEngine(AudioEngine&&) noexcept = default;
AudioEngine& AudioEngine::operator=(AudioEngine&&) noexcept = default;

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

    bool started = output_->start([this](uint8_t* destinationBuffer, uint32_t frameCount, const AudioFormat& format) {
        toneGen_.generateFrames(destinationBuffer, frameCount, format);
    });

    if (!started) {
        output_->close();
        return false;
    }

    return true;
}

void AudioEngine::stop() {
    if (output_) {
        output_->stop();
        output_->close();
    }
}

bool AudioEngine::isRunning() const {
    return output_ && output_->state() == OutputState::Running;
}

EngineDiagnostics AudioEngine::getDiagnostics() const {
    EngineDiagnostics diag;
    if (!output_) return diag;

    diag.deviceName = output_->deviceName();
    diag.deviceId = output_->deviceId();
    diag.format = output_->format();
    diag.bufferFrameCount = output_->bufferFrameCount();
    diag.framesRendered = output_->framesRendered();
    diag.underruns = output_->underruns();
    auto clock = output_->getClockPosition();
    diag.clockPosition = clock.first;
    diag.clockFrequency = clock.second;
    diag.isRunning = isRunning();

    return diag;
}

} // namespace syncwave
