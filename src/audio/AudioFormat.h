#pragma once

#include <cstdint>
#include <string>

namespace syncwave {

enum class SampleType {
    Float32,
    Int16,
    Int24In32,
    Int32,
    Unknown
};

inline std::string sampleTypeToString(SampleType type) {
    switch (type) {
        case SampleType::Float32:   return "Float32";
        case SampleType::Int16:     return "Int16";
        case SampleType::Int24In32: return "Int24-in-32";
        case SampleType::Int32:     return "Int32";
        case SampleType::Unknown:
        default:                    return "Unknown";
    }
}

struct AudioFormat {
    uint32_t sampleRate = 48000;
    uint16_t channels = 2;
    uint16_t bitsPerSample = 32;
    uint16_t validBitsPerSample = 32;
    uint16_t blockAlign = 8;
    SampleType sampleType = SampleType::Float32;

    [[nodiscard]] uint32_t bytesPerFrame() const {
        return (blockAlign > 0) ? blockAlign : (channels * (bitsPerSample / 8));
    }

    [[nodiscard]] uint32_t bytesPerSecond() const {
        return sampleRate * bytesPerFrame();
    }

    [[nodiscard]] bool isFloat() const {
        return sampleType == SampleType::Float32;
    }

    [[nodiscard]] std::string formatString() const {
        return std::to_string(sampleRate) + " Hz, " +
               std::to_string(channels) + " ch, " +
               sampleTypeToString(sampleType) + " (" +
               std::to_string(bitsPerSample) + "-bit)";
    }
};

} // namespace syncwave
