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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace syncwave
