#include "OutputRouter.h"
#include <algorithm>

namespace syncwave {

OutputRouter::OutputRouter() = default;

OutputRouter::~OutputRouter() {
    closeOutputs();
}

bool OutputRouter::addOutput(const AudioDevice& device) {
    std::lock_guard<std::mutex> lock(outputMutex_);

    // Prevent duplicate device addition
    for (const auto& out : outputs_) {
        if (out->deviceId() == device.id) {
            return false;
        }
    }

    outputs_.push_back(std::make_unique<DeviceOutput>(device));
    return true;
}

bool OutputRouter::removeOutput(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(outputMutex_);

    auto it = std::find_if(outputs_.begin(), outputs_.end(), [&](const std::unique_ptr<DeviceOutput>& out) {
        return out->deviceId() == deviceId;
    });

    if (it != outputs_.end()) {
        (*it)->close();
        outputs_.erase(it);
        return true;
    }

    return false;
}

void OutputRouter::clearOutputs() {
    std::lock_guard<std::mutex> lock(outputMutex_);
    for (auto& out : outputs_) {
        out->close();
    }
    outputs_.clear();
}

size_t OutputRouter::outputCount() const {
    std::lock_guard<std::mutex> lock(outputMutex_);
    return outputs_.size();
}

DeviceOutput* OutputRouter::getOutput(size_t index) {
    std::lock_guard<std::mutex> lock(outputMutex_);
    if (index < outputs_.size()) {
        return outputs_[index].get();
    }
    return nullptr;
}

DeviceOutput* OutputRouter::getOutput(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(outputMutex_);
    for (auto& out : outputs_) {
        if (out->deviceId() == deviceId) {
            return out.get();
        }
    }
    return nullptr;
}

bool OutputRouter::initializeOutputs(uint32_t masterSampleRate, uint32_t masterChannels) {
    std::lock_guard<std::mutex> lock(outputMutex_);

    if (outputs_.empty()) {
        return false;
    }

    masterSampleRate_ = (masterSampleRate > 0) ? masterSampleRate : 48000;
    masterChannels_ = (masterChannels > 0) ? masterChannels : 2;
    scratchDispatchBuffer_.assign(2048 * masterChannels_, 0.0f);
    totalFramesDistributed_.store(0, std::memory_order_relaxed);

    bool anyInitialized = false;
    for (auto& out : outputs_) {
        if (out->initialize(masterSampleRate_, masterChannels_)) {
            anyInitialized = true;
        }
    }

    return anyInitialized;
}

bool OutputRouter::initializeOutputsForTesting(uint32_t masterSampleRate, uint32_t masterChannels, const std::vector<uint32_t>& deviceSampleRates) {
    std::lock_guard<std::mutex> lock(outputMutex_);

    if (outputs_.empty()) {
        return false;
    }

    masterSampleRate_ = (masterSampleRate > 0) ? masterSampleRate : 48000;
    masterChannels_ = (masterChannels > 0) ? masterChannels : 2;
    scratchDispatchBuffer_.assign(2048 * masterChannels_, 0.0f);
    totalFramesDistributed_.store(0, std::memory_order_relaxed);

    for (size_t i = 0; i < outputs_.size(); ++i) {
        uint32_t devRate = masterSampleRate_;
        if (i < deviceSampleRates.size() && deviceSampleRates[i] > 0) {
            devRate = deviceSampleRates[i];
        }
        outputs_[i]->initializeForTesting(masterSampleRate_, masterChannels_, devRate);
    }

    return true;
}

bool OutputRouter::startOutputs() {
    std::lock_guard<std::mutex> lock(outputMutex_);

    if (outputs_.empty()) {
        return false;
    }

    bool anyStarted = false;
    for (auto& out : outputs_) {
        if (out->isAvailable() && out->state() == OutputState::Initialized) {
            if (out->start()) {
                anyStarted = true;
            }
        }
    }

    return anyStarted;
}

void OutputRouter::stopOutputs() {
    std::lock_guard<std::mutex> lock(outputMutex_);
    for (auto& out : outputs_) {
        out->stop();
    }
}

void OutputRouter::closeOutputs() {
    std::lock_guard<std::mutex> lock(outputMutex_);
    for (auto& out : outputs_) {
        out->close();
    }
}

void OutputRouter::route(const float* sourceFrames, size_t frameCount) {
    if (!sourceFrames || frameCount == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(outputMutex_);
    for (auto& out : outputs_) {
        if (out->isAvailable()) {
            out->push(sourceFrames, frameCount);
        }
    }

    totalFramesDistributed_.fetch_add(frameCount, std::memory_order_relaxed);
}

size_t OutputRouter::dispatch(MasterAudioBus& bus, size_t maxFrames) {
    size_t available = bus.availableFrames();
    if (available == 0) {
        return 0;
    }

    size_t framesToRead = std::min(maxFrames, available);
    if (scratchDispatchBuffer_.size() < framesToRead * masterChannels_) {
        scratchDispatchBuffer_.resize(framesToRead * masterChannels_);
    }

    size_t read = bus.read(scratchDispatchBuffer_.data(), framesToRead);
    if (read > 0) {
        route(scratchDispatchBuffer_.data(), read);
    }

    return read;
}

void OutputRouter::onDeviceDisconnected(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(outputMutex_);
    for (auto& out : outputs_) {
        if (out->deviceId() == deviceId) {
            out->markUnavailable();
            out->stop();
            break;
        }
    }
}

bool OutputRouter::allRunning() const {
    std::lock_guard<std::mutex> lock(outputMutex_);
    if (outputs_.empty()) return false;
    for (const auto& out : outputs_) {
        if (out->isAvailable() && out->state() != OutputState::Running) {
            return false;
        }
    }
    return true;
}

bool OutputRouter::anyRunning() const {
    std::lock_guard<std::mutex> lock(outputMutex_);
    for (const auto& out : outputs_) {
        if (out->isAvailable() && out->state() == OutputState::Running) {
            return true;
        }
    }
    return false;
}

uint64_t OutputRouter::totalFramesDistributed() const {
    return totalFramesDistributed_.load(std::memory_order_relaxed);
}

std::vector<DeviceOutputTelemetry> OutputRouter::getOutputTelemetry() const {
    std::lock_guard<std::mutex> lock(outputMutex_);
    std::vector<DeviceOutputTelemetry> telemetryList;
    telemetryList.reserve(outputs_.size());
    for (const auto& out : outputs_) {
        telemetryList.push_back(out->getTelemetry());
    }
    return telemetryList;
}

void OutputRouter::sampleAllClocks(std::chrono::steady_clock::time_point timestamp) {
    std::lock_guard<std::mutex> lock(outputMutex_);
    for (auto& out : outputs_) {
        if (out->isAvailable()) {
            out->sampleClock(timestamp);
        }
    }
}

std::vector<PairwiseDriftEstimate> OutputRouter::getPairwiseDriftEstimates() const {
    std::lock_guard<std::mutex> lock(outputMutex_);
    std::vector<PairwiseDriftEstimate> results;

    if (outputs_.size() < 2) {
        return results;
    }

    for (size_t i = 0; i < outputs_.size(); ++i) {
        for (size_t j = i + 1; j < outputs_.size(); ++j) {
            const auto& outA = outputs_[i];
            const auto& outB = outputs_[j];
            if (outA->isAvailable() && outB->isAvailable()) {
                auto estimate = DriftEstimator::estimate(
                    outA->clock(), outB->clock(),
                    outA->deviceName(), outB->deviceName()
                );
                results.push_back(estimate);
            }
        }
    }

    return results;
}

} // namespace syncwave
