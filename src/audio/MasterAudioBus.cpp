#include "MasterAudioBus.h"

namespace syncwave {

AudioFormat MasterAudioBus::canonicalFormat() {
    AudioFormat fmt;
    fmt.sampleRate = 48000;
    fmt.channels = 2;
    fmt.bitsPerSample = 32;
    fmt.validBitsPerSample = 32;
    fmt.blockAlign = 8;
    fmt.sampleType = SampleType::Float32;
    return fmt;
}

MasterAudioBus::MasterAudioBus(const AudioFormat& format, size_t capacityFrames)
    : format_(format), ringBuffer_(capacityFrames, format.channels) {}

MasterAudioBus::~MasterAudioBus() = default;

MasterAudioBus::MasterAudioBus(MasterAudioBus&& other) noexcept
    : format_(other.format_), ringBuffer_(std::move(other.ringBuffer_)) {}

MasterAudioBus& MasterAudioBus::operator=(MasterAudioBus&& other) noexcept {
    if (this != &other) {
        format_ = other.format_;
        ringBuffer_ = std::move(other.ringBuffer_);
    }
    return *this;
}

void MasterAudioBus::initialize(const AudioFormat& format, size_t capacityFrames) {
    format_ = format;
    ringBuffer_.initialize(capacityFrames, format.channels);
}

size_t MasterAudioBus::write(const float* source, size_t frames) {
    return ringBuffer_.write(source, frames);
}

size_t MasterAudioBus::read(float* destination, size_t frames) {
    return ringBuffer_.read(destination, frames);
}

void MasterAudioBus::reset() {
    ringBuffer_.reset();
}

void MasterAudioBus::flush() {
    ringBuffer_.flush();
}

size_t MasterAudioBus::availableFrames() const {
    return ringBuffer_.availableToRead();
}

size_t MasterAudioBus::freeFrames() const {
    return ringBuffer_.availableToWrite();
}

size_t MasterAudioBus::capacityFrames() const {
    return ringBuffer_.capacityFrames();
}

uint64_t MasterAudioBus::totalFramesWritten() const {
    return ringBuffer_.totalFramesWritten();
}

uint64_t MasterAudioBus::totalFramesRead() const {
    return ringBuffer_.totalFramesRead();
}

uint64_t MasterAudioBus::underruns() const {
    return ringBuffer_.underruns();
}

uint64_t MasterAudioBus::overruns() const {
    return ringBuffer_.overruns();
}

} // namespace syncwave
