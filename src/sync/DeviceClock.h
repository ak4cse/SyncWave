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
    uint64_t qpcFrequency = 0;        // QPC performance counter frequency (ticks/sec)
    uint64_t clockFrequency = 0;      // Hardware clock ticks per second
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
        return ticksToSeconds(clockPosition, clockFrequency);
    }
    [[nodiscard]] uint64_t positionFrames() const {
        return ticksToFrames(clockPosition, clockFrequency, sampleRate);
    }
    [[nodiscard]] double qpcSeconds() const {
        return qpcToSeconds(qpcPosition, qpcFrequency);
    }

    [[nodiscard]] static double ticksToSeconds(uint64_t ticks, uint64_t clockFreq) {
        return (clockFreq > 0) ? (static_cast<double>(ticks) / static_cast<double>(clockFreq)) : 0.0;
    }
    [[nodiscard]] static uint64_t ticksToFrames(uint64_t ticks, uint64_t clockFreq, uint32_t sRate) {
        if (clockFreq > 0 && sRate > 0) {
            return static_cast<uint64_t>((static_cast<double>(ticks) * sRate) / clockFreq);
        }
        return 0;
    }
    [[nodiscard]] static double qpcToSeconds(uint64_t qpcTicks, uint64_t qpfFreq) {
        return (qpfFreq > 0) ? (static_cast<double>(qpcTicks) / static_cast<double>(qpfFreq)) : 0.0;
    }
};

struct ClockRateEstimate {
    bool isValid = false;
    uint32_t nominalRate = 0;             // Nominal sample rate (Hz)
    double estimatedRate = 0.0;           // Estimated observed rate (Hz)
    double rateErrorPpm = 0.0;            // Deviation from nominal in ppm
    double measurementDurationSec = 0.0;  // Span between first and last sample in window
    size_t sampleCount = 0;
    double rSquared = 1.0;                // Goodness-of-fit (1.0 = ideal linear)
    double windowRequestedSec = 0.0;      // Window requested (0 = entire history)
};

class DeviceClock {
public:
    explicit DeviceClock(std::string deviceId = "", uint32_t nominalSampleRate = 48000, size_t maxHistory = 1024);
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
    [[nodiscard]] std::optional<DeviceClockSample> firstSample() const;
    [[nodiscard]] std::optional<DeviceClockSample> latestSample() const;
    [[nodiscard]] std::vector<DeviceClockSample> getHistory() const;

    // Total elapsed device-clock time (seconds) between first and latest sample
    [[nodiscard]] double totalElapsedDeviceTimeSec() const;

    // Total elapsed QPC reference time (seconds) between first and latest sample
    [[nodiscard]] double totalElapsedQpcTimeSec() const;

    // Compute effective clock rate using linear regression across entire sample history
    [[nodiscard]] ClockRateEstimate estimateRate() const;

    // Compute effective clock rate over the most recent windowSec seconds of samples
    [[nodiscard]] ClockRateEstimate estimateRateOverWindow(double windowSec) const;

    // Query system QPC frequency (cached)
    [[nodiscard]] static uint64_t getSystemQpcFrequency();

private:
    std::string deviceId_;
    uint32_t nominalSampleRate_ = 48000;
    size_t maxHistory_ = 1024;

    mutable std::mutex mutex_;
    std::deque<DeviceClockSample> history_;
};

} // namespace syncwave
