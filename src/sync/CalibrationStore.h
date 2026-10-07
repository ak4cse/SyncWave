#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <mutex>

namespace syncwave {

struct AcousticCalibrationRecord {
    std::string deviceId;
    std::string deviceName;
    std::string microphoneId;
    std::string microphoneName;
    std::string measurementType = "end_to_end_acoustic_arrival";
    double measuredLatencyMs = 0.0;
    double uncertaintyMs = 0.0;
    double madMs = 0.0;
    float confidence = 0.0f;
    uint32_t runCount = 0;
    uint32_t validRunCount = 0;
    uint32_t sampleRate = 48000;
    std::string timestamp; // ISO 8601 UTC timestamp
};

class CalibrationStore {
public:
    explicit CalibrationStore(std::string storagePath = "");

    bool save(const AcousticCalibrationRecord& record);
    [[nodiscard]] std::optional<AcousticCalibrationRecord> get(const std::string& deviceId) const;
    [[nodiscard]] std::vector<AcousticCalibrationRecord> list() const;
    bool remove(const std::string& deviceId);
    void clear();

    [[nodiscard]] const std::string& storagePath() const { return storagePath_; }

    // Direct in-memory load/save
    bool loadFromFile(const std::string& path);
    bool saveToFile(const std::string& path) const;

private:
    mutable std::string storagePath_;
    mutable std::mutex mutex_;
    std::vector<AcousticCalibrationRecord> records_;

    void ensureDefaultPath();
    bool loadInternal();
    bool persistInternal() const;
};

} // namespace syncwave
