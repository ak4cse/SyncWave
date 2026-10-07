#include "TimelineModel.h"
#include <cmath>
#include <algorithm>

namespace syncwave {

TimelineModel::TimelineModel(uint32_t masterSampleRate)
    : sampleRate_(masterSampleRate > 0 ? masterSampleRate : 48000) {}

void TimelineModel::setSampleRate(uint32_t sampleRate) {
    std::lock_guard<std::mutex> lock(mutex_);
    sampleRate_ = sampleRate > 0 ? sampleRate : 48000;
}

uint64_t TimelineModel::mediaUsToMasterFrame(int64_t mediaUs) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!hasAnchor_) {
        // If no explicit anchor is set, media 0us maps directly to frame 0
        if (mediaUs <= 0) return 0;
        return static_cast<uint64_t>(std::llround((static_cast<double>(mediaUs) * sampleRate_) / 1000000.0));
    }

    int64_t deltaUs = mediaUs - anchorMediaUs_;
    int64_t deltaFrames = static_cast<int64_t>(std::llround((static_cast<double>(deltaUs) * sampleRate_) / 1000000.0));
    int64_t targetFrame = static_cast<int64_t>(anchorMasterFrame_) + deltaFrames;
    return static_cast<uint64_t>(std::max<int64_t>(0, targetFrame));
}

int64_t TimelineModel::masterFrameToMediaUs(uint64_t masterFrame) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!hasAnchor_) {
        return static_cast<int64_t>(std::llround((static_cast<double>(masterFrame) * 1000000.0) / sampleRate_));
    }

    int64_t deltaFrames = static_cast<int64_t>(masterFrame) - static_cast<int64_t>(anchorMasterFrame_);
    int64_t deltaUs = static_cast<int64_t>(std::llround((static_cast<double>(deltaFrames) * 1000000.0) / sampleRate_));
    return anchorMediaUs_ + deltaUs;
}

double TimelineModel::masterFrameToMediaMs(uint64_t masterFrame) const {
    return static_cast<double>(masterFrameToMediaUs(masterFrame)) / 1000.0;
}

uint64_t TimelineModel::mediaMsToMasterFrame(double mediaMs) const {
    int64_t mediaUs = static_cast<int64_t>(std::llround(mediaMs * 1000.0));
    return mediaUsToMasterFrame(mediaUs);
}

double TimelineModel::calculateAvOffsetMs(double audioTimeMs, double videoTimeMs) {
    return audioTimeMs - videoTimeMs;
}

void TimelineModel::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    anchorMediaUs_ = 0;
    anchorMasterFrame_ = 0;
    hasAnchor_ = false;
}

void TimelineModel::onSeek(int64_t newMediaUs, uint64_t currentMasterFrame) {
    std::lock_guard<std::mutex> lock(mutex_);
    anchorMediaUs_ = newMediaUs;
    anchorMasterFrame_ = currentMasterFrame;
    hasAnchor_ = true;
}

void TimelineModel::onDiscontinuity(int64_t newMediaUs, uint64_t currentMasterFrame) {
    onSeek(newMediaUs, currentMasterFrame);
}

int64_t TimelineModel::anchorMediaUs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return anchorMediaUs_;
}

uint64_t TimelineModel::anchorMasterFrame() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return anchorMasterFrame_;
}

bool TimelineModel::hasAnchor() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return hasAnchor_;
}

} // namespace syncwave
