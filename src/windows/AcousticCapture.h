#pragma once

#include "../audio/AudioFormat.h"
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <cstdint>

namespace syncwave {

class AcousticCapture {
public:
    AcousticCapture();
    ~AcousticCapture();

    AcousticCapture(const AcousticCapture&) = delete;
    AcousticCapture& operator=(const AcousticCapture&) = delete;
    AcousticCapture(AcousticCapture&&) noexcept;
    AcousticCapture& operator=(AcousticCapture&&) noexcept;

    // Open audio capture endpoint (empty string = default microphone)
    bool open(const std::string& deviceId = "");

    // Query mix format and initialize WASAPI client for capture
    bool initialize();

    // Start background recording of up to maxFrames samples (mono Float32)
    bool startRecording(size_t maxFrames);

    // Stop recording and wait for background thread to exit
    void stopRecording();

    // Release all COM resources
    void close();

    [[nodiscard]] bool isRecording() const;
    [[nodiscard]] bool isComplete() const;
    [[nodiscard]] uint32_t sampleRate() const;
    [[nodiscard]] uint32_t channels() const;
    [[nodiscard]] std::string deviceName() const;
    [[nodiscard]] std::string deviceId() const;
    [[nodiscard]] std::chrono::steady_clock::time_point startTime() const;

    // Retrieve recorded mono Float32 audio samples
    [[nodiscard]] std::vector<float> getRecordedSamples() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace syncwave
