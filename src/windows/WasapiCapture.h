#pragma once

#include "../audio/AudioFormat.h"
#include <string>
#include <functional>
#include <memory>
#include <cstdint>
#include <utility>

namespace syncwave {

enum class CaptureState {
    Uninitialized,
    Opened,
    Initialized,
    Running,
    Stopped,
    Closed,
    Error
};

std::string captureStateToString(CaptureState state);

// Callback invoked when captured Float32 audio packets are available
using CaptureCallback = std::function<void(const float* sourceBuffer, uint32_t frameCount, const AudioFormat& format)>;

class WasapiCapture {
public:
    WasapiCapture();
    ~WasapiCapture();

    WasapiCapture(const WasapiCapture&) = delete;
    WasapiCapture& operator=(const WasapiCapture&) = delete;
    WasapiCapture(WasapiCapture&&) noexcept;
    WasapiCapture& operator=(WasapiCapture&&) noexcept;

    // Open audio render endpoint for loopback capture by opaque ID (empty string = default console render endpoint)
    bool open(const std::string& deviceId = "");

    // Query mix format and initialize WASAPI audio client in shared loopback mode
    bool initialize();

    // Start background capture thread delivering audio frames to callback
    bool start(CaptureCallback callback);

    // Stop background capture thread and pause audio client
    void stop();

    // Release all WASAPI and COM resources
    void close();

    [[nodiscard]] CaptureState state() const;
    [[nodiscard]] AudioFormat format() const;
    [[nodiscard]] uint32_t bufferFrameCount() const;
    [[nodiscard]] uint64_t framesCaptured() const;
    [[nodiscard]] uint64_t packetsCaptured() const;
    [[nodiscard]] uint32_t silencePackets() const;
    [[nodiscard]] uint32_t discontinuities() const;
    [[nodiscard]] uint32_t captureErrors() const;
    [[nodiscard]] std::string deviceName() const;
    [[nodiscard]] std::string deviceId() const;

    // Queries IAudioClock if available, returns { positionInFrames, clockFrequencyHz }
    [[nodiscard]] std::pair<uint64_t, uint64_t> getClockPosition() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace syncwave
