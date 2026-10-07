#include "CalibrationStore.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>
#include <filesystem>
#include <cstring>
#include <iostream>

namespace syncwave {

static std::string getCurrentIsoTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tmUtc{};
#if defined(_WIN32)
    gmtime_s(&tmUtc, &tt);
#else
    gmtime_r(&tt, &tmUtc);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tmUtc, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

static std::string escapeJson(const std::string& str) {
    std::ostringstream ss;
    for (char c : str) {
        if (c == '"') ss << "\\\"";
        else if (c == '\\') ss << "\\\\";
        else if (c == '\b') ss << "\\b";
        else if (c == '\f') ss << "\\f";
        else if (c == '\n') ss << "\\n";
        else if (c == '\r') ss << "\\r";
        else if (c == '\t') ss << "\\t";
        else ss << c;
    }
    return ss.str();
}

// Simple key-value string extractor for JSON object
static std::string extractJsonString(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return {};

    pos = json.find(':', pos + needle.size());
    pos = json.find('"', pos);
    if (pos == std::string::npos) return {};

    // find closing quote that is NOT escaped
    size_t endQuote = pos + 1;
    bool escape = false;
    while (endQuote < json.size()) {
        if (escape) {
            escape = false;
        } else if (json[endQuote] == '\\') {
            escape = true;
        } else if (json[endQuote] == '"') {
            break;
        }
        ++endQuote;
    }
    if (endQuote >= json.size()) return {};

    return json.substr(pos + 1, endQuote - pos - 1);
}

static double extractJsonDouble(const std::string& json, const std::string& key, double defaultVal = 0.0) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return defaultVal;

    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return defaultVal;

    // Skip whitespace
    while (pos < json.size() && (json[pos] == ':' || json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) {
        ++pos;
    }

    size_t endPos = pos;
    while (endPos < json.size() && (json[endPos] == '-' || json[endPos] == '+' || json[endPos] == '.' || (json[endPos] >= '0' && json[endPos] <= '9') || json[endPos] == 'e' || json[endPos] == 'E')) {
        ++endPos;
    }

    if (endPos > pos) {
        try {
            return std::stod(json.substr(pos, endPos - pos));
        } catch (...) {}
    }
    return defaultVal;
}

CalibrationStore::CalibrationStore(std::string storagePath)
    : storagePath_(std::move(storagePath))
{
    ensureDefaultPath();
    loadInternal();
}

void CalibrationStore::ensureDefaultPath() {
    if (storagePath_.empty()) {
        const char* userProfile = std::getenv("USERPROFILE");
        if (userProfile && std::strlen(userProfile) > 0) {
            std::filesystem::path p(userProfile);
            p /= ".syncwave";
            std::error_code ec;
            std::filesystem::create_directories(p, ec);
            p /= "calibrations.json";
            storagePath_ = p.string();
        } else {
            storagePath_ = "calibrations.json";
        }
    }
}

bool CalibrationStore::save(const AcousticCalibrationRecord& rec) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto record = rec;
    if (record.timestamp.empty()) {
        record.timestamp = getCurrentIsoTimestamp();
    }

    auto it = std::find_if(records_.begin(), records_.end(), [&](const AcousticCalibrationRecord& r) {
        return r.deviceId == record.deviceId;
    });

    if (it != records_.end()) {
        *it = record;
    } else {
        records_.push_back(record);
    }

    return persistInternal();
}

std::optional<AcousticCalibrationRecord> CalibrationStore::get(const std::string& deviceId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(records_.begin(), records_.end(), [&](const AcousticCalibrationRecord& r) {
        return r.deviceId == deviceId;
    });

    if (it != records_.end()) {
        return *it;
    }
    return std::nullopt;
}

std::vector<AcousticCalibrationRecord> CalibrationStore::list() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return records_;
}

bool CalibrationStore::remove(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(records_.begin(), records_.end(), [&](const AcousticCalibrationRecord& r) {
        return r.deviceId == deviceId;
    });

    if (it != records_.end()) {
        records_.erase(it);
        return persistInternal();
    }
    return false;
}

void CalibrationStore::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    records_.clear();
    persistInternal();
}

bool CalibrationStore::loadFromFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    storagePath_ = path;
    return loadInternal();
}

bool CalibrationStore::saveToFile(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    storagePath_ = path;
    return persistInternal();
}

bool CalibrationStore::loadInternal() {
    records_.clear();
    std::ifstream file(storagePath_);
    if (!file.is_open()) {
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();

    // Find "calibrations" array
    size_t arrPos = json.find("\"calibrations\"");
    if (arrPos == std::string::npos) {
        return false;
    }
    size_t openBracket = json.find('[', arrPos);
    if (openBracket == std::string::npos) {
        return false;
    }

    bool inQuotes = false;
    bool escape = false;
    int braceDepth = 0;
    size_t objStart = std::string::npos;

    for (size_t i = openBracket + 1; i < json.size(); ++i) {
        char c = json[i];
        if (escape) {
            escape = false;
            continue;
        }
        if (c == '\\') {
            escape = true;
            continue;
        }
        if (c == '"') {
            inQuotes = !inQuotes;
            continue;
        }
        if (!inQuotes) {
            if (c == ']' && braceDepth == 0) {
                break;
            }
            if (c == '{') {
                if (braceDepth == 0) {
                    objStart = i;
                }
                braceDepth++;
            } else if (c == '}') {
                braceDepth--;
                if (braceDepth == 0 && objStart != std::string::npos) {
                    std::string objStr = json.substr(objStart, i - objStart + 1);
                    std::string devId = extractJsonString(objStr, "deviceId");
                    if (!devId.empty()) {
                        AcousticCalibrationRecord r;
                        r.deviceId = devId;
                        r.deviceName = extractJsonString(objStr, "deviceName");
                        r.microphoneId = extractJsonString(objStr, "microphoneId");
                        r.microphoneName = extractJsonString(objStr, "microphoneName");
                        std::string mType = extractJsonString(objStr, "measurement_type");
                        if (mType.empty()) mType = extractJsonString(objStr, "measurementType");
                        if (!mType.empty()) r.measurementType = mType;
                        r.measuredLatencyMs = extractJsonDouble(objStr, "measuredLatencyMs", 0.0);
                        r.uncertaintyMs = extractJsonDouble(objStr, "uncertaintyMs", 0.0);
                        r.madMs = extractJsonDouble(objStr, "madMs", 0.0);
                        r.confidence = static_cast<float>(extractJsonDouble(objStr, "confidence", 0.0));
                        r.runCount = static_cast<uint32_t>(extractJsonDouble(objStr, "runCount", 0));
                        r.validRunCount = static_cast<uint32_t>(extractJsonDouble(objStr, "validRunCount", 0));
                        r.sampleRate = static_cast<uint32_t>(extractJsonDouble(objStr, "sampleRate", 48000));
                        r.timestamp = extractJsonString(objStr, "timestamp");

                        records_.push_back(r);
                    }
                    objStart = std::string::npos;
                }
            }
        }
    }

    return true;
}

bool CalibrationStore::persistInternal() const {
    if (storagePath_.empty()) {
        return false;
    }

    // Ensure parent directory exists
    std::filesystem::path p(storagePath_);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }

    std::ofstream file(storagePath_, std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }

    file << "{\n  \"calibrations\": [\n";
    for (size_t i = 0; i < records_.size(); ++i) {
        const auto& r = records_[i];
        file << "    {\n";
        file << "      \"deviceId\": \"" << escapeJson(r.deviceId) << "\",\n";
        file << "      \"deviceName\": \"" << escapeJson(r.deviceName) << "\",\n";
        file << "      \"microphoneId\": \"" << escapeJson(r.microphoneId) << "\",\n";
        file << "      \"microphoneName\": \"" << escapeJson(r.microphoneName) << "\",\n";
        file << "      \"measurement_type\": \"" << escapeJson(r.measurementType.empty() ? "end_to_end_acoustic_arrival" : r.measurementType) << "\",\n";
        file << "      \"measuredLatencyMs\": " << std::fixed << std::setprecision(4) << r.measuredLatencyMs << ",\n";
        file << "      \"uncertaintyMs\": " << std::fixed << std::setprecision(4) << r.uncertaintyMs << ",\n";
        file << "      \"madMs\": " << std::fixed << std::setprecision(4) << r.madMs << ",\n";
        file << "      \"confidence\": " << std::fixed << std::setprecision(4) << r.confidence << ",\n";
        file << "      \"runCount\": " << r.runCount << ",\n";
        file << "      \"validRunCount\": " << r.validRunCount << ",\n";
        file << "      \"sampleRate\": " << r.sampleRate << ",\n";
        file << "      \"timestamp\": \"" << escapeJson(r.timestamp) << "\"\n";
        file << "    }" << (i + 1 < records_.size() ? ",\n" : "\n");
    }
    file << "  ]\n}\n";

    return true;
}

} // namespace syncwave
