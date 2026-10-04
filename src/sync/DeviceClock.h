#pragma once

#include "../windows/WasapiOutput.h"
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <optional>

namespace syncwave {

struct DeviceClockSample {
    std::chrono::steady_clock::time_point timestamp;
    uint64_t clockPosition = 0;       // Raw ticks from IAudioClock or frames
    uint64_t qpcPosition = 0;         // QPC value when sampled
    uint64_t clockFrequency = 0;      // Ticks per second
    uint32_t sampleRate = 0;          // Endpoint sample rate in Hz
    uint32_t bufferFrameCount = 0;    // Endpoint buffer capacity
    uint32_t currentPadding = 0;      // Queued frames in WASAPI buffer
    int64_t streamLatencyHns = 0;     // WASAPI stream latency in 100ns units
    uint64_t framesRendered = 0;
    bool isValid = false;

    [[nodiscard]] double streamLatencyMs() const {
        return static_cast<double>(streamLatencyHns) / 10000.0;
    }
    [[nodiscard]] double streamLatencySec() const {
        return static_cast<double>(streamLatencyHns) / 10000000.0;
    }
    [[nodiscard]] double paddingMs() const {
        return (sampleRate > 0) ? (static_cast<double>(currentPadding) * 1000.0 / sampleRate) : 0.0;
    }
    [[nodiscard]] double positionSeconds() const {
        return (clockFrequency > 0) ? (static_cast<double>(clockPosition) / clockFrequency) : 0.0;
    }
    [[nodiscard]] uint64_t positionFrames() const {
        if (clockFrequency > 0 && sampleRate > 0) {
            return static_cast<uint64_t>((static_cast<double>(clockPosition) * sampleRate) / clockFrequency);
        }
        return clockPosition;
    }
};

struct ClockRateEstimate {
    bool isValid = false;
    uint32_t nominalRate = 0;             // Nominal sample rate (Hz)
    double estimatedRate = 0.0;           // Estimated observed rate (Hz)
    double rateErrorPpm = 0.0;            // Deviation from nominal in ppm
    double measurementDurationSec = 0.0;  // Span between first and last sample
    size_t sampleCount = 0;
    double rSquared = 1.0;                // Goodness-of-fit (1.0 = ideal linear)
};

class DeviceClock {
public:
    explicit DeviceClock(std::string deviceId = "", uint32_t nominalSampleRate = 48000, size_t maxHistory = 128);
    ~DeviceClock() = default;

    DeviceClock(const DeviceClock&) = delete;
    DeviceClock& operator=(const DeviceClock&) = delete;
    DeviceClock(DeviceClock&&) noexcept;
    DeviceClock& operator=(DeviceClock&&) noexcept;

    // Record an explicit sample
    void recordSample(const DeviceClockSample& sample);

    // Record from a WASAPI clock snapshot
    void recordSnapshot(const WasapiClockSnapshot& snapshot,
                        std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now());

    // Reset all history and counters
    void reset();

    [[nodiscard]] const std::string& deviceId() const;
    [[nodiscard]] uint32_t nominalSampleRate() const;
    void setNominalSampleRate(uint32_t rate);

    [[nodiscard]] size_t sampleCount() const;
    [[nodiscard]] std::optional<DeviceClockSample> latestSample() const;
    [[nodiscard]] std::vector<DeviceClockSample> getHistory() const;

    // Compute effective clock rate using linear regression across sample history
    [[nodiscard]] ClockRateEstimate estimateRate() const;

private:
    std::string deviceId_;
    uint32_t nominalSampleRate_ = 48000;
    size_t maxHistory_ = 128;

    mutable std::mutex mutex_;
    std::deque<DeviceClockSample> history_;
};

} // namespace syncwave
