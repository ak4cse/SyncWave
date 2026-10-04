#pragma once

#include "../audio/AudioFormat.h"
#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <cstdint>

namespace syncwave {

enum class OutputState {
    Uninitialized,
    Opened,
    Initialized,
    Running,
    Stopped,
    Closed,
    Error
};

std::string outputStateToString(OutputState state);

using RenderCallback = std::function<void(uint8_t* destinationBuffer, uint32_t frameCount, const AudioFormat& format)>;

struct WasapiClockSnapshot {
    bool isValid = false;
    uint64_t position = 0;
    uint64_t qpcPosition = 0;
    uint64_t frequency = 0;
    uint32_t sampleRate = 0;
    uint32_t bufferFrameCount = 0;
    uint32_t currentPadding = 0;
    int64_t streamLatencyHns = 0; // 100-nanosecond units (REFERENCE_TIME)
    uint64_t framesRendered = 0;
    uint32_t underruns = 0;
};

class WasapiOutput {
public:
    WasapiOutput();
    ~WasapiOutput();

    WasapiOutput(const WasapiOutput&) = delete;
    WasapiOutput& operator=(const WasapiOutput&) = delete;
    WasapiOutput(WasapiOutput&&) noexcept;
    WasapiOutput& operator=(WasapiOutput&&) noexcept;

    // Open audio render endpoint by opaque ID (empty string means default device)
    bool open(const std::string& deviceId = "");

    // Inspect format and initialize WASAPI audio client in shared event-driven mode
    bool initialize();

    // Start playback thread feeding audio frames from renderCallback
    bool start(RenderCallback callback);

    // Stop playback thread and halt audio stream
    void stop();

    // Release all WASAPI and COM resources
    void close();

    [[nodiscard]] OutputState state() const;
    [[nodiscard]] AudioFormat format() const;
    [[nodiscard]] uint32_t bufferFrameCount() const;
    [[nodiscard]] uint64_t framesRendered() const;
    [[nodiscard]] uint32_t underruns() const;
    [[nodiscard]] std::string deviceName() const;
    [[nodiscard]] std::string deviceId() const;

    // Queries IAudioClock if available, returns { positionInFrames, clockFrequencyHz }
    [[nodiscard]] std::pair<uint64_t, uint64_t> getClockPosition() const;

    // Queries comprehensive WASAPI timing snapshot (IAudioClock, IAudioClient padding, stream latency)
    [[nodiscard]] WasapiClockSnapshot getClockSnapshot() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace syncwave
