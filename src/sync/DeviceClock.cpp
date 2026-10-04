#include "DeviceClock.h"
#include <cmath>
#include <numeric>
#include <algorithm>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace syncwave {

uint64_t DeviceClock::getSystemQpcFrequency() {
    static uint64_t cachedQpf = []() -> uint64_t {
        LARGE_INTEGER qpf;
        if (QueryPerformanceFrequency(&qpf)) {
            return static_cast<uint64_t>(qpf.QuadPart);
        }
        return 10000000ULL; // Standard fallback (10 MHz)
    }();
    return cachedQpf;
}

DeviceClock::DeviceClock(std::string deviceId, uint32_t nominalSampleRate, size_t maxHistory)
    : deviceId_(std::move(deviceId)),
      nominalSampleRate_(nominalSampleRate > 0 ? nominalSampleRate : 48000),
      maxHistory_(maxHistory > 1 ? maxHistory : 1024) {}

DeviceClock::DeviceClock(DeviceClock&& other) noexcept {
    std::lock_guard<std::mutex> lock(other.mutex_);
    deviceId_ = std::move(other.deviceId_);
    nominalSampleRate_ = other.nominalSampleRate_;
    maxHistory_ = other.maxHistory_;
    history_ = std::move(other.history_);
}

DeviceClock& DeviceClock::operator=(DeviceClock&& other) noexcept {
    if (this != &other) {
        std::scoped_lock lock(mutex_, other.mutex_);
        deviceId_ = std::move(other.deviceId_);
        nominalSampleRate_ = other.nominalSampleRate_;
        maxHistory_ = other.maxHistory_;
        history_ = std::move(other.history_);
    }
    return *this;
}

void DeviceClock::recordSample(const DeviceClockSample& sample) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (history_.size() >= maxHistory_) {
        history_.pop_front();
    }
    history_.push_back(sample);
}

void DeviceClock::recordSnapshot(const WasapiClockSnapshot& snapshot,
                                 std::chrono::steady_clock::time_point timestamp) {
    if (!snapshot.isValid) {
        return;
    }

    DeviceClockSample sample;
    sample.timestamp = timestamp;
    sample.clockPosition = snapshot.position;
    sample.qpcPosition = snapshot.qpcPosition;
    sample.qpcFrequency = getSystemQpcFrequency();
    sample.clockFrequency = snapshot.frequency;
    sample.sampleRate = snapshot.sampleRate > 0 ? snapshot.sampleRate : nominalSampleRate_;
    sample.bufferFrameCount = snapshot.bufferFrameCount;
    sample.currentPadding = snapshot.currentPadding;
    sample.streamLatencyHns = snapshot.streamLatencyHns;
    sample.framesRendered = snapshot.framesRendered;
    sample.isValid = true;

    recordSample(sample);
}

void DeviceClock::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    history_.clear();
}

const std::string& DeviceClock::deviceId() const {
    return deviceId_;
}

uint32_t DeviceClock::nominalSampleRate() const {
    return nominalSampleRate_;
}

void DeviceClock::setNominalSampleRate(uint32_t rate) {
    if (rate > 0) {
        nominalSampleRate_ = rate;
    }
}

size_t DeviceClock::sampleCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return history_.size();
}

std::optional<DeviceClockSample> DeviceClock::firstSample() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (history_.empty()) {
        return std::nullopt;
    }
    return history_.front();
}

std::optional<DeviceClockSample> DeviceClock::latestSample() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (history_.empty()) {
        return std::nullopt;
    }
    return history_.back();
}

std::vector<DeviceClockSample> DeviceClock::getHistory() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::vector<DeviceClockSample>(history_.begin(), history_.end());
}

double DeviceClock::totalElapsedDeviceTimeSec() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (history_.size() < 2) {
        return 0.0;
    }
    return history_.back().positionSeconds() - history_.front().positionSeconds();
}

double DeviceClock::totalElapsedQpcTimeSec() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (history_.size() < 2) {
        return 0.0;
    }
    const auto& s0 = history_.front();
    const auto& s1 = history_.back();
    if (s0.qpcFrequency > 0 && s1.qpcPosition >= s0.qpcPosition) {
        return static_cast<double>(s1.qpcPosition - s0.qpcPosition) / static_cast<double>(s0.qpcFrequency);
    }
    return std::chrono::duration<double>(s1.timestamp - s0.timestamp).count();
}

ClockRateEstimate DeviceClock::estimateRate() const {
    return estimateRateOverWindow(0.0);
}

ClockRateEstimate DeviceClock::estimateRateOverWindow(double windowSec) const {
    std::lock_guard<std::mutex> lock(mutex_);

    ClockRateEstimate est;
    est.nominalRate = nominalSampleRate_;
    est.windowRequestedSec = windowSec;

    if (history_.size() < 2) {
        est.isValid = false;
        est.sampleCount = history_.size();
        return est;
    }

    // Determine start index based on windowSec
    size_t startIdx = 0;
    const auto latestTime = history_.back().timestamp;
    if (windowSec > 0.0) {
        const auto cutoff = latestTime - std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                             std::chrono::duration<double>(windowSec));
        while (startIdx < history_.size() && history_[startIdx].timestamp < cutoff) {
            startIdx++;
        }
        // Ensure at least 2 samples remain for the window
        if (history_.size() - startIdx < 2 && history_.size() >= 2) {
            startIdx = history_.size() - 2;
        }
    }

    const size_t N = history_.size() - startIdx;
    if (N < 2) {
        est.isValid = false;
        est.sampleCount = N;
        return est;
    }

    const auto t0 = history_[startIdx].timestamp;
    std::vector<double> t(N);
    std::vector<double> p(N);

    double sumT = 0.0;
    double sumP = 0.0;

    for (size_t i = 0; i < N; ++i) {
        const auto& s = history_[startIdx + i];
        t[i] = std::chrono::duration<double>(s.timestamp - t0).count();

        // Calculate cumulative frames from clock position
        if (s.clockFrequency > 0 && s.sampleRate > 0) {
            p[i] = (static_cast<double>(s.clockPosition) * s.sampleRate) / static_cast<double>(s.clockFrequency);
        } else {
            p[i] = static_cast<double>(s.clockPosition);
        }

        sumT += t[i];
        sumP += p[i];
    }

    const double duration = t.back() - t.front();
    est.measurementDurationSec = duration;
    est.sampleCount = N;

    // Minimum duration threshold for rate calculation (at least 1 millisecond)
    if (duration < 0.001) {
        est.isValid = false;
        return est;
    }

    const double meanT = sumT / N;
    const double meanP = sumP / N;

    double sTt = 0.0;
    double sTp = 0.0;
    double sPp = 0.0;

    for (size_t i = 0; i < N; ++i) {
        double dt = t[i] - meanT;
        double dp = p[i] - meanP;
        sTt += dt * dt;
        sTp += dt * dp;
        sPp += dp * dp;
    }

    if (sTt < 1e-9) {
        est.isValid = false;
        return est;
    }

    const double slope = sTp / sTt;
    est.estimatedRate = slope;

    if (sPp > 1e-9) {
        double r2 = (sTp * sTp) / (sTt * sPp);
        est.rSquared = std::clamp(r2, 0.0, 1.0);
    } else {
        est.rSquared = 1.0;
    }

    if (est.nominalRate > 0) {
        est.rateErrorPpm = ((est.estimatedRate / est.nominalRate) - 1.0) * 1e6;
    }

    est.isValid = (est.estimatedRate > 0.0);
    return est;
}

} // namespace syncwave
