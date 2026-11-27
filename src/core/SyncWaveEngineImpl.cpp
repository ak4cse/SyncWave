#include "ISyncWaveEngine.h"
#include "../audio/AudioEngine.h"
#include "../av/VlcMediaEngine.h"
#include "../windows/DefaultAudioSwitcher.h"
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <chrono>
#include <mutex>

namespace syncwave {

class SyncWaveEngineImpl : public ISyncWaveEngine {
public:
    SyncWaveEngineImpl()
        : startTime_(std::chrono::steady_clock::now())
    {
        deviceManager_ = std::make_unique<DeviceManager>();
        audioEngine_ = std::make_unique<AudioEngine>();

        FILETIME ftCreation, ftExit;
        GetProcessTimes(GetCurrentProcess(), &ftCreation, &ftExit, &lastKernel_, &lastUser_);
        lastCpuSampleTime_ = std::chrono::steady_clock::now();
    }

    ~SyncWaveEngineImpl() override {
        stop();
    }

    std::vector<AudioDevice> enumerateOutputDevices(bool activeOnly) override {
        std::lock_guard<std::mutex> lock(engineMutex_);
        return deviceManager_->enumerateDevices(activeOnly);
    }

    std::vector<AudioDevice> enumerateCaptureDevices(bool activeOnly) override {
        std::lock_guard<std::mutex> lock(engineMutex_);
        return deviceManager_->enumerateCaptureDevices(activeOnly);
    }

    std::optional<AudioDevice> getDefaultOutputDevice() override {
        std::lock_guard<std::mutex> lock(engineMutex_);
        return deviceManager_->getDefaultDevice();
    }

    std::optional<AudioDevice> getDefaultCaptureDevice() override {
        std::lock_guard<std::mutex> lock(engineMutex_);
        return deviceManager_->getDefaultCaptureDevice();
    }

    bool startTone(const std::vector<std::string>& outputIds, double frequencyHz, double volume) override {
        std::lock_guard<std::mutex> lock(engineMutex_);
        stopInternal();

        applySyncSettings();

        ToneParameters params;
        params.frequencyHz = frequencyHz;
        params.volume = volume;
        params.durationSec = 0.0; // Continuous

        activeSource_ = "Tone";
        return audioEngine_->startTone(outputIds, params);
    }

    bool startCapture(const std::string& captureSourceId, const std::vector<std::string>& outputIds) override {
        std::lock_guard<std::mutex> lock(engineMutex_);
        stopInternal();

        applySyncSettings();

        std::vector<AudioDevice> outDevs;
        for (const auto& id : outputIds) {
            if (auto dev = deviceManager_->getDeviceById(id)) {
                outDevs.push_back(*dev);
            }
        }
        auto capDevOpt = deviceManager_->getDeviceById(captureSourceId);
        if (!capDevOpt || outDevs.empty()) {
            lastError_ = "Invalid capture source or output device selection.";
            return false;
        }

        activeSource_ = "LoopbackCapture";
        if (!audioEngine_->startCapture(*capDevOpt, outDevs)) {
            lastError_ = "Failed to start WASAPI loopback capture.";
            activeSource_ = "None";
            return false;
        }
        applyLiveVolumeAndDelay();
        lastError_.clear();
        applyLiveVolumeAndDelay();
        return true;
    }

    bool startAutoSystemCapture(const std::vector<std::string>& outputIds) override {
        std::lock_guard<std::mutex> lock(engineMutex_);
        stopInternal();

        if (outputIds.empty()) {
            lastError_ = "Please select at least one output device.";
            return false;
        }

        auto activeDevs = deviceManager_->enumerateDevices(true);
        std::optional<AudioDevice> captureDev;

        // Prefer a virtual endpoint (e.g. Steam Streaming Speakers) not in outputIds
        auto isSelected = [&](const std::string& id) {
            return std::find(outputIds.begin(), outputIds.end(), id) != outputIds.end();
        };

        for (const auto& dev : activeDevs) {
            if (!isSelected(dev.id) && dev.name.find("Steam Streaming Speakers") != std::string::npos) {
                captureDev = dev;
                break;
            }
        }
        if (!captureDev) {
            for (const auto& dev : activeDevs) {
                if (!isSelected(dev.id)) {
                    captureDev = dev;
                    break;
                }
            }
        }
        if (!captureDev) {
            lastError_ = "No separate unused playback endpoint found for loopback capture (to avoid feedback, uncheck at least one endpoint such as Steam Streaming Speakers).";
            return false;
        }

        // Save current default playback device and auto-switch Windows default to captureDev
        if (auto currentDef = deviceManager_->getDefaultDevice()) {
            if (currentDef->id != captureDev->id) {
                originalDefaultDeviceId_ = currentDef->id;
                if (DefaultAudioSwitcher::setDefaultPlaybackDevice(captureDev->id)) {
                    autoSwitchedDefault_ = true;
                }
            }
        }

        std::vector<AudioDevice> outDevs;
        for (const auto& id : outputIds) {
            if (auto dev = deviceManager_->getDeviceById(id)) {
                outDevs.push_back(*dev);
            }
        }

        applySyncSettings();
        activeSource_ = "SystemAudioLoopback";
        if (!audioEngine_->startCapture(*captureDev, outDevs)) {
            lastError_ = "Failed to start WASAPI loopback capture on " + captureDev->name;
            restoreDefaultDeviceIfNeeded();
            activeSource_ = "None";
            return false;
        }
        applyLiveVolumeAndDelay();
        lastError_.clear();
        applyLiveVolumeAndDelay();
        return true;
    }

    bool startMedia(const std::string& filePathOrUrl, const std::vector<std::string>& outputIds) override {
        std::lock_guard<std::mutex> lock(engineMutex_);
        stopInternal();

        activeSource_ = "MediaVlc";
        auto& router = audioEngine_->outputRouter();
        router.clearOutputs();

        size_t addedCount = 0;
        for (const auto& id : outputIds) {
            auto dev = deviceManager_->getDeviceById(id);
            if (dev) {
                router.addOutput(*dev);
                addedCount++;
            }
        }

        if (addedCount == 0) {
            lastError_ = "No valid output devices selected.";
            activeSource_ = "None";
            return false;
        }

        const auto masterFmt = MasterAudioBus::canonicalFormat();
        audioEngine_->masterBus().initialize(masterFmt, masterFmt.sampleRate);
        audioEngine_->masterBus().reset();

        if (!router.initializeOutputs(masterFmt.sampleRate, masterFmt.channels)) {
            lastError_ = "Failed to initialize selected WASAPI output devices.";
            router.clearOutputs();
            activeSource_ = "None";
            return false;
        }

        applySyncSettings();

        if (!router.startOutputs()) {
            lastError_ = "Failed to start WASAPI output streams.";
            router.clearOutputs();
            activeSource_ = "None";
            return false;
        }

        mediaEngine_ = std::make_unique<VlcMediaEngine>(audioEngine_->masterBus(), router, videoHwnd_);
        mediaEngine_->setAudioDelayMs(avSyncDelayMs_);
        if (!mediaEngine_->load(filePathOrUrl)) {
            lastError_ = mediaEngine_->lastError();
            router.stopOutputs();
            router.clearOutputs();
            mediaEngine_.reset();
            activeSource_ = "None";
            return false;
        }

        if (!mediaEngine_->play()) {
            lastError_ = mediaEngine_->lastError();
            router.stopOutputs();
            router.clearOutputs();
            mediaEngine_.reset();
            activeSource_ = "None";
            return false;
        }

        lastError_.clear();
        applyLiveVolumeAndDelay();
        return true;
    }

    void stop() override {
        std::lock_guard<std::mutex> lock(engineMutex_);
        stopInternal();
    }

    void pause() override {
        std::lock_guard<std::mutex> lock(engineMutex_);
        if (mediaEngine_) {
            mediaEngine_->pause();
        }
    }

    void resume() override {
        std::lock_guard<std::mutex> lock(engineMutex_);
        if (mediaEngine_) {
            mediaEngine_->resume();
        }
    }

    bool seekMedia(double targetTimeMs) override {
        std::lock_guard<std::mutex> lock(engineMutex_);
        if (mediaEngine_) {
            return mediaEngine_->seek(targetTimeMs);
        }
        return false;
    }

    void setSyncMode(const std::string& mode) override {
        std::lock_guard<std::mutex> lock(engineMutex_);
        syncMode_ = mode;
        applySyncSettings();
    }

    void setManualDelays(const std::vector<double>& delaysMs) override {
        std::lock_guard<std::mutex> lock(engineMutex_);
        manualDelays_ = delaysMs;
        audioEngine_->setManualDelays(delaysMs);
        auto& router = audioEngine_->outputRouter();
        for (size_t i = 0; i < delaysMs.size(); ++i) {
            router.setDeviceDelayMs(i, delaysMs[i]);
        }
    }

    void setCalibrationOffsets(const std::vector<double>& offsetsMs) override {
        std::lock_guard<std::mutex> lock(engineMutex_);
        calibrationOffsets_ = offsetsMs;
        audioEngine_->setCalibrationOffsets(offsetsMs);
    }

    void setDeviceVolume(size_t index, float volume) override {
        std::lock_guard<std::mutex> lock(engineMutex_);
        if (index >= deviceVolumes_.size()) {
            deviceVolumes_.resize(index + 1, 1.0f);
        }
        deviceVolumes_[index] = volume;
        audioEngine_->outputRouter().setDeviceVolume(index, volume);
    }

    void setDeviceMuted(size_t index, bool muted) override {
        std::lock_guard<std::mutex> lock(engineMutex_);
        if (index >= deviceMutes_.size()) {
            deviceMutes_.resize(index + 1, false);
        }
        deviceMutes_[index] = muted;
        audioEngine_->outputRouter().setDeviceMuted(index, muted);
    }

    void setVideoWindow(void* hwnd) override {
        std::lock_guard<std::mutex> lock(engineMutex_);
        videoHwnd_ = hwnd;
        if (mediaEngine_) {
            mediaEngine_->setVideoWindow(hwnd);
        }
    }

    void setAvSyncDelayMs(double delayMs) override {
        std::lock_guard<std::mutex> lock(engineMutex_);
        avSyncDelayMs_ = delayMs;
        if (mediaEngine_) {
            mediaEngine_->setAudioDelayMs(delayMs);
        }
    }

    bool isRunning() const override {
        if (mediaEngine_) {
            return mediaEngine_->isPlaying();
        }
        return audioEngine_->isRunning();
    }

    FullDiagnosticsSnapshot getDiagnostics() override {
        std::lock_guard<std::mutex> lock(engineMutex_);
        FullDiagnosticsSnapshot snap;
        snap.isEngineRunning = isRunning();
        snap.activeSource = activeSource_;

        auto& bus = audioEngine_->masterBus();
        snap.masterSampleRate = bus.format().sampleRate;
        snap.masterChannels = bus.format().channels;
        snap.masterFramesProduced = bus.totalFramesWritten();
        snap.masterFramesDistributed = bus.totalFramesRead();
        snap.masterBusAvailableFrames = bus.availableFrames();
        snap.masterBusCapacityFrames = bus.capacityFrames();

        auto engineDiag = audioEngine_->getDiagnostics();
        for (const auto& out : engineDiag.outputs) {
            EndpointDiagnostics ep;
            ep.deviceId = out.deviceId;
            ep.friendlyName = out.deviceName;
            ep.state = (out.state == OutputState::Running) ? "Running" :
                       (out.state == OutputState::Stopped) ? "Stopped" :
                       (out.state == OutputState::Initialized) ? "Initialized" : "Error";
            ep.sampleRate = out.format.sampleRate;
            ep.channels = out.format.channels;
            ep.isAvailable = (out.state == OutputState::Running);

            ep.queueFrames = out.queueAvailable;
            ep.queueCapacityFrames = out.queueCapacity;
            ep.queueUnderruns = out.queueUnderruns;
            ep.queueOverruns = out.queueOverruns;
            ep.wasapiUnderruns = out.wasapiUnderruns;

            ep.configuredDelayMs = out.configuredDelayMs;
            ep.appliedDelayFrames = static_cast<uint32_t>(out.appliedDelayFrames);
            ep.estimatedSoftwareLatencyMs = out.latencyModel.estimatedSoftwareLatencyMs;
            ep.calibrationOffsetMs = out.latencyModel.optionalCalibrationOffsetMs;
            ep.effectiveLatencyMs = out.latencyModel.effectiveLatencyMs;
            ep.syncState = out.latencyModel.syncState;

            ep.driftState = (out.driftState == DriftCorrectionState::Locked) ? "Locked" :
                            (out.driftState == DriftCorrectionState::Correcting) ? "Correcting" :
                            (out.driftState == DriftCorrectionState::Measuring) ? "Measuring" :
                            (out.driftState == DriftCorrectionState::Uncertain) ? "Uncertain" : "Disabled";
            ep.phaseErrorMs = out.syncError.phaseErrorMs;
            ep.filteredPhaseErrorMs = out.syncError.filteredPhaseErrorMs;
            ep.driftPpm = out.rawDriftPpm;
            ep.filteredDriftPpm = out.filteredDriftPpm;
            ep.targetRateAdjustmentPpm = out.targetRateAdjustmentPpm;
            ep.activeRateAdjustmentPpm = out.currentRateAdjustmentPpm;
            ep.driftConfidence = out.syncError.confidence;

            snap.endpoints.push_back(ep);
        }

        if (mediaEngine_) {
            auto sync = mediaEngine_->getSyncState();
            snap.media.playbackState = sync.playbackState;
            snap.media.mediaPositionMs = sync.mediaPositionMs;
            snap.media.durationMs = mediaEngine_->durationMs();
            snap.media.videoPositionMs = sync.videoPositionMs;
            snap.media.audioMasterPositionMs = sync.audioMasterPositionMs;
            snap.media.audioAcousticPositionMs = sync.audioAcousticPositionMs;
            snap.media.audioVideoOffsetMs = sync.audioVideoOffsetMs;
            snap.media.playbackRate = static_cast<float>(sync.playbackRate);
            snap.media.isSynchronized = sync.isSynchronized;
            snap.media.isLoaded = true;
            snap.media.isSeeking = false;
        }

        // Host system usage
        snap.system.uptimeSec = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime_).count();
        sampleSystemUsage(snap.system);

        return snap;
    }

    std::string getLastError() const override {
        std::lock_guard<std::mutex> lock(engineMutex_);
        if (!lastError_.empty()) {
            return lastError_;
        }
        if (mediaEngine_) {
            return mediaEngine_->lastError();
        }
        return {};
    }

private:
    void stopInternal() {
        if (mediaEngine_) {
            mediaEngine_->stop();
            mediaEngine_.reset();
        }
        audioEngine_->stop();
        restoreDefaultDeviceIfNeeded();
        activeSource_ = "None";
    }

    void restoreDefaultDeviceIfNeeded() {
        if (autoSwitchedDefault_ && !originalDefaultDeviceId_.empty()) {
            DefaultAudioSwitcher::setDefaultPlaybackDevice(originalDefaultDeviceId_);
            autoSwitchedDefault_ = false;
            originalDefaultDeviceId_.clear();
        }
    }

    void applyLiveVolumeAndDelay() {
        auto& router = audioEngine_->outputRouter();
        for (size_t i = 0; i < manualDelays_.size(); ++i) {
            router.setDeviceDelayMs(i, manualDelays_[i]);
        }
        for (size_t i = 0; i < deviceVolumes_.size(); ++i) {
            router.setDeviceVolume(i, deviceVolumes_[i]);
        }
        for (size_t i = 0; i < deviceMutes_.size(); ++i) {
            router.setDeviceMuted(i, deviceMutes_[i]);
        }
    }

    void applySyncSettings() {
        if (syncMode_ == "adaptive") {
            audioEngine_->requestAutoSync(true);
            audioEngine_->enableDriftCorrection(true);
        } else if (syncMode_ == "software") {
            audioEngine_->requestAutoSync(true);
            audioEngine_->enableDriftCorrection(false);
        } else {
            audioEngine_->requestAutoSync(false);
            audioEngine_->enableDriftCorrection(false);
        }

        if (!manualDelays_.empty()) {
            audioEngine_->setManualDelays(manualDelays_);
        }
        if (!calibrationOffsets_.empty()) {
            audioEngine_->setCalibrationOffsets(calibrationOffsets_);
        }
    }

    void sampleSystemUsage(SystemDiagnostics& sys) {
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
            sys.memoryUsageMb = static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
        }

        FILETIME ftCreation, ftExit, ftKernel, ftUser;
        if (GetProcessTimes(GetCurrentProcess(), &ftCreation, &ftExit, &ftKernel, &ftUser)) {
            ULARGE_INTEGER kOld, kNew, uOld, uNew;
            kOld.LowPart = lastKernel_.dwLowDateTime; kOld.HighPart = lastKernel_.dwHighDateTime;
            kNew.LowPart = ftKernel.dwLowDateTime; kNew.HighPart = ftKernel.dwHighDateTime;
            uOld.LowPart = lastUser_.dwLowDateTime; uOld.HighPart = lastUser_.dwHighDateTime;
            uNew.LowPart = ftUser.dwLowDateTime; uNew.HighPart = ftUser.dwHighDateTime;

            uint64_t kernelDiff = kNew.QuadPart - kOld.QuadPart;
            uint64_t userDiff = uNew.QuadPart - uOld.QuadPart;
            uint64_t totalProcTime = kernelDiff + userDiff;

            auto now = std::chrono::steady_clock::now();
            double wallElapsedSec = std::chrono::duration<double>(now - lastCpuSampleTime_).count();
            if (wallElapsedSec > 0.0) {
                double procSec = static_cast<double>(totalProcTime) / 10000000.0;
                sys.processCpuPercent = (procSec / wallElapsedSec) * 100.0;
            }

            lastKernel_ = ftKernel;
            lastUser_ = ftUser;
            lastCpuSampleTime_ = now;
        }
    }

    mutable std::mutex engineMutex_;
    std::unique_ptr<DeviceManager> deviceManager_;
    std::unique_ptr<AudioEngine> audioEngine_;
    std::unique_ptr<VlcMediaEngine> mediaEngine_;

    std::string activeSource_ = "None";
    std::string syncMode_ = "adaptive";
    std::string lastError_;
    std::string originalDefaultDeviceId_;
    bool autoSwitchedDefault_ = false;
    void* videoHwnd_ = nullptr;
    double avSyncDelayMs_ = 0.0;
    std::vector<double> manualDelays_;
    std::vector<double> calibrationOffsets_;
    std::vector<float> deviceVolumes_;
    std::vector<bool> deviceMutes_;

    std::chrono::steady_clock::time_point startTime_;
    FILETIME lastKernel_{}, lastUser_{};
    std::chrono::steady_clock::time_point lastCpuSampleTime_;
};

std::unique_ptr<ISyncWaveEngine> createSyncWaveEngine() {
    return std::make_unique<SyncWaveEngineImpl>();
}

} // namespace syncwave
