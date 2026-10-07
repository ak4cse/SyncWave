#include "../src/windows/ComHelper.h"
#include "../src/windows/DeviceManager.h"
#include "../src/windows/DeviceNotification.h"
#include "../src/windows/WasapiOutput.h"
#include "../src/audio/AudioFormat.h"
#include "../src/audio/ToneGenerator.h"
#include "../src/audio/RingBuffer.h"
#include "../src/audio/MasterAudioBus.h"
#include "../src/audio/AudioEngine.h"
#include "../src/audio/DeviceOutput.h"
#include "../src/audio/OutputRouter.h"
#include "../src/dsp/Resampler.h"
#include "../src/windows/WasapiCapture.h"
#include "../src/sync/DeviceClock.h"
#include "../src/sync/DriftEstimator.h"
#include "../src/app/CommandInterface.h"
#include "../src/sync/SyncPulseGenerator.h"
#include "../src/sync/DelayBuffer.h"
#include "../src/sync/OutputLatencyModel.h"
#include "../src/sync/SyncController.h"
#include "../src/sync/SyncError.h"
#include "../src/sync/FilteredDriftEstimator.h"
#include "../src/dsp/ChirpGenerator.h"
#include "../src/dsp/CorrelationDetector.h"
#include "../src/sync/CalibrationStore.h"
#include "../src/sync/AcousticCalibrator.h"
#include "../src/av/AudioTimestamp.h"
#include "../src/av/TimelineModel.h"
#include "../src/av/DeterministicMediaSource.h"
#include "../src/av/VlcMediaEngine.h"
#include "../src/core/ISyncWaveEngine.h"
#include "../src/core/DiagnosticsSnapshot.h"
#include "../src/core/SyncWaveConfig.h"

#include <iostream>
#include <cassert>
#include <string>
#include <filesystem>
#include <vector>
#include <cmath>
#include <thread>
#include <chrono>
#include <atomic>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>

static int g_testsPassed = 0;
static int g_testsFailed = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (cond) { \
            std::cout << "  [PASS] " << msg << "\n"; \
            g_testsPassed++; \
        } else { \
            std::cerr << "  [FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            g_testsFailed++; \
        } \
    } while (0)

void testStringConversions() {
    std::cout << "[TEST] String and HRESULT utilities\n";

    std::wstring originalW = L"Test Audio Device (\u00A9 2026)";
    std::string utf8 = syncwave::wideToUtf8(originalW);
    TEST_ASSERT(!utf8.empty(), "UTF-8 conversion produces non-empty string");

    std::wstring roundTrip = syncwave::utf8ToWide(utf8);
    TEST_ASSERT(roundTrip == originalW, "Wide-to-UTF8 and UTF8-to-Wide roundtrip matches");

    std::string hexHr = syncwave::formatHResult(0x88890008);
    TEST_ASSERT(hexHr == "0x88890008", "formatHResult formats 32-bit hex with 0x prefix");

    std::string okHr = syncwave::formatHResult(S_OK);
    TEST_ASSERT(okHr == "0x00000000", "formatHResult formats S_OK as 0x00000000");
}

void testDeviceDataModel() {
    std::cout << "[TEST] Device Data Model & State Mapping\n";

    TEST_ASSERT(syncwave::deviceStateToString(syncwave::DeviceState::Active) == "Active", "DeviceState::Active maps to 'Active'");
    TEST_ASSERT(syncwave::deviceStateToString(syncwave::DeviceState::Disabled) == "Disabled", "DeviceState::Disabled maps to 'Disabled'");
    TEST_ASSERT(syncwave::deviceStateToString(syncwave::DeviceState::NotPresent) == "Not Present", "DeviceState::NotPresent maps to 'Not Present'");
    TEST_ASSERT(syncwave::deviceStateToString(syncwave::DeviceState::Unplugged) == "Unplugged", "DeviceState::Unplugged maps to 'Unplugged'");
    TEST_ASSERT(syncwave::deviceStateToString(syncwave::DeviceState::Unknown) == "Unknown", "DeviceState::Unknown maps to 'Unknown'");

    syncwave::AudioDevice dev;
    dev.id = "{0.0.0.00000000}.{test-id}";
    dev.name = "Virtual Output";
    dev.state = syncwave::DeviceState::Active;
    dev.isActive = true;
    dev.isDefault = true;

    TEST_ASSERT(dev.stateString() == "Active", "AudioDevice::stateString() returns Active");
    TEST_ASSERT(dev.isActive, "AudioDevice::isActive is true");
    TEST_ASSERT(dev.isDefault, "AudioDevice::isDefault is true");
}

void testEventModel() {
    std::cout << "[TEST] Event Data Model & Event Type Mapping\n";

    TEST_ASSERT(syncwave::deviceEventTypeToString(syncwave::DeviceEventType::Added) == "Device Added", "Added event maps to 'Device Added'");
    TEST_ASSERT(syncwave::deviceEventTypeToString(syncwave::DeviceEventType::Removed) == "Device Removed", "Removed event maps to 'Device Removed'");
    TEST_ASSERT(syncwave::deviceEventTypeToString(syncwave::DeviceEventType::StateChanged) == "Device State Changed", "StateChanged event maps to 'Device State Changed'");
    TEST_ASSERT(syncwave::deviceEventTypeToString(syncwave::DeviceEventType::DefaultChanged) == "Default Device Changed", "DefaultChanged event maps to 'Default Device Changed'");
    TEST_ASSERT(syncwave::deviceEventTypeToString(syncwave::DeviceEventType::PropertyChanged) == "Device Property Changed", "PropertyChanged event maps to 'Device Property Changed'");

    syncwave::DeviceEvent ev;
    ev.type = syncwave::DeviceEventType::DefaultChanged;
    ev.deviceId = "{0.0.0.00000000}.{default-dev}";
    ev.newState = syncwave::DeviceState::Active;
    ev.details = "Role: Console";

    TEST_ASSERT(ev.typeString() == "Default Device Changed", "DeviceEvent::typeString() matches");
    TEST_ASSERT(ev.deviceId == "{0.0.0.00000000}.{default-dev}", "DeviceEvent::deviceId matches");
    TEST_ASSERT(ev.details == "Role: Console", "DeviceEvent::details matches");
}

class MockListener : public syncwave::INotificationListener {
public:
    int addedCount = 0;
    int removedCount = 0;
    int stateChangedCount = 0;
    int defaultChangedCount = 0;
    int propertyChangedCount = 0;

    std::string lastId;
    DWORD lastState = 0;
    EDataFlow lastFlow = eRender;
    ERole lastRole = eConsole;

    void onDeviceAdded(const std::string& deviceId) override {
        addedCount++;
        lastId = deviceId;
    }

    void onDeviceRemoved(const std::string& deviceId) override {
        removedCount++;
        lastId = deviceId;
    }

    void onDeviceStateChanged(const std::string& deviceId, DWORD newState) override {
        stateChangedCount++;
        lastId = deviceId;
        lastState = newState;
    }

    void onDefaultDeviceChanged(EDataFlow flow, ERole role, const std::string& defaultDeviceId) override {
        defaultChangedCount++;
        lastId = defaultDeviceId;
        lastFlow = flow;
        lastRole = role;
    }

    void onPropertyValueChanged(const std::string& deviceId, const PROPERTYKEY& /*key*/) override {
        propertyChangedCount++;
        lastId = deviceId;
    }
};

void testNotificationClientComLifecycle() {
    std::cout << "[TEST] NotificationClient COM Lifecycle and Dispatching\n";

    MockListener mock;
    auto* client = new syncwave::DeviceNotificationClient(&mock);

    ULONG count = client->AddRef();
    TEST_ASSERT(count == 2, "AddRef increments count to 2");

    count = client->Release();
    TEST_ASSERT(count == 1, "Release decrements count to 1");

    void* pUnknown = nullptr;
    HRESULT hr = client->QueryInterface(IID_IUnknown, &pUnknown);
    TEST_ASSERT(SUCCEEDED(hr) && pUnknown != nullptr, "QueryInterface for IID_IUnknown succeeds");
    if (pUnknown) {
        static_cast<IUnknown*>(pUnknown)->Release();
    }

    void* pClient = nullptr;
    hr = client->QueryInterface(__uuidof(IMMNotificationClient), &pClient);
    TEST_ASSERT(SUCCEEDED(hr) && pClient != nullptr, "QueryInterface for IMMNotificationClient succeeds");
    if (pClient) {
        static_cast<IMMNotificationClient*>(pClient)->Release();
    }

    void* pInvalid = nullptr;
    GUID fakeGuid = { 0x12345678, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0 } };
    hr = client->QueryInterface(fakeGuid, &pInvalid);
    TEST_ASSERT(hr == E_NOINTERFACE && pInvalid == nullptr, "QueryInterface for unsupported GUID returns E_NOINTERFACE");

    client->OnDeviceAdded(L"{mock-device-1}");
    TEST_ASSERT(mock.addedCount == 1 && mock.lastId == "{mock-device-1}", "OnDeviceAdded forwarded to listener");

    client->OnDeviceStateChanged(L"{mock-device-1}", DEVICE_STATE_ACTIVE);
    TEST_ASSERT(mock.stateChangedCount == 1 && mock.lastState == DEVICE_STATE_ACTIVE, "OnDeviceStateChanged forwarded to listener");

    client->OnDefaultDeviceChanged(eRender, eMultimedia, L"{mock-device-1}");
    TEST_ASSERT(mock.defaultChangedCount == 1 && mock.lastRole == eMultimedia, "OnDefaultDeviceChanged forwarded to listener");

    client->OnPropertyValueChanged(L"{mock-device-1}", PKEY_Device_FriendlyName);
    TEST_ASSERT(mock.propertyChangedCount == 1, "OnPropertyValueChanged forwarded to listener");

    client->OnDeviceRemoved(L"{mock-device-1}");
    TEST_ASSERT(mock.removedCount == 1 && mock.lastId == "{mock-device-1}", "OnDeviceRemoved forwarded to listener");

    count = client->Release();
    TEST_ASSERT(count == 0, "Final Release deletes client (count reaches 0)");
}

void testDeviceManagerMonitoringLifecycle() {
    std::cout << "[TEST] DeviceManager Monitoring Lifecycle\n";

    try {
        syncwave::DeviceManager dm;
        TEST_ASSERT(!dm.isMonitoring(), "DeviceManager initially not monitoring");

        int eventCount = 0;
        bool started = dm.startMonitoring([&](const syncwave::DeviceEvent& /*ev*/) {
            eventCount++;
        });

        TEST_ASSERT(started, "startMonitoring returns true on host");
        TEST_ASSERT(dm.isMonitoring(), "isMonitoring() reports true while monitoring");

        bool startedAgain = dm.startMonitoring([](const syncwave::DeviceEvent&) {});
        TEST_ASSERT(startedAgain, "Second startMonitoring call is idempotent and returns true");

        dm.stopMonitoring();
        TEST_ASSERT(!dm.isMonitoring(), "isMonitoring() reports false after stopMonitoring");

        dm.stopMonitoring();
        TEST_ASSERT(!dm.isMonitoring(), "Second stopMonitoring call is idempotent and safe");
    } catch (const std::exception& e) {
        std::cerr << "Monitoring lifecycle test error: " << e.what() << "\n";
        TEST_ASSERT(false, "DeviceManager monitoring threw unexpected exception");
    }
}

void testDeviceManagerEnumerationIntegration() {
    std::cout << "[TEST] DeviceManager Core Audio Integration\n";

    try {
        syncwave::DeviceManager dm;

        auto activeDevices = dm.enumerateDevices(true);
        std::cout << "    Found " << activeDevices.size() << " active render endpoint(s)\n";
        TEST_ASSERT(true, "DeviceManager::enumerateDevices(true) executed without throwing");

        auto allDevices = dm.enumerateDevices(false);
        std::cout << "    Found " << allDevices.size() << " total render endpoint(s)\n";
        TEST_ASSERT(allDevices.size() >= activeDevices.size(), "Total endpoints >= active endpoints");

        for (const auto& d : activeDevices) {
            TEST_ASSERT(!d.id.empty(), "Active device ID is non-empty");
            TEST_ASSERT(!d.name.empty(), "Active device name is non-empty");
            TEST_ASSERT(d.isActive, "Active device marked as isActive == true");
        }

        auto defaultDev = dm.getDefaultDevice();
        if (defaultDev) {
            std::cout << "    Default device: " << defaultDev->name << " [ID: " << defaultDev->id << "]\n";
            TEST_ASSERT(defaultDev->isDefault, "Default device flag is true");
            TEST_ASSERT(!defaultDev->id.empty(), "Default device ID is non-empty");

            auto queried = dm.getDeviceById(defaultDev->id);
            TEST_ASSERT(queried.has_value(), "getDeviceById successfully retrieves default device");
            if (queried) {
                TEST_ASSERT(queried->name == defaultDev->name, "Retrieved device name matches default device name");
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Integration test error: " << e.what() << "\n";
        TEST_ASSERT(false, "DeviceManager initialization or enumeration threw exception");
    }
}

void testAudioFormat() {
    std::cout << "[TEST] AudioFormat Representation and Calculations\n";

    syncwave::AudioFormat fmt;
    fmt.sampleRate = 48000;
    fmt.channels = 2;
    fmt.bitsPerSample = 32;
    fmt.blockAlign = 8;
    fmt.sampleType = syncwave::SampleType::Float32;

    TEST_ASSERT(fmt.isFloat(), "AudioFormat::isFloat returns true for Float32");
    TEST_ASSERT(fmt.bytesPerFrame() == 8, "bytesPerFrame is 8 for 2ch 32-bit");
    TEST_ASSERT(fmt.bytesPerSecond() == 48000 * 8, "bytesPerSecond is 384000 for 48kHz 2ch 32-bit");
    TEST_ASSERT(!fmt.formatString().empty(), "formatString produces descriptive text");

    syncwave::AudioFormat pcm16;
    pcm16.sampleRate = 44100;
    pcm16.channels = 2;
    pcm16.bitsPerSample = 16;
    pcm16.blockAlign = 4;
    pcm16.sampleType = syncwave::SampleType::Int16;

    TEST_ASSERT(!pcm16.isFloat(), "AudioFormat::isFloat returns false for Int16");
    TEST_ASSERT(pcm16.bytesPerFrame() == 4, "bytesPerFrame is 4 for 2ch 16-bit");
    TEST_ASSERT(pcm16.bytesPerSecond() == 44100 * 4, "bytesPerSecond is 176400 for 44.1kHz 2ch 16-bit");
}

void testToneGenerator() {
    std::cout << "[TEST] ToneGenerator Math, Bounds, and Buffer Generation\n";

    syncwave::ToneGenerator gen(440.0, 0.25);
    TEST_ASSERT(std::abs(gen.frequency() - 440.0) < 1e-6, "Initial frequency is 440 Hz");
    TEST_ASSERT(std::abs(gen.volume() - 0.25) < 1e-6, "Initial volume is 0.25");

    gen.setVolume(1.5);
    TEST_ASSERT(std::abs(gen.volume() - 1.0) < 1e-6, "Volume clamped to 1.0 maximum");
    gen.setVolume(-0.5);
    TEST_ASSERT(std::abs(gen.volume() - 0.0) < 1e-6, "Volume clamped to 0.0 minimum");

    gen.setFrequency(50000.0);
    TEST_ASSERT(std::abs(gen.frequency() - 24000.0) < 1e-6, "Frequency clamped to 24000 Hz maximum");
    gen.setFrequency(-10.0);
    TEST_ASSERT(std::abs(gen.frequency() - 1.0) < 1e-6, "Frequency clamped to 1.0 Hz minimum");

    gen.setFrequency(440.0);
    gen.setVolume(0.5);
    gen.resetPhase();

    syncwave::AudioFormat fmt;
    fmt.sampleRate = 48000;
    fmt.channels = 2;
    fmt.bitsPerSample = 32;
    fmt.blockAlign = 8;
    fmt.sampleType = syncwave::SampleType::Float32;

    constexpr uint32_t FRAME_COUNT = 480;
    std::vector<float> buffer(FRAME_COUNT * 2, 0.0f);
    gen.generateFrames(reinterpret_cast<uint8_t*>(buffer.data()), FRAME_COUNT, fmt);

    float maxAmp = 0.0f;
    bool inBounds = true;
    for (float s : buffer) {
        if (std::abs(s) > 0.5001f) {
            inBounds = false;
        }
        if (std::abs(s) > maxAmp) {
            maxAmp = std::abs(s);
        }
    }
    TEST_ASSERT(inBounds, "All generated Float32 samples within [-0.5, +0.5] volume bounds");
    TEST_ASSERT(maxAmp > 0.45f, "Waveform reaches expected peak amplitude (> 0.45)");

    double phase1 = gen.currentPhase();
    gen.generateFrames(reinterpret_cast<uint8_t*>(buffer.data()), FRAME_COUNT, fmt);
    double phase2 = gen.currentPhase();
    TEST_ASSERT(phase2 != phase1, "Phase advances continuously across consecutive chunks");

    syncwave::AudioFormat fmt16;
    fmt16.sampleRate = 48000;
    fmt16.channels = 2;
    fmt16.bitsPerSample = 16;
    fmt16.blockAlign = 4;
    fmt16.sampleType = syncwave::SampleType::Int16;

    std::vector<int16_t> buffer16(FRAME_COUNT * 2, 0);
    gen.generateFrames(reinterpret_cast<uint8_t*>(buffer16.data()), FRAME_COUNT, fmt16);

    int16_t maxIntAmp = 0;
    for (int16_t s : buffer16) {
        if (std::abs(s) > maxIntAmp) {
            maxIntAmp = std::abs(s);
        }
    }
    TEST_ASSERT(maxIntAmp > 14000 && maxIntAmp <= 16384, "Int16 samples scale correctly to ~50% amplitude");
}

void testWasapiOutputIntegration() {
    std::cout << "[TEST] WasapiOutput Real-Time Render Integration\n";

    try {
        syncwave::DeviceManager dm;
        auto defaultDev = dm.getDefaultDevice();
        if (!defaultDev) {
            std::cout << "    (No default audio render endpoint available for WASAPI integration test)\n";
            return;
        }

        std::cout << "    Testing on default endpoint: " << defaultDev->name << "\n";

        syncwave::WasapiOutput output;
        TEST_ASSERT(output.state() == syncwave::OutputState::Uninitialized, "WasapiOutput initially Uninitialized");

        bool opened = output.open(defaultDev->id);
        TEST_ASSERT(opened, "WasapiOutput::open() succeeded on default endpoint");
        TEST_ASSERT(output.state() == syncwave::OutputState::Opened, "WasapiOutput in Opened state");

        bool initialized = output.initialize();
        TEST_ASSERT(initialized, "WasapiOutput::initialize() succeeded in shared event-driven mode");
        TEST_ASSERT(output.state() == syncwave::OutputState::Initialized, "WasapiOutput in Initialized state");

        auto fmt = output.format();
        TEST_ASSERT(fmt.sampleRate >= 44100, "Device sample rate is >= 44.1 kHz");
        TEST_ASSERT(fmt.channels >= 1, "Device channels is >= 1");
        TEST_ASSERT(output.bufferFrameCount() > 0, "Buffer frame count > 0");

        syncwave::ToneGenerator toneGen(440.0, 0.1);
        bool started = output.start([&](uint8_t* pBuf, uint32_t frames, const syncwave::AudioFormat& f) {
            toneGen.generateFrames(pBuf, frames, f);
        });

        TEST_ASSERT(started, "WasapiOutput::start() succeeded");
        TEST_ASSERT(output.state() == syncwave::OutputState::Running, "WasapiOutput in Running state");

        std::this_thread::sleep_for(std::chrono::milliseconds(150));

        TEST_ASSERT(output.framesRendered() > 0, "Audio frames were rendered during playback");

        auto clock = output.getClockPosition();
        TEST_ASSERT(clock.second > 0, "IAudioClock frequency is reported and valid (> 0)");

        output.stop();
        TEST_ASSERT(output.state() == syncwave::OutputState::Stopped, "WasapiOutput in Stopped state after stop()");

        output.close();
        TEST_ASSERT(output.state() == syncwave::OutputState::Closed, "WasapiOutput in Closed state after close()");
    } catch (const std::exception& e) {
        std::cerr << "WASAPI Integration error: " << e.what() << "\n";
        TEST_ASSERT(false, "WasapiOutput integration test threw unexpected exception");
    }
}

void testRingBufferBasics() {
    std::cout << "[TEST] RingBuffer Operations, Wraparound, and Bounds\n";

    syncwave::RingBuffer rb(100, 2);
    TEST_ASSERT(rb.capacityFrames() == 100, "RingBuffer capacity is 100 frames");
    TEST_ASSERT(rb.channels() == 2, "RingBuffer channels is 2");
    TEST_ASSERT(rb.availableToRead() == 0, "Initially availableToRead is 0");
    TEST_ASSERT(rb.availableToWrite() == 100, "Initially availableToWrite is 100");

    // Test empty read: should return 0, zero-fill destination, and increment underruns
    std::vector<float> readBuf(20, 99.0f);
    size_t readCount = rb.read(readBuf.data(), 10);
    TEST_ASSERT(readCount == 0, "Reading from empty buffer returns 0 frames");
    TEST_ASSERT(rb.underruns() == 10, "Underruns counter incremented by 10");
    bool allZero = true;
    for (float v : readBuf) {
        if (v != 0.0f) allZero = false;
    }
    TEST_ASSERT(allZero, "Destination buffer zero-padded on underrun");

    // Normal write and read (50 frames)
    std::vector<float> writeData(50 * 2);
    for (size_t i = 0; i < writeData.size(); ++i) {
        writeData[i] = static_cast<float>(i + 1);
    }
    size_t written = rb.write(writeData.data(), 50);
    TEST_ASSERT(written == 50, "Wrote 50 frames successfully");
    TEST_ASSERT(rb.availableToRead() == 50, "availableToRead is now 50");
    TEST_ASSERT(rb.availableToWrite() == 50, "availableToWrite is now 50");

    std::vector<float> outData(50 * 2, 0.0f);
    size_t readActual = rb.read(outData.data(), 50);
    TEST_ASSERT(readActual == 50, "Read 50 frames successfully");
    TEST_ASSERT(outData == writeData, "Read data matches written data exactly");
    TEST_ASSERT(rb.availableToRead() == 0, "availableToRead is 0 after full read");

    // Partial reads: write 40 frames, read 15, then read 25
    std::vector<float> partialData(40 * 2);
    for (size_t i = 0; i < partialData.size(); ++i) {
        partialData[i] = static_cast<float>(i + 100);
    }
    rb.write(partialData.data(), 40);

    std::vector<float> chunk1(15 * 2);
    std::vector<float> chunk2(25 * 2);
    rb.read(chunk1.data(), 15);
    rb.read(chunk2.data(), 25);

    bool matchChunk1 = std::equal(chunk1.begin(), chunk1.end(), partialData.begin());
    bool matchChunk2 = std::equal(chunk2.begin(), chunk2.end(), partialData.begin() + 15 * 2);
    TEST_ASSERT(matchChunk1, "First partial read (15 frames) matches expected data");
    TEST_ASSERT(matchChunk2, "Second partial read (25 frames) matches expected data");

    // Wraparound test:
    // Currently read and write pointers are at 90. Write 40 frames (which wraps around 100 to 30)
    std::vector<float> wrapData(40 * 2);
    for (size_t i = 0; i < wrapData.size(); ++i) {
        wrapData[i] = static_cast<float>(i + 500);
    }
    size_t wrapWritten = rb.write(wrapData.data(), 40);
    TEST_ASSERT(wrapWritten == 40, "Write that wraps around ring buffer boundary succeeds");

    std::vector<float> wrapRead(40 * 2);
    size_t wrapReadCount = rb.read(wrapRead.data(), 40);
    TEST_ASSERT(wrapReadCount == 40, "Read that wraps around ring buffer boundary succeeds");
    TEST_ASSERT(wrapRead == wrapData, "Data read across wraparound matches written data exactly");

    // Overrun test: capacity is 100, buffer is empty. Attempt to write 120 frames
    std::vector<float> overData(120 * 2, 1.0f);
    size_t overWritten = rb.write(overData.data(), 120);
    TEST_ASSERT(overWritten == 100, "write capped at available capacity (100 frames)");
    TEST_ASSERT(rb.overruns() == 20, "Overrun counter incremented by dropped frames (20)");

    // Reset test
    rb.reset();
    TEST_ASSERT(rb.availableToRead() == 0, "availableToRead is 0 after reset");
    TEST_ASSERT(rb.availableToWrite() == 100, "availableToWrite is 100 after reset");
    TEST_ASSERT(rb.overruns() == 0, "Overruns reset to 0");
    TEST_ASSERT(rb.underruns() == 0, "Underruns reset to 0");
}

void testRingBufferDataIntegrity() {
    std::cout << "[TEST] RingBuffer Gradient Sequence Data Integrity\n";

    syncwave::RingBuffer rb(256, 2);
    constexpr size_t TOTAL_FRAMES = 5000;

    std::vector<float> fullSource(TOTAL_FRAMES * 2);
    for (size_t i = 0; i < TOTAL_FRAMES; ++i) {
        fullSource[i * 2 + 0] = static_cast<float>(i) * 0.001f;
        fullSource[i * 2 + 1] = static_cast<float>(i) * 0.001f + 0.0005f;
    }

    std::vector<float> fullDest(TOTAL_FRAMES * 2, 0.0f);

    size_t framesWritten = 0;
    size_t framesRead = 0;

    // Write in chunks of 37 frames, read in chunks of 29 frames across many wraparounds
    while (framesRead < TOTAL_FRAMES) {
        if (framesWritten < TOTAL_FRAMES) {
            size_t writeChunk = std::min<size_t>(37, TOTAL_FRAMES - framesWritten);
            size_t w = rb.write(fullSource.data() + (framesWritten * 2), writeChunk);
            framesWritten += w;
        }

        if (rb.availableToRead() > 0) {
            size_t readChunk = std::min<size_t>(29, rb.availableToRead());
            size_t r = rb.read(fullDest.data() + (framesRead * 2), readChunk);
            framesRead += r;
        }
    }

    TEST_ASSERT(framesWritten == TOTAL_FRAMES, "All 5000 frames written through gradient test");
    TEST_ASSERT(framesRead == TOTAL_FRAMES, "All 5000 frames read through gradient test");

    bool identical = true;
    for (size_t i = 0; i < fullSource.size(); ++i) {
        if (std::abs(fullSource[i] - fullDest[i]) > 1e-7f) {
            identical = false;
            break;
        }
    }
    TEST_ASSERT(identical, "Gradient data integrity verified across multiple wraparounds");
}

void testRingBufferConcurrencyStress() {
    std::cout << "[TEST] RingBuffer Multi-Threaded SPSC Concurrency Stress\n";

    syncwave::RingBuffer rb(512, 1);
    constexpr size_t TEST_FRAMES = 100000;

    std::atomic<bool> producerDone{false};
    std::vector<float> receivedFrames;
    receivedFrames.reserve(TEST_FRAMES);

    // Producer thread
    std::thread producer([&]() {
        size_t written = 0;
        std::vector<float> chunk(64);
        while (written < TEST_FRAMES) {
            size_t count = std::min<size_t>(64, TEST_FRAMES - written);
            for (size_t i = 0; i < count; ++i) {
                chunk[i] = static_cast<float>(written + i);
            }

            size_t actual = rb.write(chunk.data(), count);
            written += actual;
            if (actual == 0) {
                std::this_thread::yield();
            }
        }
        producerDone.store(true, std::memory_order_release);
    });

    // Consumer thread
    std::thread consumer([&]() {
        std::vector<float> chunk(64);
        while (!producerDone.load(std::memory_order_acquire) || rb.availableToRead() > 0) {
            size_t avail = rb.availableToRead();
            if (avail > 0) {
                size_t toRead = std::min<size_t>(chunk.size(), avail);
                size_t actual = rb.read(chunk.data(), toRead);
                receivedFrames.insert(receivedFrames.end(), chunk.begin(), chunk.begin() + actual);
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    TEST_ASSERT(receivedFrames.size() == TEST_FRAMES, "Received exactly 100,000 frames without loss");

    bool strictlyOrdered = true;
    for (size_t i = 0; i < receivedFrames.size(); ++i) {
        if (std::abs(receivedFrames[i] - static_cast<float>(i)) > 1e-6f) {
            strictlyOrdered = false;
            break;
        }
    }
    TEST_ASSERT(strictlyOrdered, "All 100,000 frames received in strict FIFO order without corruption");
}

void testMasterAudioBusIntegration() {
    std::cout << "[TEST] MasterAudioBus Abstraction and Tone Generator Routing\n";

    auto canonFmt = syncwave::MasterAudioBus::canonicalFormat();
    TEST_ASSERT(canonFmt.sampleRate == 48000, "Canonical master sample rate is 48000 Hz");
    TEST_ASSERT(canonFmt.channels == 2, "Canonical master channels is 2");
    TEST_ASSERT(canonFmt.isFloat(), "Canonical master format is Float32");

    syncwave::MasterAudioBus bus(canonFmt, 4800); // 100ms capacity
    TEST_ASSERT(bus.capacityFrames() == 4800, "MasterAudioBus capacity is 4800 frames");

    syncwave::ToneGenerator toneGen(440.0, 0.25);
    std::vector<float> synthChunk(480 * 2);
    toneGen.generateFrames(reinterpret_cast<uint8_t*>(synthChunk.data()), 480, canonFmt);

    size_t written = bus.write(synthChunk.data(), 480);
    TEST_ASSERT(written == 480, "ToneGenerator wrote 480 frames to MasterAudioBus");
    TEST_ASSERT(bus.availableFrames() == 480, "MasterAudioBus availableFrames is 480");

    std::vector<float> destChunk(480 * 2, 0.0f);
    size_t readFrames = bus.read(destChunk.data(), 480);
    TEST_ASSERT(readFrames == 480, "MasterAudioBus read 480 frames");
    TEST_ASSERT(destChunk == synthChunk, "Audio samples read from bus match generated tone samples");
}

void testResampler() {
    std::cout << "[TEST] Resampler Calculations and DSP Interpolation\n";

    // 1. Passthrough test (48k -> 48k)
    syncwave::Resampler passResampler(48000, 48000, 2);
    TEST_ASSERT(std::abs(passResampler.ratio() - 1.0) < 1e-6, "Passthrough ratio is 1.0");

    std::vector<float> inPassthrough(100 * 2, 0.75f);
    std::vector<float> outPassthrough(100 * 2, 0.0f);
    size_t passProduced = passResampler.process(inPassthrough.data(), 100, outPassthrough.data(), 100);
    TEST_ASSERT(passProduced == 100, "Passthrough produces exact frame count (100)");
    TEST_ASSERT(outPassthrough == inPassthrough, "Passthrough output samples match input exactly");

    // 2. Downsampling test (48000 -> 44100)
    syncwave::Resampler downResampler(48000, 44100, 2);
    double expectedDownRatio = 48000.0 / 44100.0;
    TEST_ASSERT(std::abs(downResampler.ratio() - expectedDownRatio) < 1e-6, "Downsampling ratio matches 48000/44100");

    // 3. Upsampling test (44100 -> 48000)
    syncwave::Resampler upResampler(44100, 48000, 2);
    double expectedUpRatio = 44100.0 / 48000.0;
    TEST_ASSERT(std::abs(upResampler.ratio() - expectedUpRatio) < 1e-6, "Upsampling ratio matches 44100/48000");

    // 4. Linear interpolation accuracy on a known linear slope
    // x[t] = t * 0.01f
    syncwave::Resampler rampResampler(48000, 44100, 1);
    constexpr size_t RAMP_IN_FRAMES = 480;
    std::vector<float> rampIn(RAMP_IN_FRAMES);
    for (size_t i = 0; i < RAMP_IN_FRAMES; ++i) {
        rampIn[i] = static_cast<float>(i) * 0.01f;
    }
    std::vector<float> rampOut(500, 0.0f);
    size_t rampProduced = rampResampler.process(rampIn.data(), RAMP_IN_FRAMES, rampOut.data(), 500);
    TEST_ASSERT(rampProduced > 430 && rampProduced <= 442, "Downsampled 480 frames produce ~441 output frames");

    // Verify all points on interpolated ramp follow y = (m * ratio) * 0.01f
    bool rampAccurate = true;
    for (size_t i = 0; i < rampProduced; ++i) {
        float expectedVal = static_cast<float>(i * (48000.0 / 44100.0)) * 0.01f;
        if (std::abs(rampOut[i] - expectedVal) > 1e-4f) {
            rampAccurate = false;
            break;
        }
    }
    TEST_ASSERT(rampAccurate, "Linear interpolation reproduces exact linear continuous ramp");

    // 5. Multi-chunk boundary continuity
    syncwave::Resampler chunkResampler(48000, 44100, 1);
    std::vector<float> chunkOut1(250, 0.0f);
    std::vector<float> chunkOut2(250, 0.0f);
    size_t chunk1Produced = chunkResampler.process(rampIn.data(), 240, chunkOut1.data(), 250);
    size_t chunk2Produced = chunkResampler.process(rampIn.data() + 240, 240, chunkOut2.data(), 250);
    
    std::vector<float> combinedOut;
    combinedOut.insert(combinedOut.end(), chunkOut1.begin(), chunkOut1.begin() + chunk1Produced);
    combinedOut.insert(combinedOut.end(), chunkOut2.begin(), chunkOut2.begin() + chunk2Produced);

    TEST_ASSERT(combinedOut.size() == rampProduced, "Chunked processing produced identical total frame count");
    bool boundarySmooth = true;
    for (size_t i = 0; i < combinedOut.size(); ++i) {
        if (std::abs(combinedOut[i] - rampOut[i]) > 1e-5f) {
            boundarySmooth = false;
            break;
        }
    }
    TEST_ASSERT(boundarySmooth, "Chunk boundary interpolation perfectly continuous with zero phase jump");

    // 6. Resampler::pull from MasterAudioBus
    auto fmt48k = syncwave::AudioFormat{48000, 2, 32, 8, 32, syncwave::SampleType::Float32};
    syncwave::MasterAudioBus bus(fmt48k, 4800);
    syncwave::Resampler pullResampler(48000, 44100, 2);

    std::vector<float> busFeed(2000 * 2, 0.5f);
    bus.write(busFeed.data(), 2000);

    std::vector<float> pulled(441 * 2, 0.0f);
    size_t pullCount = pullResampler.pull(bus, pulled.data(), 441);
    TEST_ASSERT(pullCount == 441, "Resampler::pull returned exact requested 441 frames");
    TEST_ASSERT(std::abs(pulled[0] - 0.5f) < 1e-4f, "Pulled audio values match fed values");
}

void testWasapiCaptureIntegration() {
    std::cout << "[TEST] WasapiCapture Real-Time Loopback Integration\n";

    syncwave::WasapiCapture capture;
    TEST_ASSERT(capture.state() == syncwave::CaptureState::Uninitialized, "WasapiCapture initially Uninitialized");

    bool opened = capture.open("");
    TEST_ASSERT(opened, "WasapiCapture::open() succeeded on default render endpoint");
    TEST_ASSERT(capture.state() == syncwave::CaptureState::Opened, "WasapiCapture in Opened state");
    TEST_ASSERT(!capture.deviceId().empty(), "Captured device ID is non-empty");
    TEST_ASSERT(!capture.deviceName().empty(), "Captured device name is non-empty");
    std::cout << "    Capturing on default endpoint: " << capture.deviceName() << "\n";

    bool initialized = capture.initialize();
    TEST_ASSERT(initialized, "WasapiCapture::initialize() succeeded in shared loopback mode");
    TEST_ASSERT(capture.state() == syncwave::CaptureState::Initialized, "WasapiCapture in Initialized state");

    const auto capFmt = capture.format();
    TEST_ASSERT(capFmt.sampleRate >= 44100, "Capture sample rate is >= 44.1 kHz");
    TEST_ASSERT(capFmt.channels >= 1, "Capture channels >= 1");
    TEST_ASSERT(capture.bufferFrameCount() > 0, "Capture buffer frame count > 0");

    std::atomic<uint64_t> receivedFrames{0};
    bool started = capture.start([&](const float* /*data*/, uint32_t frames, const syncwave::AudioFormat& /*fmt*/) {
        receivedFrames.fetch_add(frames, std::memory_order_relaxed);
    });

    TEST_ASSERT(started, "WasapiCapture::start() succeeded");
    TEST_ASSERT(capture.state() == syncwave::CaptureState::Running, "WasapiCapture in Running state");

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    capture.stop();
    TEST_ASSERT(capture.state() == syncwave::CaptureState::Stopped, "WasapiCapture in Stopped state after stop()");

    capture.close();
    TEST_ASSERT(capture.state() == syncwave::CaptureState::Closed, "WasapiCapture in Closed state after close()");
}

void testAudioEngineCaptureIntegration() {
    std::cout << "[TEST] AudioEngine System Audio Loopback Pipeline\n";

    syncwave::AudioEngine engine;
    TEST_ASSERT(!engine.isRunning(), "AudioEngine initially not running");

    bool started = engine.startCapture("", "");
    TEST_ASSERT(started, "AudioEngine::startCapture() succeeded on default endpoints");
    TEST_ASSERT(engine.isRunning(), "AudioEngine isRunning() reports true during capture");

    auto diag = engine.getDiagnostics();
    TEST_ASSERT(diag.isCaptureMode, "Diagnostics report isCaptureMode == true");
    TEST_ASSERT(diag.busCapacityFrames > 0, "MasterAudioBus capacity configured");

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    engine.stop();
    TEST_ASSERT(!engine.isRunning(), "AudioEngine stopped cleanly");

    auto finalDiag = engine.getDiagnostics();
    TEST_ASSERT(!finalDiag.isRunning, "Final diagnostics report isRunning == false");
}

void testDeviceOutputModel() {
    std::cout << "[TEST] DeviceOutput Queue, Resampler & Telemetry\n";

    syncwave::AudioDevice dev{"mock-dev-1", "Mock Device 1", syncwave::DeviceState::Active, true, false};
    syncwave::DeviceOutput out(dev);

    TEST_ASSERT(out.deviceId() == "mock-dev-1", "DeviceOutput preserves device ID");
    TEST_ASSERT(out.deviceName() == "Mock Device 1", "DeviceOutput preserves device Name");
    TEST_ASSERT(out.isAvailable(), "DeviceOutput initially available");
    TEST_ASSERT(out.queue() == nullptr, "Queue initially unallocated before initialize");

    bool inited = out.initializeForTesting(48000, 2, 48000);
    TEST_ASSERT(inited, "DeviceOutput initializeForTesting succeeds");
    TEST_ASSERT(out.queue() != nullptr, "Queue allocated after initialization");
    TEST_ASSERT(out.queue()->capacityFrames() == 48000, "Queue capacity matches 1.0 second master frame rate");

    std::vector<float> sampleData(500 * 2, 0.75f);
    size_t pushed = out.push(sampleData.data(), 500);
    TEST_ASSERT(pushed == 500, "DeviceOutput::push pushes 500 stereo frames");

    auto telem = out.getTelemetry();
    TEST_ASSERT(telem.framesRouted == 500, "Telemetry records 500 frames routed");
    TEST_ASSERT(telem.queueAvailable == 500, "Telemetry records 500 frames available in queue");

    std::vector<float> readBuf(500 * 2, 0.0f);
    size_t readFrames = out.queue()->read(readBuf.data(), 500);
    TEST_ASSERT(readFrames == 500, "Read 500 frames from output queue");
    TEST_ASSERT(readBuf[0] == 0.75f, "Read sample matches pushed value");

    out.markUnavailable();
    TEST_ASSERT(!out.isAvailable(), "DeviceOutput marked unavailable");
    size_t droppedPush = out.push(sampleData.data(), 100);
    TEST_ASSERT(droppedPush == 0, "Push returns 0 when device is unavailable");
}

void testOutputRouterLifecycle() {
    std::cout << "[TEST] OutputRouter Lifecycle & Endpoint Management\n";

    syncwave::OutputRouter router;
    TEST_ASSERT(router.outputCount() == 0, "OutputRouter begins with 0 outputs");

    syncwave::AudioDevice dev1{"dev-1", "Speakers 1", syncwave::DeviceState::Active, true, false};
    syncwave::AudioDevice dev2{"dev-2", "Headphones 2", syncwave::DeviceState::Active, false, false};
    syncwave::AudioDevice dev3{"dev-3", "Bluetooth 3", syncwave::DeviceState::Active, false, false};

    TEST_ASSERT(router.addOutput(dev1), "addOutput(dev1) succeeds");
    TEST_ASSERT(router.addOutput(dev2), "addOutput(dev2) succeeds");
    TEST_ASSERT(router.addOutput(dev3), "addOutput(dev3) succeeds");
    TEST_ASSERT(router.outputCount() == 3, "OutputRouter count is 3");

    // Cannot add duplicate
    TEST_ASSERT(!router.addOutput(dev1), "addOutput rejected duplicate device ID");
    TEST_ASSERT(router.outputCount() == 3, "OutputRouter count remains 3 after duplicate rejection");

    // Lookup
    TEST_ASSERT(router.getOutput(0) != nullptr, "getOutput(0) returns valid pointer");
    TEST_ASSERT(router.getOutput(0)->deviceId() == "dev-1", "getOutput(0) matches dev-1");
    TEST_ASSERT(router.getOutput("dev-2") != nullptr, "getOutput('dev-2') finds device");
    TEST_ASSERT(router.getOutput("dev-2")->deviceName() == "Headphones 2", "dev-2 name matches");
    TEST_ASSERT(router.getOutput("unknown") == nullptr, "getOutput for unknown ID returns nullptr");

    // Remove
    TEST_ASSERT(router.removeOutput("dev-2"), "removeOutput('dev-2') succeeds");
    TEST_ASSERT(router.outputCount() == 2, "OutputRouter count decreases to 2");
    TEST_ASSERT(router.getOutput("dev-2") == nullptr, "dev-2 no longer found after removal");

    // Clear
    router.clearOutputs();
    TEST_ASSERT(router.outputCount() == 0, "clearOutputs() clears all configured outputs");
}

void testOutputRouterFanOut() {
    std::cout << "[TEST] OutputRouter Identical Multi-Output Fan-Out\n";

    syncwave::OutputRouter router;
    syncwave::AudioDevice dev1{"out-1", "Endpoint 1", syncwave::DeviceState::Active, true, false};
    syncwave::AudioDevice dev2{"out-2", "Endpoint 2", syncwave::DeviceState::Active, false, false};
    syncwave::AudioDevice dev3{"out-3", "Endpoint 3", syncwave::DeviceState::Active, false, false};

    router.addOutput(dev1);
    router.addOutput(dev2);
    router.addOutput(dev3);

    bool inited = router.initializeOutputsForTesting(48000, 2);
    TEST_ASSERT(inited, "initializeOutputsForTesting succeeded for 3 outputs");

    // Generate unique test pattern: stereo ramp
    const size_t TEST_FRAMES = 1024;
    std::vector<float> testPattern(TEST_FRAMES * 2);
    for (size_t i = 0; i < TEST_FRAMES; ++i) {
        testPattern[i * 2 + 0] = static_cast<float>(i) * 0.001f;
        testPattern[i * 2 + 1] = -static_cast<float>(i) * 0.001f;
    }

    router.route(testPattern.data(), TEST_FRAMES);

    TEST_ASSERT(router.totalFramesDistributed() == TEST_FRAMES, "Router totalFramesDistributed matches pushed frames");

    auto telems = router.getOutputTelemetry();
    TEST_ASSERT(telems.size() == 3, "Telemetry returned for all 3 outputs");
    for (size_t o = 0; o < telems.size(); ++o) {
        TEST_ASSERT(telems[o].framesRouted == TEST_FRAMES, "Each output received exact routed frame count");
        TEST_ASSERT(telems[o].queueAvailable == TEST_FRAMES, "Each output queue holds exact routed frame count");
    }

    // Verify all 3 queues contain identical data bit-for-bit
    for (size_t o = 0; o < 3; ++o) {
        auto* devOut = router.getOutput(o);
        std::vector<float> readBuffer(TEST_FRAMES * 2, 0.0f);
        size_t read = devOut->queue()->read(readBuffer.data(), TEST_FRAMES);
        TEST_ASSERT(read == TEST_FRAMES, "Read exact frames from output queue");
        TEST_ASSERT(readBuffer == testPattern, "Output queue contains bit-identical copy of routed audio frames");
    }

    // Test dispatch from MasterAudioBus to OutputRouter
    syncwave::AudioFormat busFmt{48000, 2, 32, 8, 32, syncwave::SampleType::Float32};
    syncwave::MasterAudioBus bus(busFmt, 4800);
    bus.write(testPattern.data(), 512);

    size_t dispatched = router.dispatch(bus, 512);
    TEST_ASSERT(dispatched == 512, "router.dispatch() pulled 512 frames from MasterAudioBus");
    for (size_t o = 0; o < 3; ++o) {
        auto* devOut = router.getOutput(o);
        TEST_ASSERT(devOut->queue()->availableToRead() == 512, "Each output queue received 512 dispatched frames from bus");
    }
}

void testOutputRouterIndependentQueues() {
    std::cout << "[TEST] OutputRouter Independent Queue Isolation\n";

    syncwave::OutputRouter router;
    syncwave::AudioDevice devFast{"dev-fast", "Fast Consumer", syncwave::DeviceState::Active, true, false};
    syncwave::AudioDevice devSlow{"dev-slow", "Slow Consumer", syncwave::DeviceState::Active, false, false};

    router.addOutput(devFast);
    router.addOutput(devSlow);
    router.initializeOutputsForTesting(48000, 2);

    std::vector<float> chunk1(1000 * 2, 0.1f);
    router.route(chunk1.data(), 1000);

    // Fast consumer consumes all 1000 frames
    std::vector<float> fastBuf(1000 * 2);
    size_t fastRead = router.getOutput("dev-fast")->queue()->read(fastBuf.data(), 1000);
    TEST_ASSERT(fastRead == 1000, "Fast consumer read 1000 frames");
    TEST_ASSERT(router.getOutput("dev-fast")->queue()->availableToRead() == 0, "Fast consumer queue empty");

    // Slow consumer reads nothing
    TEST_ASSERT(router.getOutput("dev-slow")->queue()->availableToRead() == 1000, "Slow consumer queue retains all 1000 frames");

    // Route another chunk
    std::vector<float> chunk2(500 * 2, 0.2f);
    router.route(chunk2.data(), 500);

    TEST_ASSERT(router.getOutput("dev-fast")->queue()->availableToRead() == 500, "Fast consumer queue has only new 500 frames");
    TEST_ASSERT(router.getOutput("dev-slow")->queue()->availableToRead() == 1500, "Slow consumer queue has accumulated 1500 frames");
    TEST_ASSERT(router.getOutput("dev-fast")->getTelemetry().queueOverruns == 0, "Fast consumer 0 overruns");
    TEST_ASSERT(router.getOutput("dev-slow")->getTelemetry().queueOverruns == 0, "Slow consumer 0 overruns");
}

void testOutputRouterQueueOverflow() {
    std::cout << "[TEST] OutputRouter Queue Overflow Non-Destructive Handling\n";

    syncwave::OutputRouter router;
    syncwave::AudioDevice devA{"dev-a", "Active Consumer", syncwave::DeviceState::Active, true, false};
    syncwave::AudioDevice devStalled{"dev-stalled", "Stalled Consumer", syncwave::DeviceState::Active, false, false};

    router.addOutput(devA);
    router.addOutput(devStalled);
    router.initializeOutputsForTesting(48000, 2); // capacity = 48000 frames each

    // Fill queues to full capacity (48000 frames)
    std::vector<float> bulkAudio(4000 * 2, 0.3f);
    for (int i = 0; i < 12; ++i) {
        router.route(bulkAudio.data(), 4000);
    }

    TEST_ASSERT(router.getOutput("dev-stalled")->queue()->availableToRead() == 48000, "Stalled consumer queue completely filled");

    // devA consumes 2000 frames to make room
    std::vector<float> drain(2000 * 2);
    router.getOutput("dev-a")->queue()->read(drain.data(), 2000);
    TEST_ASSERT(router.getOutput("dev-a")->queue()->availableToRead() == 46000, "Dev A has 2000 frames of headroom");

    // Route 1000 more frames
    std::vector<float> extra(1000 * 2, 0.9f);
    router.route(extra.data(), 1000);

    // devA absorbed all 1000 frames
    TEST_ASSERT(router.getOutput("dev-a")->queue()->availableToRead() == 47000, "Dev A successfully absorbed extra frames");
    TEST_ASSERT(router.getOutput("dev-a")->getTelemetry().queueOverruns == 0, "Dev A suffered 0 queue overruns");

    // devStalled dropped 1000 frames due to overflow
    TEST_ASSERT(router.getOutput("dev-stalled")->queue()->availableToRead() == 48000, "Stalled queue remained at capacity limit");
    TEST_ASSERT(router.getOutput("dev-stalled")->getTelemetry().queueOverruns == 1000, "Stalled queue recorded exact 1000 overrun frames");
}

void testOutputRouterMultiSampleRateResampling() {
    std::cout << "[TEST] OutputRouter Multi-Sample-Rate Resampling Isolation\n";

    syncwave::OutputRouter router;
    syncwave::AudioDevice dev48k{"dev-48k", "Speakers 48kHz", syncwave::DeviceState::Active, true, false};
    syncwave::AudioDevice dev44k{"dev-44k", "Buds 44.1kHz", syncwave::DeviceState::Active, false, false};

    router.addOutput(dev48k);
    router.addOutput(dev44k);

    // dev48k at 48000, dev44k at 44100
    router.initializeOutputsForTesting(48000, 2, {48000, 44100});

    // 4800 frames of 48 kHz stereo audio (= 0.1s)
    std::vector<float> audio48k(4800 * 2, 0.4f);
    router.route(audio48k.data(), 4800);

    // dev48k queue read (1:1 rate)
    std::vector<float> out48k(4800 * 2, 0.0f);
    size_t dev48Read = router.getOutput("dev-48k")->queue()->read(out48k.data(), 4800);
    TEST_ASSERT(dev48Read == 4800, "48 kHz output read all 4800 frames directly");

    // dev44k resampler pull (4800 frames downsampled to 44.1k = 4410 frames)
    std::vector<float> out44k(4410 * 2, 0.0f);
    auto* dev44Output = router.getOutput("dev-44k");
    size_t dev44Produced = dev44Output->resampler()->pull(*dev44Output->queue(), out44k.data(), 4410);
    TEST_ASSERT(dev44Produced == 4410, "Resampler pull produced exact 4410 frames for 44.1 kHz output");
    TEST_ASSERT(std::abs(out44k[0] - 0.4f) < 1e-4f, "Resampled 44.1 kHz audio preserved sample magnitude");
}

void testOutputRouterDisconnectHandling() {
    std::cout << "[TEST] OutputRouter Hot-Unplug / Disconnect Handling\n";

    syncwave::OutputRouter router;
    syncwave::AudioDevice dev1{"usb-dac", "USB DAC", syncwave::DeviceState::Active, true, false};
    syncwave::AudioDevice dev2{"realtek", "Realtek Speakers", syncwave::DeviceState::Active, false, false};

    router.addOutput(dev1);
    router.addOutput(dev2);
    router.initializeOutputsForTesting(48000, 2);

    TEST_ASSERT(router.getOutput("usb-dac")->isAvailable(), "USB DAC initially available");
    TEST_ASSERT(router.getOutput("realtek")->isAvailable(), "Realtek initially available");

    // Disconnect USB DAC
    router.onDeviceDisconnected("usb-dac");
    TEST_ASSERT(!router.getOutput("usb-dac")->isAvailable(), "USB DAC marked unavailable after disconnect");
    TEST_ASSERT(router.getOutput("realtek")->isAvailable(), "Realtek remains active and available");

    // Route frames
    std::vector<float> testChunk(300 * 2, 0.5f);
    router.route(testChunk.data(), 300);

    TEST_ASSERT(router.getOutput("usb-dac")->getTelemetry().framesRouted == 0, "Disconnected device received 0 frames");
    TEST_ASSERT(router.getOutput("realtek")->getTelemetry().framesRouted == 300, "Active device received all 300 routed frames");
}

void testOutputRouterCleanShutdown() {
    std::cout << "[TEST] OutputRouter Clean Shutdown With Buffered Frames\n";

    syncwave::OutputRouter router;
    syncwave::AudioDevice dev1{"dev-1", "Device 1", syncwave::DeviceState::Active, true, false};
    syncwave::AudioDevice dev2{"dev-2", "Device 2", syncwave::DeviceState::Active, false, false};

    router.addOutput(dev1);
    router.addOutput(dev2);
    router.initializeOutputsForTesting(48000, 2);

    std::vector<float> chunk(2500 * 2, 0.2f);
    router.route(chunk.data(), 2500);

    TEST_ASSERT(router.getOutput("dev-1")->queue()->availableToRead() == 2500, "Queue has 2500 buffered frames");

    // Close outputs while data is in queues
    router.closeOutputs();
    TEST_ASSERT(router.getOutput("dev-1")->queue()->availableToRead() == 0, "dev-1 queue reset on close");
    TEST_ASSERT(router.getOutput("dev-2")->queue()->availableToRead() == 0, "dev-2 queue reset on close");
}

void testAudioEngineMultiOutputIntegration() {
    std::cout << "[TEST] AudioEngine Multi-Output Integration\n";

    syncwave::DeviceManager mgr;
    auto activeDevices = mgr.enumerateDevices(true);
    if (activeDevices.size() >= 2) {
        std::cout << "    Testing with 2 physical endpoints: " 
                  << activeDevices[0].name << " and " << activeDevices[1].name << "\n";
        
        syncwave::AudioEngine engine;
        std::vector<syncwave::AudioDevice> targets = {activeDevices[0], activeDevices[1]};
        
        syncwave::ToneParameters params;
        params.frequencyHz = 440.0;
        params.volume = 0.2;
        params.durationSec = 0.5;

        bool started = engine.startTone(targets, params);
        TEST_ASSERT(started, "AudioEngine::startTone() with 2 endpoints succeeded");
        TEST_ASSERT(engine.isRunning(), "AudioEngine reports isRunning == true with 2 endpoints");

        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        auto diag = engine.getDiagnostics();
        TEST_ASSERT(diag.outputs.size() == 2, "Diagnostics report 2 active output endpoints");
        TEST_ASSERT(diag.outputs[0].framesRouted > 0 || diag.outputs[1].framesRouted > 0, "Frames routed to outputs");

        engine.stop();
        TEST_ASSERT(!engine.isRunning(), "AudioEngine stopped cleanly from multi-output tone");
    } else {
        std::cout << "    Skipping physical multi-output integration test (requires >= 2 active audio endpoints, found " 
                  << activeDevices.size() << ")\n";
    }
}

void testDeviceClockBasics() {
    std::cout << "[TEST] DeviceClock Basics & Unit Conversions\n";

    syncwave::DeviceClock clock("dev-1", 48000, 100);
    TEST_ASSERT(clock.deviceId() == "dev-1", "DeviceClock retains deviceId");
    TEST_ASSERT(clock.nominalSampleRate() == 48000, "DeviceClock nominal rate is 48000");
    TEST_ASSERT(clock.sampleCount() == 0, "DeviceClock initial sample count is 0");
    TEST_ASSERT(!clock.latestSample().has_value(), "DeviceClock initial latestSample is empty");

    auto est = clock.estimateRate();
    TEST_ASSERT(!est.isValid, "Initial clock rate estimate is invalid (no samples)");

    // Test DeviceClockSample conversions
    syncwave::DeviceClockSample sample;
    sample.timestamp = std::chrono::steady_clock::now();
    sample.clockPosition = 48000 * 2; // 2 seconds
    sample.clockFrequency = 48000;
    sample.sampleRate = 48000;
    sample.currentPadding = 480;      // 10ms at 48kHz
    sample.streamLatencyHns = 100000; // 10ms = 100,000 * 100ns
    sample.isValid = true;

    TEST_ASSERT(std::abs(sample.streamLatencyMs() - 10.0) < 0.001, "streamLatencyMs converts hns to ms correctly");
    TEST_ASSERT(std::abs(sample.streamLatencySec() - 0.01) < 0.0001, "streamLatencySec converts hns to sec correctly");
    TEST_ASSERT(std::abs(sample.paddingMs() - 10.0) < 0.001, "paddingMs converts frames to ms correctly");
    TEST_ASSERT(std::abs(sample.positionSeconds() - 2.0) < 0.001, "positionSeconds converts ticks to seconds");
    TEST_ASSERT(sample.positionFrames() == 96000, "positionFrames converts ticks to frames");

    // Test with non-sample-rate clock frequency (e.g. 384000 Hz)
    syncwave::DeviceClockSample sample384k;
    sample384k.clockPosition = 768000; // 2 seconds
    sample384k.clockFrequency = 384000;
    sample384k.sampleRate = 48000;
    sample384k.isValid = true;
    TEST_ASSERT(std::abs(sample384k.positionSeconds() - 2.0) < 0.001, "positionSeconds handles 384kHz clock frequency");
    TEST_ASSERT(sample384k.positionFrames() == 96000, "positionFrames converts 384kHz clock to 48kHz frames");
}

void testDeviceClockLinearRegression() {
    std::cout << "[TEST] DeviceClock Linear Regression Rate Estimation\n";

    // Scenario 1: Exact 48000 Hz clock
    syncwave::DeviceClock clock("dev-exact", 48000, 100);
    auto t0 = std::chrono::steady_clock::now();

    for (int i = 0; i < 30; ++i) {
        syncwave::DeviceClockSample s;
        // Step of 100ms
        s.timestamp = t0 + std::chrono::milliseconds(i * 100);
        // Position advances exactly 4800 ticks per 100ms
        s.clockPosition = static_cast<uint64_t>(i * 4800);
        s.clockFrequency = 48000;
        s.sampleRate = 48000;
        s.isValid = true;
        clock.recordSample(s);
    }

    auto est = clock.estimateRate();
    TEST_ASSERT(est.isValid, "Clock rate estimate is valid with 30 samples");
    TEST_ASSERT(est.sampleCount == 30, "Estimate sample count is 30");
    TEST_ASSERT(std::abs(est.estimatedRate - 48000.0) < 0.5, "Estimated rate is ~48000 Hz");
    TEST_ASSERT(std::abs(est.rateErrorPpm) < 10.0, "Rate error ppm is ~0 for ideal clock");
    TEST_ASSERT(est.measurementDurationSec > 2.8, "Measurement duration is ~2.9s");
    TEST_ASSERT(est.rSquared > 0.9999, "Goodness-of-fit rSquared > 0.9999 for linear progression");

    // Scenario 2: Clock drifting fast (+50 ppm)
    // 48000 * (1 + 50e-6) = 48002.4 Hz
    // Ticks per 100ms = 4800.24
    syncwave::DeviceClock driftingClock("dev-drift", 48000, 100);
    for (int i = 0; i < 30; ++i) {
        syncwave::DeviceClockSample s;
        s.timestamp = t0 + std::chrono::milliseconds(i * 100);
        s.clockPosition = static_cast<uint64_t>(std::round(i * 4800.24));
        s.clockFrequency = 48000;
        s.sampleRate = 48000;
        s.isValid = true;
        driftingClock.recordSample(s);
    }

    auto estDrift = driftingClock.estimateRate();
    TEST_ASSERT(estDrift.isValid, "Drifting clock rate estimate is valid");
    TEST_ASSERT(std::abs(estDrift.estimatedRate - 48002.4) < 1.0, "Estimated rate is ~48002.4 Hz");
    TEST_ASSERT(std::abs(estDrift.rateErrorPpm - 50.0) < 2.0, "Rate error ppm is ~+50 ppm (+/- 2 ppm)");
}

void testDeviceClockInsufficientSamples() {
    std::cout << "[TEST] DeviceClock Insufficient & Edge-Case Samples\n";

    syncwave::DeviceClock clock("dev-edge", 48000, 100);
    TEST_ASSERT(!clock.estimateRate().isValid, "0 samples -> isValid == false");

    syncwave::DeviceClockSample s1;
    s1.timestamp = std::chrono::steady_clock::now();
    s1.clockPosition = 0;
    s1.clockFrequency = 48000;
    s1.isValid = true;
    clock.recordSample(s1);
    TEST_ASSERT(!clock.estimateRate().isValid, "1 sample -> isValid == false");

    // Second sample with same timestamp (dt == 0)
    syncwave::DeviceClockSample s2 = s1;
    s2.clockPosition = 480;
    clock.recordSample(s2);
    TEST_ASSERT(!clock.estimateRate().isValid, "2 samples with dt == 0 -> isValid == false");

    // Reset clears everything
    clock.reset();
    TEST_ASSERT(clock.sampleCount() == 0, "reset() clears sampleCount");
    TEST_ASSERT(!clock.estimateRate().isValid, "reset() invalidates rate estimate");
}

void testDriftEstimatorCalculations() {
    std::cout << "[TEST] DriftEstimator Mathematical Calculations\n";

    // calculatePpm:
    // (48002.4 / 48000.0 - 1.0) * 1e6 = 50.0
    double ppmFast = syncwave::DriftEstimator::calculatePpm(48002.4, 48000.0);
    TEST_ASSERT(std::abs(ppmFast - 50.0) < 0.01, "calculatePpm for +50 ppm clock is accurate");

    // (47997.6 / 48000.0 - 1.0) * 1e6 = -50.0
    double ppmSlow = syncwave::DriftEstimator::calculatePpm(47997.6, 48000.0);
    TEST_ASSERT(std::abs(ppmSlow - (-50.0)) < 0.01, "calculatePpm for -50 ppm clock is accurate");

    // Zero nominal handling
    TEST_ASSERT(syncwave::DriftEstimator::calculatePpm(48000.0, 0.0) == 0.0, "calculatePpm handles 0 nominal rate gracefully");

    // Relative ppm:
    // Clock A = 48000 / 48000 = 1.0
    // Clock B = 44102.205 / 44100 = 1.00005 (+50 ppm)
    // relative = (1.0 / 1.00005 - 1.0) * 1e6 =~ -49.9975 ppm
    double relPpm = syncwave::DriftEstimator::calculateRelativePpm(48000.0, 48000.0, 44102.205, 44100.0);
    TEST_ASSERT(std::abs(relPpm - (-49.9975)) < 0.01, "calculateRelativePpm across 48kHz and 44.1kHz is accurate");

    // Relative ppm with zero rates
    TEST_ASSERT(syncwave::DriftEstimator::calculateRelativePpm(48000.0, 48000.0, 0.0, 44100.0) == 0.0,
                "calculateRelativePpm handles 0 rate gracefully");
}

void testDriftEstimatorPairwise() {
    std::cout << "[TEST] DriftEstimator Pairwise Clock Evaluation\n";

    syncwave::DeviceClock clockA("dev-speaker", 48000, 100);
    syncwave::DeviceClock clockB("dev-buds", 44100, 100);

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 25; ++i) {
        auto t = t0 + std::chrono::milliseconds(i * 100);
        // Clock A: 48000 Hz nominal, exact
        syncwave::DeviceClockSample sa;
        sa.timestamp = t;
        sa.clockPosition = static_cast<uint64_t>(i * 4800);
        sa.clockFrequency = 48000;
        sa.sampleRate = 48000;
        sa.isValid = true;
        clockA.recordSample(sa);

        // Clock B: 44100 Hz nominal, +50 ppm faster: 44102.205 Hz
        syncwave::DeviceClockSample sb;
        sb.timestamp = t;
        sb.clockPosition = static_cast<uint64_t>(std::round(i * 4410.2205));
        sb.clockFrequency = 44100;
        sb.sampleRate = 44100;
        sb.isValid = true;
        clockB.recordSample(sb);
    }

    auto pair = syncwave::DriftEstimator::estimate(clockA, clockB, "Realtek", "realme Buds");
    TEST_ASSERT(pair.isValid, "PairwiseDriftEstimate is valid");
    TEST_ASSERT(pair.deviceNameA == "Realtek", "Pairwise estimate retains deviceNameA");
    TEST_ASSERT(pair.deviceNameB == "realme Buds", "Pairwise estimate retains deviceNameB");
    TEST_ASSERT(std::abs(pair.estimatedRateA - 48000.0) < 1.0, "Estimated rate A is ~48000 Hz");
    TEST_ASSERT(std::abs(pair.estimatedRateB - 44102.2) < 1.0, "Estimated rate B is ~44102.2 Hz");
    TEST_ASSERT(pair.relativeDriftPpm < -45.0 && pair.relativeDriftPpm > -55.0, "Relative drift is ~-50 ppm");
    TEST_ASSERT(pair.sampleCountA == 25 && pair.sampleCountB == 25, "Sample counts are 25");
}

void testDriftEstimatorRobustness() {
    std::cout << "[TEST] DriftEstimator Robustness & Disconnection Handling\n";

    syncwave::DeviceClock validClock("dev-valid", 48000, 100);
    syncwave::DeviceClock emptyClock("dev-empty", 48000, 100);

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; ++i) {
        syncwave::DeviceClockSample s;
        s.timestamp = t0 + std::chrono::milliseconds(i * 100);
        s.clockPosition = static_cast<uint64_t>(i * 4800);
        s.clockFrequency = 48000;
        s.sampleRate = 48000;
        s.isValid = true;
        validClock.recordSample(s);
    }

    auto pair1 = syncwave::DriftEstimator::estimate(validClock, emptyClock);
    TEST_ASSERT(!pair1.isValid, "Drift estimate with one empty clock is invalid");

    auto pair2 = syncwave::DriftEstimator::estimate(emptyClock, emptyClock);
    TEST_ASSERT(!pair2.isValid, "Drift estimate with two empty clocks is invalid");
}

void testWasapiClockSnapshotIntegration() {
    std::cout << "[TEST] WasapiClockSnapshot Live Endpoint Query\n";
    syncwave::ComInitializer com;
    syncwave::DeviceManager mgr;
    auto defaultDev = mgr.getDefaultDevice();
    if (!defaultDev) {
        std::cout << "    Skipping live snapshot test (no active default audio device found)\n";
        return;
    }

    syncwave::WasapiOutput output;
    TEST_ASSERT(output.open(defaultDev->id), "Opened default device for snapshot test");
    TEST_ASSERT(output.initialize(), "Initialized default device for snapshot test");

    auto snap = output.getClockSnapshot();
    TEST_ASSERT(snap.isValid, "WasapiClockSnapshot is valid on initialized device");
    TEST_ASSERT(snap.frequency > 0, "WasapiClockSnapshot frequency > 0");
    TEST_ASSERT(snap.sampleRate > 0, "WasapiClockSnapshot sampleRate > 0");
    TEST_ASSERT(snap.bufferFrameCount > 0, "WasapiClockSnapshot bufferFrameCount > 0");
    TEST_ASSERT(snap.streamLatencyHns >= 0, "WasapiClockSnapshot streamLatencyHns >= 0");

    output.close();
}

void testOutputRouterClockTelemetry() {
    std::cout << "[TEST] OutputRouter Clock Sampling and Telemetry\n";

    syncwave::OutputRouter router;
    syncwave::AudioDevice dev1{"dev-1", "Endpoint 1", syncwave::DeviceState::Active, true, false};
    syncwave::AudioDevice dev2{"dev-2", "Endpoint 2", syncwave::DeviceState::Active, false, false};

    router.addOutput(dev1);
    router.addOutput(dev2);

    bool inited = router.initializeOutputsForTesting(48000, 2);
    TEST_ASSERT(inited, "initializeOutputsForTesting succeeded for 2 outputs");

    // Initially sampleAllClocks() works cleanly even if outputs have no live WASAPI client
    router.sampleAllClocks();

    auto telemsInitial = router.getOutputTelemetry();
    TEST_ASSERT(telemsInitial.size() == 2, "Output telemetry contains 2 devices");
    TEST_ASSERT(telemsInitial[0].clockSampleCount == 0, "No snapshots recorded yet without live WASAPI client");

    // Feed synthetic snapshots into the embedded DeviceClock objects
    auto* out1 = router.getOutput(0);
    auto* out2 = router.getOutput(1);
    TEST_ASSERT(out1 != nullptr && out2 != nullptr, "Retrieved outputs from router");

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; ++i) {
        auto t = t0 + std::chrono::milliseconds(i * 100);

        syncwave::WasapiClockSnapshot s1;
        s1.isValid = true;
        s1.position = i * 4800;
        s1.frequency = 48000;
        s1.sampleRate = 48000;
        s1.currentPadding = 480;
        s1.streamLatencyHns = 100000; // 10ms
        s1.bufferFrameCount = 960;
        out1->clock().recordSnapshot(s1, t);

        syncwave::WasapiClockSnapshot s2;
        s2.isValid = true;
        s2.position = static_cast<uint64_t>(std::round(i * 4410.22));
        s2.frequency = 44100;
        s2.sampleRate = 44100;
        s2.currentPadding = 441;
        s2.streamLatencyHns = 200000; // 20ms
        s2.bufferFrameCount = 882;
        out2->clock().recordSnapshot(s2, t);
    }

    auto telems = router.getOutputTelemetry();
    TEST_ASSERT(telems.size() == 2, "Router returns telemetry for both endpoints");
    TEST_ASSERT(telems[0].clockSampleCount == 20, "Output 1 has 20 clock samples");
    TEST_ASSERT(telems[1].clockSampleCount == 20, "Output 2 has 20 clock samples");
    TEST_ASSERT(std::abs(telems[0].streamLatencyMs - 10.0) < 0.1, "Output 1 stream latency is 10.0ms");
    TEST_ASSERT(std::abs(telems[1].streamLatencyMs - 20.0) < 0.1, "Output 2 stream latency is 20.0ms");
    TEST_ASSERT(std::abs(telems[0].estimatedClockRateHz - 48000.0) < 1.0, "Output 1 estimated rate is ~48000 Hz");
    TEST_ASSERT(std::abs(telems[1].estimatedClockRateHz - 44102.2) < 1.0, "Output 2 estimated rate is ~44102.2 Hz");

    auto pairs = router.getPairwiseDriftEstimates();
    TEST_ASSERT(pairs.size() == 1, "Exactly 1 pairwise drift estimate for 2 outputs");
    TEST_ASSERT(pairs[0].isValid, "Pairwise estimate is valid");
    TEST_ASSERT(pairs[0].sampleCountA == 20 && pairs[0].sampleCountB == 20, "Pairwise estimate uses 20 samples per device");

    router.closeOutputs();
}

void testDeviceClockUnitsAndConversions() {
    std::cout << "[TEST] DeviceClock Units, Conversions, and Edge Cases\n";

    // QPC conversions
    TEST_ASSERT(std::abs(syncwave::DeviceClockSample::qpcToSeconds(10000000ULL, 10000000ULL) - 1.0) < 1e-9, 
                "qpcToSeconds(10M, 10M) == 1.0s");
    TEST_ASSERT(std::abs(syncwave::DeviceClockSample::qpcToSeconds(5000000ULL, 10000000ULL) - 0.5) < 1e-9, 
                "qpcToSeconds(5M, 10M) == 0.5s");
    TEST_ASSERT(syncwave::DeviceClockSample::qpcToSeconds(10000000ULL, 0) == 0.0, 
                "qpcToSeconds with 0 QPF returns 0.0 (safe guard)");

    // WASAPI clock ticks to seconds
    TEST_ASSERT(std::abs(syncwave::DeviceClockSample::ticksToSeconds(384000ULL, 384000ULL) - 1.0) < 1e-9, 
                "ticksToSeconds(384k, 384k) == 1.0s");
    TEST_ASSERT(std::abs(syncwave::DeviceClockSample::ticksToSeconds(192000ULL, 384000ULL) - 0.5) < 1e-9, 
                "ticksToSeconds(192k, 384k) == 0.5s");
    TEST_ASSERT(syncwave::DeviceClockSample::ticksToSeconds(100ULL, 0) == 0.0, 
                "ticksToSeconds with 0 frequency returns 0.0");

    // WASAPI clock ticks to audio frames (e.g. 384 kHz clock with 48 kHz frames = 8 ticks/frame)
    TEST_ASSERT(syncwave::DeviceClockSample::ticksToFrames(384000ULL, 384000ULL, 48000) == 48000ULL, 
                "ticksToFrames(384k, 384k, 48k) == 48,000 frames");
    TEST_ASSERT(syncwave::DeviceClockSample::ticksToFrames(768000ULL, 384000ULL, 48000) == 96000ULL, 
                "ticksToFrames(768k, 384k, 48k) == 96,000 frames");
    TEST_ASSERT(syncwave::DeviceClockSample::ticksToFrames(8ULL, 384000ULL, 48000) == 1ULL, 
                "8 ticks at 384 kHz == exactly 1 frame at 48 kHz");
    TEST_ASSERT(syncwave::DeviceClockSample::ticksToFrames(100ULL, 0, 48000) == 0ULL, 
                "ticksToFrames with 0 frequency returns 0");
    TEST_ASSERT(syncwave::DeviceClockSample::ticksToFrames(100ULL, 384000ULL, 0) == 0ULL, 
                "ticksToFrames with 0 sample rate returns 0");

    // System QPF query
    uint64_t qpf = syncwave::DeviceClock::getSystemQpcFrequency();
    TEST_ASSERT(qpf > 0, "System QPF is greater than zero");
    TEST_ASSERT(qpf >= 1000000ULL, "System QPF is at least 1 MHz (high resolution)");
}

void testDeviceClockMultiWindowEstimation() {
    std::cout << "[TEST] DeviceClock Multi-Window Rate Estimation\n";

    syncwave::DeviceClock clock("dev-multi-win", 48000, 1024);
    auto t0 = std::chrono::steady_clock::now();

    // Generate 60 seconds of data at 10 Hz (600 samples)
    // Device clock runs at 48000 * 1.000025 = 48001.2 Hz (+25 ppm)
    // Clock frequency is 384000 Hz (8 ticks per frame)
    const uint64_t clockFreq = 384000;
    const double framesPerSec = 48001.2;
    const double ticksPerSec = framesPerSec * (static_cast<double>(clockFreq) / 48000.0);

    for (int i = 0; i <= 600; ++i) {
        auto t = t0 + std::chrono::milliseconds(i * 100);
        syncwave::DeviceClockSample s;
        s.timestamp = t;
        s.clockPosition = static_cast<uint64_t>(std::round(i * 0.1 * ticksPerSec));
        s.clockFrequency = clockFreq;
        s.sampleRate = 48000;
        s.isValid = true;
        clock.recordSample(s);
    }

    TEST_ASSERT(clock.sampleCount() == 601, "Recorded 601 samples (60s @ 10 Hz)");

    // Window = 5 seconds
    auto est5 = clock.estimateRateOverWindow(5.0);
    TEST_ASSERT(est5.isValid, "5s window rate estimate is valid");
    TEST_ASSERT(est5.sampleCount > 40 && est5.sampleCount <= 60, "5s window uses ~50 samples");
    TEST_ASSERT(std::abs(est5.estimatedRate - framesPerSec) < 0.2, "5s estimated rate matches target (+25 ppm)");
    TEST_ASSERT(est5.rSquared > 0.999, "5s R^2 > 0.999");

    // Window = 30 seconds
    auto est30 = clock.estimateRateOverWindow(30.0);
    TEST_ASSERT(est30.isValid, "30s window rate estimate is valid");
    TEST_ASSERT(est30.sampleCount > 280 && est30.sampleCount <= 310, "30s window uses ~300 samples");
    TEST_ASSERT(std::abs(est30.estimatedRate - framesPerSec) < 0.1, "30s estimated rate matches target");

    // Window = 60 seconds (full window)
    auto est60 = clock.estimateRateOverWindow(60.0);
    TEST_ASSERT(est60.isValid, "60s window rate estimate is valid");
    TEST_ASSERT(est60.sampleCount >= 600, "60s window uses all ~601 samples");
    TEST_ASSERT(std::abs(est60.estimatedRate - framesPerSec) < 0.05, "60s estimated rate matches target");

    // Window exceeding available range (fallback to all samples)
    auto est120 = clock.estimateRateOverWindow(120.0);
    TEST_ASSERT(est120.isValid, "Window exceeding duration succeeds with fallback");
    TEST_ASSERT(est120.sampleCount == 601, "Exceeding window used all 601 samples");

    // Window = 0.0 (all samples)
    auto estAll = clock.estimateRateOverWindow(0.0);
    TEST_ASSERT(estAll.isValid, "Window = 0.0 estimates over all samples");
    TEST_ASSERT(estAll.sampleCount == 601, "Window = 0.0 used all 601 samples");

    // Elapsed times
    TEST_ASSERT(std::abs(clock.totalElapsedQpcTimeSec() - 60.0) < 0.1, "Total elapsed QPC time ~60.0s");
    TEST_ASSERT(std::abs(clock.totalElapsedDeviceTimeSec() - 60.0) < 0.1, "Total elapsed Device time ~60.0s");
}

void testDriftAndOffsetSeparation() {
    std::cout << "[TEST] Mathematical Separation of Drift from Offset\n";

    // 1. Instantaneous offset calculation
    double offset1 = syncwave::DriftEstimator::calculateOffset(10.050, 10.000);
    TEST_ASSERT(std::abs(offset1 - 0.050) < 1e-9, "calculateOffset(10.050, 10.000) == +0.050s (+50.0 ms)");

    double offset2 = syncwave::DriftEstimator::calculateOffset(10.000, 10.050);
    TEST_ASSERT(std::abs(offset2 - (-0.050)) < 1e-9, "calculateOffset(10.000, 10.050) == -0.050s (-50.0 ms)");

    // 2. Accumulated drift calculation: Delta Offset = Offset(t1) - Offset(t0)
    // If device A started 100 ms ahead and stayed 100 ms ahead:
    double driftConstant = syncwave::DriftEstimator::calculateAccumulatedDrift(0.100, 0.100);
    TEST_ASSERT(std::abs(driftConstant - 0.0) < 1e-9, 
                "Constant offset (100ms -> 100ms) produces EXACTLY 0.0 ms accumulated drift");

    // If device A started 100 ms ahead and expanded to 105 ms ahead:
    double driftExpanding = syncwave::DriftEstimator::calculateAccumulatedDrift(0.100, 0.105);
    TEST_ASSERT(std::abs(driftExpanding - 0.005) < 1e-9, 
                "Offset expanding from 100ms to 105ms produces +5.0 ms (+0.005s) accumulated drift");

    // If device A started 100 ms ahead and contracted to 95 ms ahead:
    double driftContracting = syncwave::DriftEstimator::calculateAccumulatedDrift(0.100, 0.095);
    TEST_ASSERT(std::abs(driftContracting - (-0.005)) < 1e-9, 
                "Offset contracting from 100ms to 95ms produces -5.0 ms (-0.005s) accumulated drift");

    // 3. Drift rate calculation from accumulated drift (5.0 ms drift over 10.0 s = 0.005s / 10s * 1e6 = 500 ppm)
    double ratePpm = syncwave::DriftEstimator::calculateDriftRateFromDelta(0.005, 10.0);
    TEST_ASSERT(std::abs(ratePpm - 500.0) < 1e-9, 
                "5.0 ms (0.005s) drift over 10.0 s == +500.0 ppm");

    double rateZero = syncwave::DriftEstimator::calculateDriftRateFromDelta(0.0, 10.0);
    TEST_ASSERT(std::abs(rateZero - 0.0) < 1e-9, 
                "0.0 ms drift over 10.0 s == 0.0 ppm");

    double rateInvalidDur = syncwave::DriftEstimator::calculateDriftRateFromDelta(0.005, 0.0);
    TEST_ASSERT(rateInvalidDur == 0.0, 
                "calculateDriftRateFromDelta with 0 duration returns 0.0 ppm safely");
}

void testDriftEstimatorWindowedAndConfidence() {
    std::cout << "[TEST] DriftEstimator Windowed Estimation & Confidence Metric\n";

    syncwave::DeviceClock clockA("dev-A", 48000, 200);
    syncwave::DeviceClock clockB("dev-B", 48000, 200);
    auto t0 = std::chrono::steady_clock::now();

    // Clock A: 48000 Hz exact
    // Clock B: 48000.72 Hz (+15 ppm)
    // Also, clock B starts with an intentional 80 ms offset
    for (int i = 0; i <= 50; ++i) {
        auto t = t0 + std::chrono::milliseconds(i * 100);

        syncwave::DeviceClockSample sa;
        sa.timestamp = t;
        sa.clockPosition = i * 4800ULL;
        sa.clockFrequency = 48000;
        sa.sampleRate = 48000;
        sa.isValid = true;
        clockA.recordSample(sa);

        syncwave::DeviceClockSample sb;
        sb.timestamp = t;
        // B has initial 80 ms offset (= 3840 frames) plus +15 ppm
        double bFrames = 3840.0 + (i * 0.1 * 48000.72);
        sb.clockPosition = static_cast<uint64_t>(std::round(bFrames));
        sb.clockFrequency = 48000;
        sb.sampleRate = 48000;
        sb.isValid = true;
        clockB.recordSample(sb);
    }

    // Estimate over full 5s
    auto estFull = syncwave::DriftEstimator::estimate(clockA, clockB);
    TEST_ASSERT(estFull.isValid, "Full drift estimate is valid");
    TEST_ASSERT(estFull.confidence > 0.99, "Drift confidence metric (rA^2 * rB^2) > 0.99");
    TEST_ASSERT(std::abs(estFull.driftRatePpm - (-15.0)) < 1.0, 
                "Drift ppm reflects A relative to B (~ -15 ppm)");
    TEST_ASSERT(std::abs((estFull.initialOffsetSec * 1000.0) - (-80.0)) < 1.0, 
                "Initial offset captures ~ -80 ms");

    // Estimate over 2.0s window
    auto estWin = syncwave::DriftEstimator::estimateOverWindow(clockA, clockB, 2.0);
    TEST_ASSERT(estWin.isValid, "Windowed drift estimate is valid");
    TEST_ASSERT(estWin.windowRequestedSec == 2.0, "Window duration recorded as 2.0s");
    TEST_ASSERT(estWin.confidence > 0.99, "Windowed confidence metric > 0.99");
    TEST_ASSERT(estWin.sampleCountA > 15 && estWin.sampleCountA <= 25, 
                "Windowed estimate used ~20 samples");
}

void testDeviceOutputPlayheadEstimation() {
    std::cout << "[TEST] DeviceOutput Playhead Estimation & Timeline Tracking\n";

    syncwave::AudioDevice dev{"dev-test", "Playhead Test Device", syncwave::DeviceState::Active, true, false};
    syncwave::DeviceOutput output(dev);

    // Initial state before playback
    TEST_ASSERT(output.estimatedAppPlayheadFrames() == 0, "Initial app playhead frames == 0");
    TEST_ASSERT(output.estimatedAppPlayheadSeconds() == 0.0, "Initial app playhead seconds == 0.0");
    TEST_ASSERT(output.wasapiClockPlayheadSeconds() == 0.0, "Initial WASAPI clock playhead seconds == 0.0");
    TEST_ASSERT(output.playheadDiscrepancyMs() == 0.0, "Initial playhead discrepancy == 0.0 ms");

    // Simulate clock snapshot recorded via output.clock()
    // Native format: 48000 Hz, Clock freq: 384000 Hz (8 ticks per frame)
    syncwave::WasapiClockSnapshot snap;
    snap.isValid = true;
    snap.position = 768000ULL; // 768000 / 384000 = 2.0 seconds of audio played by hardware
    snap.frequency = 384000ULL;
    snap.sampleRate = 48000;
    snap.currentPadding = 480;  // 10 ms currently sitting in hardware buffer
    snap.streamLatencyHns = 100000; // 10 ms driver latency
    snap.bufferFrameCount = 960;

    output.clock().recordSnapshot(snap, std::chrono::steady_clock::now());

    // WASAPI clock playhead: position / frequency = 768000 / 384000 = 2.0 seconds
    TEST_ASSERT(std::abs(output.wasapiClockPlayheadSeconds() - 2.0) < 1e-9, 
                "wasapiClockPlayheadSeconds() == 2.0s");

    auto telem = output.getTelemetry();
    TEST_ASSERT(std::abs(telem.wasapiClockPlayheadSec - 2.0) < 1e-9, 
                "Telemetry wasapiClockPlayheadSec matches 2.0s");
    TEST_ASSERT(telem.currentPadding == 480, 
                "Telemetry reports currentPadding == 480");
}

void testSyncPulseGenerator() {
    std::cout << "[TEST] SyncPulseGenerator Deterministic Pulse Generation\n";

    syncwave::PulseParameters params;
    params.leadInFrames = 24000;       // 500 ms at 48 kHz
    params.pulseDurationFrames = 48;   // 1 ms at 48 kHz
    params.leadOutFrames = 24000;      // 500 ms at 48 kHz
    params.peakAmplitude = 1.0f;

    syncwave::SyncPulseGenerator gen(params);

    TEST_ASSERT(gen.pulseMasterFrameIndex() == 24000ULL, 
                "Pulse impulse index is exactly frame 24,000 (after 500 ms lead-in)");
    TEST_ASSERT(gen.totalFrames() == 48048ULL, 
                "Total frames is 48,048 (24000 + 48 + 24000)");
    TEST_ASSERT(!gen.isComplete(), "Generator is initially not complete");
    TEST_ASSERT(gen.framesGenerated() == 0, "Current frame starts at 0");

    // Read 24,000 frames (all lead-in silence)
    std::vector<float> silenceBuf(24000 * 2, 999.0f);
    size_t readLeadIn = gen.generateFrames(silenceBuf.data(), 24000, 2);
    TEST_ASSERT(readLeadIn == 24000, "Generated 24,000 lead-in frames");
    TEST_ASSERT(gen.framesGenerated() == 24000, "Current frame is now 24,000");

    bool allSilent = true;
    for (float val : silenceBuf) {
        if (val != 0.0f) {
            allSilent = false;
            break;
        }
    }
    TEST_ASSERT(allSilent, "All 24,000 lead-in frames are strictly zero (silence)");

    // Read 48 frames (the pulse impulse)
    std::vector<float> pulseBuf(48 * 2, 0.0f);
    size_t readPulse = gen.generateFrames(pulseBuf.data(), 48, 2);
    TEST_ASSERT(readPulse == 48, "Generated 48 pulse impulse frames");

    float maxVal = 0.0f;
    bool stereoMatched = true;
    for (size_t f = 0; f < 48; ++f) {
        float left = pulseBuf[f * 2];
        float right = pulseBuf[f * 2 + 1];
        if (left != right) stereoMatched = false;
        if (left > maxVal) maxVal = left;
        TEST_ASSERT(left >= 0.0f, "Half-sine impulse is non-negative");
    }
    TEST_ASSERT(stereoMatched, "Left and right channels are identical");
    TEST_ASSERT(maxVal > 0.99f && maxVal <= 1.0f, "Peak impulse amplitude reaches ~1.0f");
    TEST_ASSERT(pulseBuf[0] < 0.15f, "Impulse begins near 0.0f at start");
    TEST_ASSERT(pulseBuf[47 * 2] < 0.15f, "Impulse returns near 0.0f at end");

    // Read lead-out silence
    std::vector<float> leadOutBuf(24000 * 2, 0.0f);
    size_t readLeadOut = gen.generateFrames(leadOutBuf.data(), 24000, 2);
    TEST_ASSERT(readLeadOut == 24000, "Generated 24,000 lead-out frames");
    TEST_ASSERT(gen.isComplete(), "Generator is marked complete after total frames");

    // Attempting further reads produces silence and returns 0 frames
    std::vector<float> extraBuf(100 * 2, 1.0f);
    size_t readExtra = gen.generateFrames(extraBuf.data(), 100, 2);
    TEST_ASSERT(readExtra == 0, "Generating beyond totalFrames returns 0");
    TEST_ASSERT(extraBuf[0] == 0.0f, "Buffer is cleared with silence");

    // Test reset
    gen.reset();
    TEST_ASSERT(!gen.isComplete(), "After reset, generator is not complete");
    TEST_ASSERT(gen.framesGenerated() == 0, "After reset, framesGenerated is 0");
}

void testOutputRouterMultiWindowPairwise() {
    std::cout << "[TEST] OutputRouter Multi-Window Pairwise Drift Queries\n";

    syncwave::OutputRouter router;
    syncwave::AudioDevice dev1{"dev-1", "Endpoint 1", syncwave::DeviceState::Active, true, false};
    syncwave::AudioDevice dev2{"dev-2", "Endpoint 2", syncwave::DeviceState::Active, false, false};

    router.addOutput(dev1);
    router.addOutput(dev2);
    TEST_ASSERT(router.initializeOutputsForTesting(48000, 2), "Initialized outputs for testing");

    auto* out1 = router.getOutput(0);
    auto* out2 = router.getOutput(1);
    TEST_ASSERT(out1 != nullptr && out2 != nullptr, "Retrieved outputs");

    // Populate 60 samples (6 seconds @ 10 Hz)
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i <= 60; ++i) {
        auto t = t0 + std::chrono::milliseconds(i * 100);

        syncwave::WasapiClockSnapshot s1;
        s1.isValid = true;
        s1.position = i * 4800ULL;
        s1.frequency = 48000;
        s1.sampleRate = 48000;
        s1.currentPadding = 480;
        s1.streamLatencyHns = 100000;
        s1.bufferFrameCount = 960;
        out1->clock().recordSnapshot(s1, t);

        syncwave::WasapiClockSnapshot s2;
        s2.isValid = true;
        s2.position = static_cast<uint64_t>(std::round(i * 4800.5));
        s2.frequency = 48000;
        s2.sampleRate = 48000;
        s2.currentPadding = 480;
        s2.streamLatencyHns = 100000;
        s2.bufferFrameCount = 960;
        out2->clock().recordSnapshot(s2, t);
    }

    auto pairsWin2 = router.getPairwiseDriftEstimatesOverWindow(2.0);
    TEST_ASSERT(pairsWin2.size() == 1, "Exactly 1 pairwise estimate for 2 outputs");
    TEST_ASSERT(pairsWin2[0].isValid, "Windowed pairwise estimate is valid");
    TEST_ASSERT(pairsWin2[0].windowRequestedSec == 2.0, "Pairwise estimate windowRequestedSec is 2.0");
    TEST_ASSERT(pairsWin2[0].sampleCountA <= 25, "Windowed pairwise used <= 25 samples");

    auto pairsWin5 = router.getPairwiseDriftEstimatesOverWindow(5.0);
    TEST_ASSERT(pairsWin5.size() == 1, "Windowed pairwise estimate for 5.0s exists");
    TEST_ASSERT(pairsWin5[0].sampleCountA > pairsWin2[0].sampleCountA, 
                "5s window used more samples than 2s window");

    router.closeOutputs();
}

void testDelayBufferBasics() {
    std::cout << "[TEST] DelayBuffer Basic Delay & Passthrough\n";

    syncwave::DelayBuffer db(48000);
    TEST_ASSERT(db.delayFrames() == 0, "Initial delay is 0 frames");
    TEST_ASSERT(db.delayMs(48000) == 0.0, "Initial delay is 0.0 ms");
    TEST_ASSERT(db.maxCapacityFrames() >= 48000, "Max delay frames >= 48000");

    // Zero-delay passthrough
    std::vector<float> input = { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f };
    std::vector<float> output(6, 0.0f);
    db.process(input.data(), output.data(), 3);
    TEST_ASSERT(output[0] == 0.1f && output[1] == 0.2f, "Zero delay frame 0 identical");
    TEST_ASSERT(output[2] == 0.3f && output[3] == 0.4f, "Zero delay frame 1 identical");
    TEST_ASSERT(output[4] == 0.5f && output[5] == 0.6f, "Zero delay frame 2 identical");

    // In-place zero-delay passthrough
    db.process(input.data(), input.data(), 3);
    TEST_ASSERT(input[0] == 0.1f && input[1] == 0.2f, "In-place zero delay passthrough works");

    // 1-frame delay
    db.reset();
    db.setDelayFrames(1);
    TEST_ASSERT(db.delayFrames() == 1, "Delay is 1 frame");
    std::vector<float> in1 = { 1.0f, 1.1f, 2.0f, 2.1f, 3.0f, 3.1f };
    std::vector<float> out1(6, -99.0f);
    db.process(in1.data(), out1.data(), 3);
    TEST_ASSERT(out1[0] == 0.0f && out1[1] == 0.0f, "Frame 0 is prefilled silence");
    TEST_ASSERT(out1[2] == 1.0f && out1[3] == 1.1f, "Frame 1 outputs in1[0]");
    TEST_ASSERT(out1[4] == 2.0f && out1[5] == 2.1f, "Frame 2 outputs in1[1]");

    // Read 1 more frame
    std::vector<float> in2 = { 4.0f, 4.1f };
    std::vector<float> out2(2, -99.0f);
    db.process(in2.data(), out2.data(), 1);
    TEST_ASSERT(out2[0] == 3.0f && out2[1] == 3.1f, "Next frame outputs in1[2]");
}

void testDelayBufferIntegrityAndWraparound() {
    std::cout << "[TEST] DelayBuffer Data Integrity & Circular Wraparound\n";

    // Setup 100-frame delay at 48 kHz
    syncwave::DelayBuffer db(10000); // 10,000 frames capacity
    db.setDelayFrames(100);
    TEST_ASSERT(db.delayFrames() == 100, "Delay set to 100 frames");

    const size_t totalFrames = 50000;
    std::vector<float> source(totalFrames * 2);
    for (size_t i = 0; i < totalFrames; ++i) {
        source[i * 2] = static_cast<float>(i + 1);
        source[i * 2 + 1] = -static_cast<float>(i + 1);
    }

    std::vector<float> received(totalFrames * 2, 0.0f);
    const size_t chunkSize = 128;
    for (size_t offset = 0; offset < totalFrames; offset += chunkSize) {
        size_t count = std::min(chunkSize, totalFrames - offset);
        db.process(source.data() + offset * 2, received.data() + offset * 2, count);
    }

    // Verify first 100 frames are silence
    bool leadInSilent = true;
    for (size_t i = 0; i < 100; ++i) {
        if (received[i * 2] != 0.0f || received[i * 2 + 1] != 0.0f) {
            leadInSilent = false;
            break;
        }
    }
    TEST_ASSERT(leadInSilent, "First 100 frames are strictly initial silence");

    // Verify frames 100..49999 match source 0..49899
    bool streamMatched = true;
    for (size_t i = 100; i < totalFrames; ++i) {
        size_t srcIdx = i - 100;
        float expectedL = static_cast<float>(srcIdx + 1);
        float expectedR = -static_cast<float>(srcIdx + 1);
        if (received[i * 2] != expectedL || received[i * 2 + 1] != expectedR) {
            streamMatched = false;
            break;
        }
    }
    TEST_ASSERT(streamMatched, "50,000 frames through circular delay matched with zero corruption or loss");

    // Dynamic delay adjustment from ms
    db.setDelayMs(10.0, 48000); // 10ms at 48kHz = 480 frames
    TEST_ASSERT(db.delayFrames() == 480, "10.0 ms converts to 480 frames at 48 kHz");
    TEST_ASSERT(std::abs(db.delayMs(48000) - 10.0) < 0.001, "delayMs(48000) returns 10.0 ms");

    // Reset clears state
    db.reset();
    TEST_ASSERT(db.delayFrames() == 480, "Reset retains configured delay size");
    std::vector<float> testIn(480 * 2, 5.0f);
    std::vector<float> testOut(480 * 2, -1.0f);
    db.process(testIn.data(), testOut.data(), 480);
    bool resetSilent = true;
    for (size_t i = 0; i < 480 * 2; ++i) {
        if (testOut[i] != 0.0f) { resetSilent = false; break; }
    }
    TEST_ASSERT(resetSilent, "Buffer prefills with silence after reset");
}

void testOutputLatencyModel() {
    std::cout << "[TEST] OutputLatencyModel Arithmetic & State Mapping\n";

    syncwave::OutputLatencyModel model;
    model.deviceId = "dev-test";
    model.deviceName = "Test Endpoint";
    model.sampleRate = 48000;
    model.wasapiStreamLatencyMs = 10.0;
    model.wasapiPaddingMs = 5.0;
    model.queueLatencyMs = 2.0;
    model.resamplerLatencyMs = 0.5;
    model.updateTotals();

    TEST_ASSERT(std::abs(model.estimatedSoftwareLatencyMs - 17.5) < 0.001, 
                "estimatedSoftwareLatencyMs is exactly sum of components (17.5 ms)");
    TEST_ASSERT(std::abs(model.effectiveLatencyMs - 17.5) < 0.001, 
                "effectiveLatencyMs equals software latency when calibration offset is 0");

    // Apply manual calibration offset (Model B: end-to-end arrival offset)
    model.optionalCalibrationOffsetMs = 50.0;
    model.recalculate();
    TEST_ASSERT(std::abs(model.effectiveLatencyMs - 50.0) < 0.001, 
                "effectiveLatencyMs equals calibrated arrival offset under Model B without double-counting (50.0 ms)");

    // Test sync state strings
    TEST_ASSERT(syncwave::syncStateToString(syncwave::SyncState::Disabled) == "Disabled", "SyncState::Disabled string");
    TEST_ASSERT(syncwave::syncStateToString(syncwave::SyncState::Manual) == "Manual", "SyncState::Manual string");
    TEST_ASSERT(syncwave::syncStateToString(syncwave::SyncState::SoftwareCalibrated) == "SoftwareCalibrated", "SyncState::SoftwareCalibrated string");
    TEST_ASSERT(syncwave::syncStateToString(syncwave::SyncState::PhysicallyCalibrated) == "PhysicallyCalibrated", "SyncState::PhysicallyCalibrated string");
    TEST_ASSERT(syncwave::syncStateToString(syncwave::SyncState::Uncertain) == "Uncertain", "SyncState::Uncertain string");
}

void testSyncControllerAlignmentMath() {
    std::cout << "[TEST] SyncController Latency Alignment Mathematics\n";

    // 2 devices: Dev0 = 20.0 ms, Dev1 = 60.0 ms
    syncwave::OutputLatencyModel m0;
    m0.deviceName = "Fast Dev";
    m0.sampleRate = 48000;
    m0.wasapiStreamLatencyMs = 20.0;
    m0.updateTotals();

    syncwave::OutputLatencyModel m1;
    m1.deviceName = "Slow Dev";
    m1.sampleRate = 44100;
    m1.wasapiStreamLatencyMs = 60.0;
    m1.updateTotals();

    std::vector<syncwave::OutputLatencyModel> models = { m0, m1 };

    double targetLatency = syncwave::SyncController::calculateTargetLatency(models);
    TEST_ASSERT(std::abs(targetLatency - 60.0) < 0.001, "Target latency is max latency (60.0 ms)");

    double d0 = syncwave::SyncController::calculateDeviceDelay(targetLatency, m0);
    double d1 = syncwave::SyncController::calculateDeviceDelay(targetLatency, m1);
    TEST_ASSERT(std::abs(d0 - 40.0) < 0.001, "Fast device delay is 40.0 ms (60 - 20)");
    TEST_ASSERT(std::abs(d1 - 0.0) < 0.001, "Slow device delay is 0.0 ms (slowest path = baseline)");

    // Compute plan
    auto plan = syncwave::SyncController::computeSoftwareAlignmentPlan(models);
    TEST_ASSERT(plan.isValid, "Software alignment plan is valid");
    TEST_ASSERT(plan.syncState == syncwave::SyncState::SoftwareCalibrated, "Plan state is SoftwareCalibrated");
    TEST_ASSERT(plan.calculatedDelaysMs.size() == 2, "2 delay values in plan");
    TEST_ASSERT(std::abs(plan.calculatedDelaysMs[0] - 40.0) < 0.001, "Dev0 delay is 40.0 ms");
    TEST_ASSERT(std::abs(plan.calculatedDelaysMs[1] - 0.0) < 0.001, "Dev1 delay is 0.0 ms");

    // Frame conversions at different sample rates
    // Dev0 @ 48kHz: 40ms = 1920 frames
    // Dev1 @ 44.1kHz: 0ms = 0 frames
    TEST_ASSERT(plan.calculatedDelaysFrames[0] == 1920, "Dev0 40ms @ 48kHz is exactly 1920 frames");
    TEST_ASSERT(plan.calculatedDelaysFrames[1] == 0, "Dev1 0ms is 0 frames");

    // Physical calibration offset test under Model B:
    // Fast Dev has 100 ms calibrated acoustic arrival offset (no double-counting with software latency)
    models[0].optionalCalibrationOffsetMs = 100.0;
    models[0].updateTotals(); // effective = 100.0 ms under Model B
    auto physPlan = syncwave::SyncController::computeSoftwareAlignmentPlan(models);
    TEST_ASSERT(physPlan.syncState == syncwave::SyncState::PhysicallyCalibrated, "State is PhysicallyCalibrated when offsets present");
    TEST_ASSERT(std::abs(physPlan.targetLatencyMs - 100.0) < 0.001, "Target latency is 100.0 ms (max of 100.0 ms arrival and 60.0 ms SW)");
    TEST_ASSERT(std::abs(physPlan.calculatedDelaysMs[0] - 0.0) < 0.001, "Dev0 now gets 0 delay (slowest path)");
    TEST_ASSERT(std::abs(physPlan.calculatedDelaysMs[1] - 40.0) < 0.001, "Dev1 gets 40 ms delay (100 - 60)");
    // Dev1 @ 44.1kHz: 40ms = 1764 frames
    TEST_ASSERT(physPlan.calculatedDelaysFrames[1] == 1764, "Dev1 40ms @ 44.1kHz is 1764 frames");

    // Manual plan test with clamping
    std::vector<double> manualDelays = { -10.0, 25.5 };
    auto manPlan = syncwave::SyncController::computeManualPlan(models, manualDelays);
    TEST_ASSERT(manPlan.isValid, "Manual plan is valid");
    TEST_ASSERT(manPlan.syncState == syncwave::SyncState::Manual, "Plan state is Manual");
    TEST_ASSERT(manPlan.calculatedDelaysMs[0] == 0.0, "Negative manual delay clamped to 0.0 ms");
    TEST_ASSERT(std::abs(manPlan.calculatedDelaysMs[1] - 25.5) < 0.001, "Positive manual delay retained");

    // 3 devices alignment
    syncwave::OutputLatencyModel m2;
    m2.deviceName = "Mid Dev";
    m2.sampleRate = 48000;
    m2.wasapiStreamLatencyMs = 35.0;
    m2.updateTotals();
    std::vector<syncwave::OutputLatencyModel> triModels = { m0, m1, m2 };
    // Clear offsets for pure SW test
    triModels[0].optionalCalibrationOffsetMs = 0.0;
    triModels[0].updateTotals();
    auto triPlan = syncwave::SyncController::computeSoftwareAlignmentPlan(triModels);
    TEST_ASSERT(triPlan.calculatedDelaysMs.size() == 3, "3 devices in tri-plan");
    TEST_ASSERT(std::abs(triPlan.calculatedDelaysMs[0] - 40.0) < 0.001, "Dev0 gets 40ms");
    TEST_ASSERT(std::abs(triPlan.calculatedDelaysMs[1] - 0.0) < 0.001, "Dev1 gets 0ms (slowest)");
    TEST_ASSERT(std::abs(triPlan.calculatedDelaysMs[2] - 25.0) < 0.001, "Dev2 gets 25ms (60 - 35)");
}

void testDeviceOutputDelayBufferIntegration() {
    std::cout << "[TEST] DeviceOutput DelayBuffer Integration & Telemetry\n";

    syncwave::AudioDevice dev{"dev-test", "Test Device", syncwave::DeviceState::Active, true, false};
    syncwave::DeviceOutput out(dev);

    TEST_ASSERT(out.initializeForTesting(48000, 2, 48000), "Initialized DeviceOutput for testing");
    TEST_ASSERT(out.configuredDelayMs() == 0.0, "Initial delay ms is 0.0");
    TEST_ASSERT(out.configuredDelayFrames() == 0, "Initial delay frames is 0");
    TEST_ASSERT(out.syncState() == syncwave::SyncState::Disabled, "Initial sync state is Disabled");

    // Set delay ms
    out.setDelayMs(20.0);
    TEST_ASSERT(out.configuredDelayMs() == 20.0, "Configured delay ms is 20.0");
    TEST_ASSERT(out.configuredDelayFrames() == 960, "Configured delay frames @ 48kHz is 960");
    TEST_ASSERT(out.syncState() == syncwave::SyncState::Manual, "Sync state changed to Manual");

    // Set calibration offset
    out.setCalibrationOffsetMs(15.0);
    TEST_ASSERT(out.calibrationOffsetMs() == 15.0, "Calibration offset is 15.0 ms");

    // Latency model query
    auto model = out.getLatencyModel();
    TEST_ASSERT(model.configuredDelayMs == 20.0, "Latency model reflects configuredDelayMs");
    TEST_ASSERT(model.appliedDelayFrames == 960, "Latency model reflects appliedDelayFrames");
    TEST_ASSERT(model.optionalCalibrationOffsetMs == 15.0, "Latency model reflects calibration offset");
    TEST_ASSERT(model.effectiveLatencyMs >= 15.0, "Effective latency includes calibration offset");

    // Telemetry query
    auto tele = out.getTelemetry();
    TEST_ASSERT(tele.syncState == syncwave::SyncState::Manual, "Telemetry sync state matches");
    TEST_ASSERT(tele.latencyModel.configuredDelayMs == 20.0, "Telemetry latency model configured delay matches");
}

void testOutputRouterSyncPlanIntegration() {
    std::cout << "[TEST] OutputRouter Delay & SyncPlan Integration\n";

    syncwave::OutputRouter router;
    syncwave::AudioDevice dev1{"dev-1", "Endpoint 1", syncwave::DeviceState::Active, true, false};
    syncwave::AudioDevice dev2{"dev-2", "Endpoint 2", syncwave::DeviceState::Active, false, false};

    router.addOutput(dev1);
    router.addOutput(dev2);
    TEST_ASSERT(router.initializeOutputsForTesting(48000, 2), "Initialized outputs for testing");

    auto models = router.getLatencyModels();
    TEST_ASSERT(models.size() == 2, "Router returned 2 latency models");

    // Test individual router delay setters
    router.setDeviceDelayMs(0, 30.0);
    router.setDeviceDelayFrames(1, 1440); // 30ms @ 48kHz
    auto* out0 = router.getOutput(0);
    auto* out1 = router.getOutput(1);
    TEST_ASSERT(out0->configuredDelayMs() == 30.0, "Output 0 delay set to 30.0 ms");
    TEST_ASSERT(out1->configuredDelayFrames() == 1440, "Output 1 delay set to 1440 frames");

    // Test applying an alignment plan
    syncwave::SyncPlan plan;
    plan.targetLatencyMs = 50.0;
    plan.calculatedDelaysMs = { 20.0, 10.0 };
    plan.calculatedDelaysFrames = { 960, 480 };
    plan.syncState = syncwave::SyncState::SoftwareCalibrated;
    plan.isValid = true;

    router.applySyncPlan(plan);
    TEST_ASSERT(out0->configuredDelayMs() == 20.0, "Output 0 received plan delay 20.0 ms");
    TEST_ASSERT(out1->configuredDelayMs() == 10.0, "Output 1 received plan delay 10.0 ms");
    TEST_ASSERT(out0->syncState() == syncwave::SyncState::SoftwareCalibrated, "Output 0 sync state updated to SoftwareCalibrated");
    TEST_ASSERT(out1->syncState() == syncwave::SyncState::SoftwareCalibrated, "Output 1 sync state updated to SoftwareCalibrated");

    router.closeOutputs();
}

// Milestone 10 Tests

void testResamplerRateAdjustmentAndSlewing() {
    std::cout << "[TEST] Resampler Rate Adjustment & Slewing\n";

    syncwave::Resampler resampler(48000, 48000, 2);
    TEST_ASSERT(!resampler.hasRateAdjustment(), "Initially no rate adjustment");
    TEST_ASSERT(resampler.targetRateAdjustmentPpm() == 0.0, "Target PPM initially 0.0");
    TEST_ASSERT(resampler.currentRateAdjustmentPpm() == 0.0, "Current PPM initially 0.0");

    // Immediate rate adjustment
    resampler.setRateAdjustmentPpm(100.0, true);
    TEST_ASSERT(resampler.hasRateAdjustment(), "hasRateAdjustment is true after +100 PPM");
    TEST_ASSERT(resampler.targetRateAdjustmentPpm() == 100.0, "Target PPM is 100.0");
    TEST_ASSERT(resampler.currentRateAdjustmentPpm() == 100.0, "Current PPM is immediately 100.0");
    double expectedRatio = 1.0 * (1.0 + 100.0 / 1e6);
    TEST_ASSERT(std::abs(resampler.ratio() - expectedRatio) < 1e-9, "Effective ratio includes +100 PPM");

    // Negative immediate rate adjustment
    resampler.setRateAdjustmentPpm(-50.0, true);
    TEST_ASSERT(resampler.currentRateAdjustmentPpm() == -50.0, "Current PPM is immediately -50.0");
    expectedRatio = 1.0 * (1.0 - 50.0 / 1e6);
    TEST_ASSERT(std::abs(resampler.ratio() - expectedRatio) < 1e-9, "Effective ratio includes -50 PPM");

    // Slew rate limiting test
    resampler.setRateAdjustmentPpm(0.0, true);
    resampler.setSlewRatePpmPerSecond(5.0); // 5 ppm/s slew limit
    TEST_ASSERT(resampler.slewRatePpmPerSecond() == 5.0, "Slew rate is 5.0 ppm/s");

    // Target +20 PPM without immediate
    resampler.setRateAdjustmentPpm(20.0, false);
    TEST_ASSERT(resampler.targetRateAdjustmentPpm() == 20.0, "Target PPM is 20.0");
    TEST_ASSERT(resampler.currentRateAdjustmentPpm() == 0.0, "Current PPM is still 0.0 before processing");

    // Process 1.0 second of audio (48000 frames)
    std::vector<float> inAudio(48000 * 2, 0.5f);
    std::vector<float> outAudio(48000 * 2, 0.0f);
    size_t produced = resampler.process(inAudio.data(), 48000, outAudio.data(), 48000);
    TEST_ASSERT(produced > 0, "Processed 1 second block");
    TEST_ASSERT(std::abs(resampler.currentRateAdjustmentPpm() - 5.0) < 0.1, "After 1 second, current PPM slewed to ~5.0 PPM");

    // Process another 1.0 second block (total 2.0s): should advance to ~10.0 PPM
    resampler.process(inAudio.data(), 48000, outAudio.data(), 48000);
    TEST_ASSERT(std::abs(resampler.currentRateAdjustmentPpm() - 10.0) < 0.1, "After 2 seconds, current PPM slewed to ~10.0 PPM");

    // Process 3 more seconds (total 5.0s, need 10 more ppm): should reach 20.0 target PPM and hold
    for (int i = 0; i < 3; ++i) {
        resampler.process(inAudio.data(), 48000, outAudio.data(), 48000);
    }
    TEST_ASSERT(std::abs(resampler.currentRateAdjustmentPpm() - 20.0) < 0.01, "After 5 seconds, current PPM reached target 20.0 PPM exactly");
}

void testFilteredDriftEstimatorOutlierRejection() {
    std::cout << "[TEST] FilteredDriftEstimator Outlier Rejection & Filtering\n";

    syncwave::FilteredDriftEstimator estimator(5.0, 30, 500.0, 0.15, 0.20);
    syncwave::DeviceClock clock("dev-test", 48000, 200);

    auto t0 = std::chrono::steady_clock::now();

    // 1. Check before minimum observation duration: should be invalid / not enough samples
    for (int i = 0; i < 15; ++i) {
        syncwave::DeviceClockSample s;
        s.timestamp = t0 + std::chrono::milliseconds(i * 100);
        s.clockPosition = i * 4800; // 48000 Hz
        s.clockFrequency = 48000;
        s.sampleRate = 48000;
        s.isValid = true;
        clock.recordSample(s);
    }

    auto res1 = estimator.update(clock, 1.5, 1.5, 10.0);
    TEST_ASSERT(!res1.isValid, "Estimator invalid before 30 samples / 5.0s");

    // 2. Add samples up to 55 samples (5.5s)
    for (int i = 15; i < 55; ++i) {
        syncwave::DeviceClockSample s;
        s.timestamp = t0 + std::chrono::milliseconds(i * 100);
        s.clockPosition = i * 4800;
        s.clockFrequency = 48000;
        s.sampleRate = 48000;
        s.isValid = true;
        clock.recordSample(s);
    }

    auto res2 = estimator.update(clock, 5.0, 5.0, 10.0);
    TEST_ASSERT(res2.isValid, "Estimator valid after 54 samples and >5s observation");
    TEST_ASSERT(!res2.isOutlierRejected, "Clean samples not rejected as outlier");
    TEST_ASSERT(std::abs(res2.filteredDriftPpm) < 2.0, "Filtered drift near 0 for nominal 48000 Hz clock");
    TEST_ASSERT(res2.confidence >= 0.90, "Confidence score >= 0.90");

    // 3. Inject a corrupted outlier sample (+5000 PPM jump)
    syncwave::DeviceClockSample spikeSample;
    spikeSample.timestamp = t0 + std::chrono::milliseconds(55 * 100);
    spikeSample.clockPosition = 55 * 4800 + 4800; // Massive jump
    spikeSample.clockFrequency = 48000;
    spikeSample.sampleRate = 48000;
    spikeSample.isValid = true;
    clock.recordSample(spikeSample);

    auto resSpike = estimator.update(clock, 5.5, 5.5, 10.0);
    TEST_ASSERT(resSpike.isOutlierRejected, "Massive clock jump successfully rejected as outlier");
    TEST_ASSERT(std::abs(resSpike.filteredDriftPpm) < 5.0, "Filtered drift not corrupted by spike");
}

void testDriftControllerDeadbandAndClamping() {
    std::cout << "[TEST] DriftController Deadband, Proportional Feedback, & Hard Clamping\n";

    syncwave::DriftControllerConfig config;
    config.deadbandMs = 1.0;
    config.maxAdjustmentPpm = 100.0;
    config.kp = 10.0; // 10 ppm per ms
    config.enableFeedforward = true;
    config.minObservationSec = 5.0;
    config.minSamples = 30;
    config.minConfidence = 0.90;

    syncwave::DriftController controller(config);
    controller.setEnabled(true);

    syncwave::SyncError error;
    error.isValid = true;
    error.confidence = 0.95;
    error.observationDurationSec = 6.0;

    // Case 1: Inside positive deadband (+0.5 ms)
    error.filteredPhaseErrorMs = 0.5;
    error.filteredDriftPpm = 0.0;
    auto out1 = controller.calculateCorrection(error);
    TEST_ASSERT(out1.state == syncwave::DriftCorrectionState::Locked, "Inside deadband (+0.5 ms) is Locked");
    TEST_ASSERT(out1.proportionalTermPpm == 0.0, "Proportional feedback is 0 inside deadband");
    TEST_ASSERT(out1.targetRateAdjustmentPpm == 0.0, "Target PPM is 0.0 inside deadband");

    // Case 2: Inside negative deadband (-0.8 ms)
    error.filteredPhaseErrorMs = -0.8;
    auto out2 = controller.calculateCorrection(error);
    TEST_ASSERT(out2.state == syncwave::DriftCorrectionState::Locked, "Inside deadband (-0.8 ms) is Locked");
    TEST_ASSERT(out2.proportionalTermPpm == 0.0, "Proportional feedback is 0 inside negative deadband");

    // Case 3: Output lagging behind target (+2.5 ms) -> excess error = +1.5 ms
    error.filteredPhaseErrorMs = 2.5;
    error.filteredDriftPpm = 0.0;
    auto out3 = controller.calculateCorrection(error);
    TEST_ASSERT(out3.state == syncwave::DriftCorrectionState::Correcting, "+2.5 ms error is Correcting");
    TEST_ASSERT(std::abs(out3.proportionalTermPpm - 15.0) < 1e-6, "Proportional feedback is +15.0 ppm for +2.5 ms error");
    TEST_ASSERT(std::abs(out3.targetRateAdjustmentPpm - 15.0) < 1e-6, "Target PPM is +15.0 ppm (speed up)");
    TEST_ASSERT(!out3.isClamped, "+15 ppm is within limits (not clamped)");

    // Case 4: Output leading ahead of target (-3.0 ms) -> excess error = -2.0 ms
    error.filteredPhaseErrorMs = -3.0;
    auto out4 = controller.calculateCorrection(error);
    TEST_ASSERT(out4.state == syncwave::DriftCorrectionState::Correcting, "-3.0 ms error is Correcting");
    TEST_ASSERT(std::abs(out4.proportionalTermPpm - (-20.0)) < 1e-6, "Proportional feedback is -20.0 ppm for -3.0 ms error");
    TEST_ASSERT(std::abs(out4.targetRateAdjustmentPpm - (-20.0)) < 1e-6, "Target PPM is -20.0 ppm (slow down)");

    // Case 5: Large error exceeding clamp (+25 ms error)
    error.filteredPhaseErrorMs = 25.0;
    auto out5 = controller.calculateCorrection(error);
    TEST_ASSERT(out5.isClamped, "Excessive PPM adjustment is clamped");
    TEST_ASSERT(out5.targetRateAdjustmentPpm == 100.0, "Target PPM hard clamped to +100.0 PPM");

    // Case 6: Large negative error exceeding clamp (-25 ms error)
    error.filteredPhaseErrorMs = -25.0;
    auto out6 = controller.calculateCorrection(error);
    TEST_ASSERT(out6.isClamped, "Excessive negative adjustment is clamped");
    TEST_ASSERT(out6.targetRateAdjustmentPpm == -100.0, "Target PPM hard clamped to -100.0 PPM");

    // Case 7: Feedforward cancellation: clock is slow by -25 ppm, phase error inside deadband
    error.filteredPhaseErrorMs = 0.2;
    error.filteredDriftPpm = -25.0;
    auto out7 = controller.calculateCorrection(error);
    TEST_ASSERT(out7.state == syncwave::DriftCorrectionState::Locked, "In deadband with drift is Locked");
    TEST_ASSERT(std::abs(out7.feedforwardTermPpm - 25.0) < 1e-6, "Feedforward term is +25.0 ppm to cancel -25 ppm drift");
    TEST_ASSERT(std::abs(out7.targetRateAdjustmentPpm - 25.0) < 1e-6, "Target PPM matches feedforward +25.0 ppm");
}

void testDriftControllerStateTransitions() {
    std::cout << "[TEST] DriftController State Machine Transitions\n";

    syncwave::DriftControllerConfig config;
    syncwave::DriftController controller(config);

    TEST_ASSERT(controller.state() == syncwave::DriftCorrectionState::Disabled, "Initial state is Disabled");

    controller.setEnabled(true);
    TEST_ASSERT(controller.state() == syncwave::DriftCorrectionState::Initializing, "Enabling enters Initializing state");

    syncwave::SyncError error;
    error.isValid = true;
    error.observationDurationSec = 2.0;
    error.confidence = 0.95;
    auto out = controller.calculateCorrection(error, true);
    TEST_ASSERT(out.state == syncwave::DriftCorrectionState::Measuring, "Short observation enters Measuring state");

    error.observationDurationSec = 6.0;
    error.filteredPhaseErrorMs = 0.4;
    out = controller.calculateCorrection(error, true);
    TEST_ASSERT(out.state == syncwave::DriftCorrectionState::Locked, "Inside deadband enters Locked state");

    error.filteredPhaseErrorMs = 2.5;
    out = controller.calculateCorrection(error, true);
    TEST_ASSERT(out.state == syncwave::DriftCorrectionState::Correcting, "Outside deadband enters Correcting state");

    error.confidence = 0.70;
    out = controller.calculateCorrection(error, true);
    TEST_ASSERT(out.state == syncwave::DriftCorrectionState::Uncertain, "Low confidence enters Uncertain state");

    out = controller.calculateCorrection(error, false);
    TEST_ASSERT(out.state == syncwave::DriftCorrectionState::Disconnected, "Unavailable endpoint enters Disconnected state");
    TEST_ASSERT(out.targetRateAdjustmentPpm == 0.0, "Disconnected endpoint commands 0.0 PPM");
}

void testSyntheticMultiClockDriftSimulation() {
    std::cout << "[TEST] Synthetic Multi-Clock Drift Simulation (1 min, 5 min, 10 min)\n";

    constexpr double nominalRate = 48000.0;
    constexpr double deviceBRate = 47999.0;
    const double expectedDriftPpm = ((deviceBRate - nominalRate) / nominalRate) * 1e6; // -20.833 ppm
    TEST_ASSERT(std::abs(expectedDriftPpm - (-20.833333)) < 0.01, "Expected nominal drift is -20.83 ppm");

    auto simulateUncorrectedPhaseErrorMs = [](double durationSec) {
        return durationSec * (20.83333333 / 1e6) * 1000.0;
    };

    double uncorrected1Min = simulateUncorrectedPhaseErrorMs(60.0);
    double uncorrected5Min = simulateUncorrectedPhaseErrorMs(300.0);
    double uncorrected10Min = simulateUncorrectedPhaseErrorMs(600.0);

    TEST_ASSERT(std::abs(uncorrected1Min - 1.25) < 0.05, "Uncorrected 1-min drift reaches ~1.25 ms (> 1.0ms deadband)");
    TEST_ASSERT(std::abs(uncorrected5Min - 6.25) < 0.1, "Uncorrected 5-min drift reaches ~6.25 ms (linear divergence)");
    TEST_ASSERT(std::abs(uncorrected10Min - 12.5) < 0.2, "Uncorrected 10-min drift reaches ~12.5 ms (significant latency lag)");

    syncwave::DeviceClock clockB("dev-b", 48000, 1000);
    syncwave::FilteredDriftEstimator estimator(5.0, 30, 500.0, 0.15, 0.20);
    syncwave::DriftControllerConfig config;
    config.deadbandMs = 1.0;
    config.maxAdjustmentPpm = 100.0;
    config.kp = 10.0;
    config.enableFeedforward = true;
    syncwave::DriftController controller(config);
    controller.setEnabled(true);

    syncwave::Resampler resamplerB(48000, 48000, 2);
    resamplerB.setSlewRatePpmPerSecond(5.0);

    double outputBPlayheadSec = 0.0;
    double masterTimelineSec = 0.0;
    double maxPhaseErrorAfterLockMs = 0.0;
    double phaseErrorAt1Min = 0.0;
    double phaseErrorAt5Min = 0.0;
    double phaseErrorAt10Min = 0.0;

    auto simStart = std::chrono::steady_clock::now();

    for (int step = 0; step < 6000; ++step) {
        double t = (step + 1) * 0.1; // 100 ms step
        masterTimelineSec = t;

        double framesRenderedThisStep = 4799.9; // 47999 Hz * 0.1s
        double masterFramesConsumed = framesRenderedThisStep * (1.0 + resamplerB.currentRateAdjustmentPpm() / 1e6);
        outputBPlayheadSec += masterFramesConsumed / 48000.0;

        syncwave::DeviceClockSample s;
        s.timestamp = simStart + std::chrono::milliseconds(static_cast<int>(t * 1000.0));
        s.clockPosition = static_cast<uint64_t>(t * 47999.0);
        s.clockFrequency = 48000;
        s.sampleRate = 48000;
        s.isValid = true;
        clockB.recordSample(s);

        auto syncErr = estimator.updateSyncError(
            "dev-b", "Device B", clockB,
            masterTimelineSec, static_cast<uint64_t>(masterTimelineSec * 48000),
            masterTimelineSec, outputBPlayheadSec);

        auto correction = controller.calculateCorrection(syncErr);
        resamplerB.setRateAdjustmentPpm(correction.targetRateAdjustmentPpm, false);

        std::vector<float> dummyIn(4800 * 2, 0.0f);
        std::vector<float> dummyOut(4800 * 2, 0.0f);
        resamplerB.process(dummyIn.data(), 4800, dummyOut.data(), 4800);

        double phaseErrMs = syncErr.phaseErrorMs;

        if (step == 600) {
            phaseErrorAt1Min = phaseErrMs;
        }
        if (step == 3000) {
            phaseErrorAt5Min = phaseErrMs;
        }
        if (step == 5999) {
            phaseErrorAt10Min = phaseErrMs;
        }

        if (t > 15.0) {
            maxPhaseErrorAfterLockMs = std::max(maxPhaseErrorAfterLockMs, std::abs(phaseErrMs));
        }
    }

    TEST_ASSERT(std::abs(phaseErrorAt1Min) <= 1.0, "Corrected 1-min phase error strictly <= 1.0 ms deadband");
    TEST_ASSERT(std::abs(phaseErrorAt5Min) <= 1.0, "Corrected 5-min phase error strictly <= 1.0 ms deadband");
    TEST_ASSERT(std::abs(phaseErrorAt10Min) <= 1.0, "Corrected 10-min phase error strictly <= 1.0 ms deadband");
    TEST_ASSERT(maxPhaseErrorAfterLockMs <= 1.05, "Max phase error in steady state strictly bounded within deadband");
    TEST_ASSERT(std::abs(resamplerB.currentRateAdjustmentPpm() - 20.83) < 2.0, "Resampler rate adjustment converged to match drift (+20.8 ppm)");
}

void testDeviceOutputAndRouterDriftIntegration() {
    std::cout << "[TEST] DeviceOutput & OutputRouter Drift Correction Integration\n";

    syncwave::OutputRouter router;
    syncwave::AudioDevice dev1{"dev-1", "Realtek Audio", syncwave::DeviceState::Active, true, false};
    syncwave::AudioDevice dev2{"dev-2", "Bluetooth Buds", syncwave::DeviceState::Active, false, false};

    router.addOutput(dev1);
    router.addOutput(dev2);
    router.initializeOutputsForTesting(48000, 2);

    TEST_ASSERT(!router.isDriftCorrectionEnabled(), "Drift correction initially disabled in router");

    router.setDriftCorrectionEnabled(true);
    TEST_ASSERT(router.isDriftCorrectionEnabled(), "Drift correction enabled in router");

    auto* out1 = router.getOutput("dev-1");
    auto* out2 = router.getOutput("dev-2");
    TEST_ASSERT(out1->isDriftCorrectionEnabled(), "Output 1 inherits drift correction enabled");
    TEST_ASSERT(out2->isDriftCorrectionEnabled(), "Output 2 inherits drift correction enabled");

    std::vector<float> chunk(4800 * 2, 0.1f);
    router.route(chunk.data(), 4800);
    router.sampleAllClocks();

    auto telem1 = out1->getTelemetry();
    TEST_ASSERT(telem1.driftState != syncwave::DriftCorrectionState::Disabled, "Telemetry reports active drift controller state");

    router.onDeviceDisconnected("dev-2");
    auto telem2 = out2->getTelemetry();
    TEST_ASSERT(telem2.driftState == syncwave::DriftCorrectionState::Disconnected, "Disconnected output enters Disconnected state");
    TEST_ASSERT(telem2.targetRateAdjustmentPpm == 0.0, "Disconnected output resets target rate adjustment");
}

void testDriftControllerPhysicalDirection() {
    std::cout << "[TEST] DriftController Physical Directionality & Deadband Verification (M10.1)\n";

    syncwave::DriftControllerConfig config;
    config.deadbandMs = 1.0;
    config.maxAdjustmentPpm = 100.0;
    config.kp = 10.0; // 10 ppm per ms excess error
    config.enableFeedforward = false; // isolate proportional feedback
    config.minObservationSec = 5.0;
    config.minSamples = 30;
    config.minConfidence = 0.90;

    syncwave::DriftController controller(config);
    controller.setEnabled(true);

    syncwave::SyncError error;
    error.isValid = true;
    error.confidence = 0.95;
    error.observationDurationSec = 10.0;
    error.filteredDriftPpm = 0.0;

    // 1. Deadband checks: +0.5 ms, -0.5 ms, +1.0 ms, -1.0 ms
    const double deadbandTests[] = {0.5, -0.5, 1.0, -1.0};
    for (double errMs : deadbandTests) {
        error.filteredPhaseErrorMs = errMs;
        auto out = controller.calculateCorrection(error);
        TEST_ASSERT(out.state == syncwave::DriftCorrectionState::Locked, "Inside deadband is Locked");
        TEST_ASSERT(out.proportionalTermPpm == 0.0, "Proportional feedback strictly 0 inside deadband");
        TEST_ASSERT(out.targetRateAdjustmentPpm == 0.0, "Target adjustment strictly 0 inside deadband");
    }

    // 2. Lagging output: e = +2.0 ms -> output behind target -> must speed up (+correction)
    error.filteredPhaseErrorMs = 2.0;
    auto outLag2 = controller.calculateCorrection(error);
    TEST_ASSERT(outLag2.state == syncwave::DriftCorrectionState::Correcting, "+2.0 ms is Correcting");
    TEST_ASSERT(outLag2.proportionalTermPpm > 0.0, "Lagging output generates POSITIVE proportional feedback");
    TEST_ASSERT(std::abs(outLag2.proportionalTermPpm - 10.0) < 1e-6, "+2.0 ms excess error (+1.0 ms) * 10 = +10.0 ppm");
    TEST_ASSERT(outLag2.targetRateAdjustmentPpm > 0.0, "Target adjustment sign is POSITIVE (speeds up output)");

    // 3. Lagging output: e = +10.0 ms -> output behind target -> must speed up (+correction)
    error.filteredPhaseErrorMs = 10.0;
    auto outLag10 = controller.calculateCorrection(error);
    TEST_ASSERT(std::abs(outLag10.proportionalTermPpm - 90.0) < 1e-6, "+10.0 ms excess error (+9.0 ms) * 10 = +90.0 ppm");
    TEST_ASSERT(outLag10.targetRateAdjustmentPpm == 90.0, "Target adjustment is +90.0 ppm");

    // 4. Leading output: e = -2.0 ms -> output ahead of target -> must slow down (-correction)
    error.filteredPhaseErrorMs = -2.0;
    auto outLead2 = controller.calculateCorrection(error);
    TEST_ASSERT(outLead2.proportionalTermPpm < 0.0, "Leading output generates NEGATIVE proportional feedback");
    TEST_ASSERT(std::abs(outLead2.proportionalTermPpm - (-10.0)) < 1e-6, "-2.0 ms excess error (-1.0 ms) * 10 = -10.0 ppm");
    TEST_ASSERT(outLead2.targetRateAdjustmentPpm < 0.0, "Target adjustment sign is NEGATIVE (slows down output)");

    // 5. Leading output: e = -10.0 ms -> output ahead of target -> must slow down (-correction)
    error.filteredPhaseErrorMs = -10.0;
    auto outLead10 = controller.calculateCorrection(error);
    TEST_ASSERT(std::abs(outLead10.proportionalTermPpm - (-90.0)) < 1e-6, "-10.0 ms excess error (-9.0 ms) * 10 = -90.0 ppm");
    TEST_ASSERT(outLead10.targetRateAdjustmentPpm == -90.0, "Target adjustment is -90.0 ppm");
}

void testResamplerPullModeMathematicalVerification() {
    std::cout << "[TEST] Resampler Pull-Mode Ratio & Consumption Direction (M10.1)\n";

    // Standard 48 kHz stereo resampler
    syncwave::Resampler resamplerNominal(48000, 48000, 2);
    syncwave::Resampler resamplerPositiveAdj(48000, 48000, 2);
    resamplerPositiveAdj.setRateAdjustmentPpm(1000.0, true); // +1000 ppm immediate

    TEST_ASSERT(resamplerNominal.baseRatio() == 1.0, "Nominal base ratio is 1.0");
    TEST_ASSERT(resamplerPositiveAdj.ratio() > 1.0, "Positive adjustment increases ratio > 1.0");
    TEST_ASSERT(std::abs(resamplerPositiveAdj.ratio() - 1.001) < 1e-9, "Ratio at +1000 ppm is exactly 1.001");

    // Push 48,000 frames into two identical ring buffers
    syncwave::RingBuffer queueNominal(96000, 2);
    syncwave::RingBuffer queuePositiveAdj(96000, 2);

    std::vector<float> inputAudio(48000 * 2, 0.5f);
    queueNominal.write(inputAudio.data(), 48000);
    queuePositiveAdj.write(inputAudio.data(), 48000);

    // Pull 24,000 output frames from each in 50 chunks of 480 frames (simulating WASAPI callback buffer sizes)
    std::vector<float> blockBuf1(480 * 2);
    std::vector<float> blockBuf2(480 * 2);

    size_t pulled1 = 0;
    size_t pulled2 = 0;
    for (int i = 0; i < 50; ++i) {
        pulled1 += resamplerNominal.pull(queueNominal, blockBuf1.data(), 480);
        pulled2 += resamplerPositiveAdj.pull(queuePositiveAdj, blockBuf2.data(), 480);
    }

    TEST_ASSERT(pulled1 == 24000, "Pulled 24,000 output frames (nominal)");
    TEST_ASSERT(pulled2 == 24000, "Pulled 24,000 output frames (positive adjustment)");

    // Inspect remaining frames in queues
    size_t remainNominal = queueNominal.availableToRead();
    size_t remainPositiveAdj = queuePositiveAdj.availableToRead();

    // Since ratio was 1.001, resamplerPositiveAdj consumed 24,000 * 1.001 = 24,024 master frames
    // (plus/minus fractional filter boundary)
    size_t consumedNominal = 48000 - remainNominal;
    size_t consumedPositiveAdj = 48000 - remainPositiveAdj;

    TEST_ASSERT(consumedPositiveAdj > consumedNominal, 
                "Positive adjustment consumes MORE master frames than nominal");

    // Direct block processing test:
    // With ratio 1.001 (+1000 ppm), processing 24,024 input frames yields 24,000 output frames (24024 / 1.001 = 24000)
    std::vector<float> inDirect(24024 * 2, 0.5f);
    std::vector<float> outDirect(25000 * 2, 0.0f);
    syncwave::Resampler directResampler(48000, 48000, 2);
    directResampler.setRateAdjustmentPpm(1000.0, true);
    size_t directOut = directResampler.process(inDirect.data(), 24024, outDirect.data(), 25000);
    TEST_ASSERT(directOut == 24000, "Direct process: 24,024 master frames produces 24,000 output frames at +1000 ppm");

    // Physical conclusion: Consuming master frames faster means the audio delivered to DAC
    // advances further along the master timeline in the same playback time, advancing a lagging output.
}

void testSeparateFrequencyAndPhaseCorrection() {
    std::cout << "[TEST] Separation of Frequency Drift (Feedforward) vs Phase Offset (Feedback) (M10.1)\n";

    syncwave::DriftControllerConfig config;
    config.deadbandMs = 1.0;
    config.maxAdjustmentPpm = 100.0;
    config.kp = 10.0;
    config.enableFeedforward = true;
    config.minObservationSec = 5.0;
    config.minSamples = 30;
    config.minConfidence = 0.90;

    syncwave::DriftController controller(config);
    controller.setEnabled(true);

    syncwave::SyncError error;
    error.isValid = true;
    error.confidence = 0.95;
    error.observationDurationSec = 10.0;

    // Test 1: Output clock runs +25 ppm faster than master, zero phase error
    // Clock is fast -> DAC demands frames faster -> resampler must pull fewer master frames per output frame -> u = -25 ppm
    error.filteredDriftPpm = 25.0;
    error.filteredPhaseErrorMs = 0.0;
    auto out1 = controller.calculateCorrection(error);
    TEST_ASSERT(out1.proportionalTermPpm == 0.0, "Zero phase error produces 0 proportional feedback");
    TEST_ASSERT(out1.feedforwardTermPpm == -25.0, "Fast clock (+25 ppm) produces -25 ppm feedforward cancellation");
    TEST_ASSERT(out1.targetRateAdjustmentPpm == -25.0, "Total command is -25.0 ppm");

    // Test 2: Output clock runs -25 ppm slower than master, zero phase error
    // Clock is slow -> DAC demands frames slower -> resampler must pull more master frames per output frame -> u = +25 ppm
    error.filteredDriftPpm = -25.0;
    error.filteredPhaseErrorMs = 0.0;
    auto out2 = controller.calculateCorrection(error);
    TEST_ASSERT(out2.proportionalTermPpm == 0.0, "Zero phase error produces 0 proportional feedback");
    TEST_ASSERT(out2.feedforwardTermPpm == 25.0, "Slow clock (-25 ppm) produces +25 ppm feedforward cancellation");
    TEST_ASSERT(out2.targetRateAdjustmentPpm == 25.0, "Total command is +25.0 ppm");

    // Test 3: Static phase offset (+5.0 ms lag) but ZERO frequency drift (filteredDriftPpm = 0.0)
    error.filteredDriftPpm = 0.0;
    error.filteredPhaseErrorMs = 5.0;
    auto out3 = controller.calculateCorrection(error);
    TEST_ASSERT(out3.feedforwardTermPpm == 0.0, "Zero frequency drift produces 0 feedforward");
    TEST_ASSERT(std::abs(out3.proportionalTermPpm - 40.0) < 1e-6, "+5 ms lag (excess +4 ms) produces +40 ppm feedback");
    TEST_ASSERT(out3.targetRateAdjustmentPpm == 40.0, "Total command is +40.0 ppm (pure phase feedback)");
}

void testSyntheticControllerValidationCasesAtoH() {
    std::cout << "[TEST] Synthetic Controller Validation: Cases A through H (M10.1)\n";

    syncwave::DriftControllerConfig config;
    config.deadbandMs = 1.0;
    config.maxAdjustmentPpm = 100.0;
    config.kp = 10.0;
    config.enableFeedforward = true;
    config.minObservationSec = 5.0;
    config.minSamples = 30;
    config.minConfidence = 0.90;

    syncwave::DriftController controller(config);
    controller.setEnabled(true);

    syncwave::SyncError error;
    error.isValid = true;
    error.confidence = 0.95;
    error.observationDurationSec = 10.0;

    // Case A: Zero drift, zero phase offset -> correction ~ 0 ppm
    error.filteredDriftPpm = 0.0;
    error.filteredPhaseErrorMs = 0.0;
    auto resA = controller.calculateCorrection(error);
    TEST_ASSERT(resA.state == syncwave::DriftCorrectionState::Locked, "Case A: State is Locked");
    TEST_ASSERT(resA.targetRateAdjustmentPpm == 0.0, "Case A: Correction is strictly 0.0 ppm");

    // Case B: +25 ppm output clock error -> correction converges toward -25 ppm to cancel
    error.filteredDriftPpm = 25.0;
    error.filteredPhaseErrorMs = 0.0;
    auto resB = controller.calculateCorrection(error);
    TEST_ASSERT(resB.targetRateAdjustmentPpm == -25.0, "Case B: Correction is -25.0 ppm to cancel +25 ppm clock");

    // Case C: -25 ppm output clock error -> opposite correction direction (+25 ppm)
    error.filteredDriftPpm = -25.0;
    error.filteredPhaseErrorMs = 0.0;
    auto resC = controller.calculateCorrection(error);
    TEST_ASSERT(resC.targetRateAdjustmentPpm == 25.0, "Case C: Correction is +25.0 ppm to cancel -25 ppm clock");

    // Case D: +2 ms phase lag, zero frequency error -> bounded positive correction
    error.filteredDriftPpm = 0.0;
    error.filteredPhaseErrorMs = 2.0;
    auto resD = controller.calculateCorrection(error);
    TEST_ASSERT(resD.state == syncwave::DriftCorrectionState::Correcting, "Case D: State is Correcting");
    TEST_ASSERT(resD.targetRateAdjustmentPpm == 10.0, "Case D: Bounded positive correction +10.0 ppm");

    // Case E: -2 ms phase lead, zero frequency error -> opposite phase correction (-10 ppm)
    error.filteredDriftPpm = 0.0;
    error.filteredPhaseErrorMs = -2.0;
    auto resE = controller.calculateCorrection(error);
    TEST_ASSERT(resE.state == syncwave::DriftCorrectionState::Correcting, "Case E: State is Correcting");
    TEST_ASSERT(resE.targetRateAdjustmentPpm == -10.0, "Case E: Bounded negative correction -10.0 ppm");

    // Case F: Static +100 ms latency offset, zero drift -> baseline reference absorbs offset
    // Simulated via FilteredDriftEstimator with constant 100 ms offset
    syncwave::FilteredDriftEstimator estimator(5.0, 30, 500.0, 0.15, 0.20);
    syncwave::DeviceClock clockF("dev-f", 48000, 100);
    auto t0 = std::chrono::steady_clock::now();

    for (int i = 0; i <= 60; ++i) {
        syncwave::DeviceClockSample s;
        s.timestamp = t0 + std::chrono::milliseconds(i * 100);
        s.clockPosition = i * 4800; // exact 48000 Hz (zero drift)
        s.clockFrequency = 48000;
        s.sampleRate = 48000;
        s.isValid = true;
        clockF.recordSample(s);

        // Constant 100 ms offset: target is always 100 ms ahead of output
        double targetPlayhead = (i * 0.1) + 0.100;
        double outputPlayhead = (i * 0.1);

        auto fResult = estimator.update(clockF, targetPlayhead, outputPlayhead);
        if (i == 60) {
            TEST_ASSERT(fResult.isValid, "Case F: Estimator is valid after 6s");
            TEST_ASSERT(std::abs(fResult.rawPhaseErrorMs) < 0.01, 
                        "Case F: Static 100ms offset absorbed by baseline (phase error == 0.0 ms)");
            TEST_ASSERT(std::abs(fResult.filteredDriftPpm) < 1.0, 
                        "Case F: Static offset NOT interpreted as frequency drift (~0 ppm)");

            syncwave::SyncError errF;
            errF.isValid = true;
            errF.confidence = fResult.confidence;
            errF.observationDurationSec = fResult.observationDurationSec;
            errF.filteredPhaseErrorMs = fResult.filteredPhaseErrorMs;
            errF.filteredDriftPpm = fResult.filteredDriftPpm;

            auto resF = controller.calculateCorrection(errF);
            TEST_ASSERT(resF.state == syncwave::DriftCorrectionState::Locked, "Case F: Controller remains Locked");
            TEST_ASSERT(std::abs(resF.targetRateAdjustmentPpm) < 0.1, "Case F: Correction is 0.0 ppm");
        }
    }

    // Case G: Noisy measurement with +/-0.5 ms phase noise inside deadband -> no oscillation, no saturation
    for (int step = 0; step < 100; ++step) {
        double noise = 0.5 * std::sin(step * 0.5); // sinusoidal noise in [-0.5, +0.5] ms
        error.filteredPhaseErrorMs = noise;
        error.filteredDriftPpm = 0.0;
        auto resG = controller.calculateCorrection(error);
        TEST_ASSERT(resG.state == syncwave::DriftCorrectionState::Locked, "Case G: Controller stays Locked in deadband");
        TEST_ASSERT(resG.proportionalTermPpm == 0.0, "Case G: Zero proportional chatter on noise");
        TEST_ASSERT(resG.targetRateAdjustmentPpm == 0.0, "Case G: Zero adjustment commanded on noise");
        TEST_ASSERT(!resG.isClamped, "Case G: No clamp saturation");
    }

    // Case H: Sudden impossible measurement (+1000 ppm jump) -> outlier rejection / Uncertain state
    syncwave::FilteredDriftEstimator estimatorH(5.0, 30, 500.0, 0.15, 0.20);
    syncwave::DeviceClock clockH("dev-h", 48000, 100);

    for (int i = 0; i <= 55; ++i) {
        syncwave::DeviceClockSample s;
        s.timestamp = t0 + std::chrono::milliseconds(i * 100);
        s.clockPosition = i * 4800;
        s.clockFrequency = 48000;
        s.sampleRate = 48000;
        s.isValid = true;
        clockH.recordSample(s);
        estimatorH.update(clockH, i * 0.1, i * 0.1);
    }

    // Inject +1000 ppm spike
    syncwave::DeviceClockSample spike;
    spike.timestamp = t0 + std::chrono::milliseconds(56 * 100);
    spike.clockPosition = 56 * 4800 + 4800; // impossible jump
    spike.clockFrequency = 48000;
    spike.sampleRate = 48000;
    spike.isValid = true;
    clockH.recordSample(spike);

    auto resH = estimatorH.update(clockH, 5.6, 5.6);
    TEST_ASSERT(resH.isOutlierRejected, "Case H: Extreme measurement successfully flagged as outlier");
    TEST_ASSERT(resH.confidence <= 0.2, "Case H: Confidence severely penalized on outlier");

    syncwave::SyncError errH;
    errH.isValid = true;
    errH.confidence = resH.confidence; // 0.2 < minConfidence 0.90
    errH.observationDurationSec = 5.6;
    errH.filteredPhaseErrorMs = resH.filteredPhaseErrorMs;
    errH.filteredDriftPpm = resH.filteredDriftPpm;

    auto controllerOutH = controller.calculateCorrection(errH);
    TEST_ASSERT(controllerOutH.state == syncwave::DriftCorrectionState::Uncertain, 
                "Case H: Low confidence triggers Uncertain state");
    TEST_ASSERT(!controllerOutH.isClamped, "Case H: Controller does not slam into clamp on outlier");
}

void testChirpGeneratorBasicsAndTukeyWindow() {
    std::cout << "[TEST] ChirpGenerator: Sweep Synthesis & Tukey Window (M11)\n";

    syncwave::ChirpParameters params;
    params.sampleRate = 48000;
    params.durationSec = 0.100;      // 100 ms chirp = 4800 frames
    params.startFreqHz = 300.0;
    params.endFreqHz = 4000.0;
    params.leadInSec = 0.200;        // 200 ms lead-in = 9600 frames
    params.leadOutSec = 0.300;       // 300 ms lead-out = 14400 frames
    params.amplitude = 0.25f;
    params.windowRampSec = 0.010;    // 10 ms ramp = 480 frames

    syncwave::ChirpGenerator gen(params);

    TEST_ASSERT(gen.chirpFrames() == 4800, "Chirp portion is exactly 4800 frames");
    TEST_ASSERT(gen.chirpMasterFrameIndex() == 9600, "Chirp starts at frame 9600");
    TEST_ASSERT(gen.totalFrames() == 9600 + 4800 + 14400, "Total sequence is 28800 frames (600 ms)");
    TEST_ASSERT(!gen.isComplete(), "Generator starts incomplete");

    auto ref = gen.generateReferenceChirp();
    TEST_ASSERT(ref.size() == 4800, "Reference chirp size matches active chirp frames");

    // Check Tukey windowing on reference chirp
    TEST_ASSERT(std::abs(ref.front()) < 0.01f, "First sample is tapered near zero");
    TEST_ASSERT(std::abs(ref.back()) < 0.01f, "Last sample is tapered near zero");

    // Check peak amplitude does not exceed configured level
    float maxAmp = 0.0f;
    for (float s : ref) {
        maxAmp = std::max(maxAmp, std::abs(s));
    }
    TEST_ASSERT(maxAmp <= 0.2501f, "Peak amplitude strictly respects safe configured level");
    TEST_ASSERT(maxAmp >= 0.20f, "Signal reaches full expected amplitude in sustain portion");

    // Generate full stereo stream and verify silence padding
    std::vector<float> stream(gen.totalFrames() * 2);
    size_t genCount = gen.generateFrames(stream.data(), gen.totalFrames(), 2);
    TEST_ASSERT(genCount == gen.totalFrames(), "Generated full frame sequence");
    TEST_ASSERT(gen.isComplete(), "Generator is marked complete");

    // Verify lead-in silence (samples 0..9599 are strictly zero)
    bool leadInSilent = true;
    for (size_t i = 0; i < 9600 * 2; ++i) {
        if (stream[i] != 0.0f) { leadInSilent = false; break; }
    }
    TEST_ASSERT(leadInSilent, "Lead-in frames are strictly zero silence");

    // Verify lead-out silence (samples 14400..28799 are strictly zero)
    bool leadOutSilent = true;
    for (size_t i = (9600 + 4800) * 2; i < gen.totalFrames() * 2; ++i) {
        if (stream[i] != 0.0f) { leadOutSilent = false; break; }
    }
    TEST_ASSERT(leadOutSilent, "Lead-out frames are strictly zero silence");
}

void testCorrelationDetectorExactPeak() {
    std::cout << "[TEST] CorrelationDetector: Exact Peak & Sub-Sample Precision (M11)\n";

    syncwave::ChirpParameters params;
    params.sampleRate = 48000;
    params.durationSec = 0.150; // 150 ms = 7200 frames
    params.amplitude = 0.30f;
    syncwave::ChirpGenerator gen(params);
    auto ref = gen.generateReferenceChirp();

    // Create 1.0 second test buffer (48,000 samples)
    std::vector<float> target(48000, 0.0f);
    // Embed reference chirp at sample index 4800 (exactly 100.0 ms delay)
    const size_t embedIndex = 4800;
    for (size_t i = 0; i < ref.size(); ++i) {
        target[embedIndex + i] = ref[i];
    }

    syncwave::CorrelationDetector detector;
    auto res = detector.detect(ref, target, 48000);

    TEST_ASSERT(res.isDetected, "Chirp arrival detected in clean buffer");
    TEST_ASSERT(res.peakScore > 0.999f, "Normalized correlation peak score > 0.999 for identical signal");
    TEST_ASSERT(std::abs(res.peakIndex - static_cast<double>(embedIndex)) < 0.01, 
                "Detected arrival sample matches embedded index within 0.01 samples");
    TEST_ASSERT(std::abs(res.arrivalTimeSec - 0.100) < 1e-5, 
                "Detected arrival time is exactly 100.0 ms");
    TEST_ASSERT(res.peakToNoiseRatio > 10.0f, "Peak-to-noise ratio is high (> 10.0)");
    TEST_ASSERT(res.confidence > 0.95f, "Detection confidence > 95%");
}

void testCorrelationDetectorKnownSyntheticDelays() {
    std::cout << "[TEST] CorrelationDetector: Known Synthetic Delays (10 to 500 ms) (M11)\n";

    syncwave::ChirpParameters params;
    params.sampleRate = 48000;
    params.durationSec = 0.100; // 100 ms = 4800 frames
    params.amplitude = 0.25f;
    syncwave::ChirpGenerator gen(params);
    auto ref = gen.generateReferenceChirp();

    std::vector<double> testDelaysMs = { 10.0, 25.0, 50.0, 100.0, 250.0, 500.0 };
    syncwave::CorrelationDetector detector;

    for (double delayMs : testDelaysMs) {
        size_t offsetSamples = static_cast<size_t>(std::round((delayMs / 1000.0) * 48000.0));
        std::vector<float> target(offsetSamples + ref.size() + 2400, 0.0f);

        for (size_t i = 0; i < ref.size(); ++i) {
            target[offsetSamples + i] = ref[i];
        }

        auto res = detector.detect(ref, target, 48000);
        TEST_ASSERT(res.isDetected, "Detected chirp at known delay " + std::to_string(delayMs) + " ms");
        TEST_ASSERT(std::abs(res.arrivalTimeSec * 1000.0 - delayMs) < 0.05, 
                    "Arrival time matches target delay within 0.05 ms");
    }
}

void testCorrelationDetectorNoisySignal() {
    std::cout << "[TEST] CorrelationDetector: Robustness Under Additive Noise (M11)\n";

    syncwave::ChirpParameters params;
    params.sampleRate = 48000;
    params.durationSec = 0.150;
    params.amplitude = 0.25f;
    syncwave::ChirpGenerator gen(params);
    auto ref = gen.generateReferenceChirp();

    const size_t embedIndex = 2400; // 50 ms delay
    std::vector<float> target(24000, 0.0f);
    for (size_t i = 0; i < ref.size(); ++i) {
        target[embedIndex + i] = ref[i];
    }

    // Add pseudo-random white noise (linear congruential generator)
    uint32_t seed = 123456789;
    for (float& s : target) {
        seed = seed * 1664525 + 1013904223;
        float noise = (static_cast<float>(seed & 0xFFFF) / 32768.0f - 1.0f) * 0.05f; // noise +/- 0.05
        s += noise;
    }

    syncwave::CorrelationDetector detector;
    auto res = detector.detect(ref, target, 48000);

    TEST_ASSERT(res.isDetected, "Detected chirp in noisy environment");
    TEST_ASSERT(std::abs(res.arrivalTimeSec * 1000.0 - 50.0) < 0.5, 
                "Detected arrival is within 0.5 ms of true 50.0 ms arrival under noise");
    TEST_ASSERT(res.noiseFloorRms > 0.001f, "Noise floor RMS reflects injected noise level");
}

void testCorrelationDetectorMultiEcho() {
    std::cout << "[TEST] CorrelationDetector: Multi-Echo & Room Reflection Immunity (M11)\n";

    syncwave::ChirpParameters params;
    params.sampleRate = 48000;
    params.durationSec = 0.100;
    params.amplitude = 0.25f;
    syncwave::ChirpGenerator gen(params);
    auto ref = gen.generateReferenceChirp();

    // Primary direct arrival at 100 ms (sample 4800) with amplitude 1.0
    // Secondary reflected echo at 135 ms (sample 6480) with amplitude 0.5
    std::vector<float> target(24000, 0.0f);
    const size_t primaryIdx = 4800;
    const size_t echoIdx = 6480;

    for (size_t i = 0; i < ref.size(); ++i) {
        target[primaryIdx + i] += ref[i];
        if (echoIdx + i < target.size()) {
            target[echoIdx + i] += ref[i] * 0.5f; // 50% echo
        }
    }

    syncwave::CorrelationDetector detector;
    auto res = detector.detect(ref, target, 48000);

    TEST_ASSERT(res.isDetected, "Primary chirp detected despite secondary multipath echo");
    TEST_ASSERT(std::abs(res.arrivalTimeSec * 1000.0 - 100.0) < 0.1, 
                "Arrival is locked to primary direct wave (100.0 ms), not echo (135.0 ms)");
}

void testCorrelationDetectorFailedCorrelation() {
    std::cout << "[TEST] CorrelationDetector: Outlier & Silence / Uncorrelated Rejection (M11)\n";

    syncwave::ChirpParameters params;
    syncwave::ChirpGenerator gen(params);
    auto ref = gen.generateReferenceChirp();

    // Pure silence buffer
    std::vector<float> silence(24000, 0.0f);
    syncwave::CorrelationDetector detector;
    auto resSilence = detector.detect(ref, silence, 48000);
    TEST_ASSERT(!resSilence.isDetected, "Silence correctly rejected (isDetected == false)");
    TEST_ASSERT(resSilence.confidence < 0.2f, "Confidence is near zero for silence");

    // Pure random noise buffer (no chirp embedded)
    std::vector<float> noise(24000, 0.0f);
    uint32_t seed = 987654321;
    for (float& s : noise) {
        seed = seed * 1664525 + 1013904223;
        s = (static_cast<float>(seed & 0xFFFF) / 32768.0f - 1.0f) * 0.2f;
    }
    auto resNoise = detector.detect(ref, noise, 48000);
    TEST_ASSERT(!resNoise.isDetected, "Uncorrelated noise correctly rejected");
    TEST_ASSERT(resNoise.peakScore < 0.35f, "Peak score on noise is below threshold");
}

void testSampleRateDomainConversion() {
    std::cout << "[TEST] Sample-Rate Domain Conversion (44.1 kHz Out vs 48 kHz Mic) (M11)\n";

    // Output is 44.1 kHz, mic is 48.0 kHz
    const uint32_t outRate = 44100;
    const uint32_t micRate = 48000;

    syncwave::ChirpParameters params;
    params.sampleRate = outRate;
    params.durationSec = 0.150;
    params.leadInSec = 0.200; // 200 ms lead-in silence @ 44.1k = 8820 frames
    syncwave::ChirpGenerator gen(params);
    auto ref = gen.generateReferenceChirp();

    TEST_ASSERT(gen.chirpMasterFrameIndex() == 8820, "Lead-in frames @ 44.1kHz is 8820");

    // True physical acoustic delay = 45.0 ms
    // Emission starts at t = 200.0 ms. Physical arrival at t = 200.0 + 45.0 = 245.0 ms.
    // At micRate 48 kHz, 245.0 ms corresponds to sample index: 0.245 * 48000 = 11760.
    std::vector<float> micBuffer(48000, 0.0f);
    const size_t arrivalSample = 11760;

    // Resample reference chirp from 44.1k to 48k for simulated acoustic recording
    syncwave::Resampler resampler(outRate, micRate, 1);
    std::vector<float> micRef(static_cast<size_t>(ref.size() * (48000.0 / 44100.0)) + 100);
    size_t produced = resampler.process(ref.data(), ref.size(), micRef.data(), micRef.size());
    micRef.resize(produced);

    for (size_t i = 0; i < micRef.size(); ++i) {
        if (arrivalSample + i < micBuffer.size()) {
            micBuffer[arrivalSample + i] = micRef[i];
        }
    }

    syncwave::CorrelationDetector detector;
    auto corr = detector.detect(micRef, micBuffer, micRate);
    TEST_ASSERT(corr.isDetected, "Detected chirp across sample rate boundary");

    double emissionSec = static_cast<double>(gen.chirpMasterFrameIndex()) / static_cast<double>(outRate);
    double arrivalSec = corr.arrivalTimeSec;
    double calculatedLatencyMs = (arrivalSec - emissionSec) * 1000.0;

    TEST_ASSERT(std::abs(calculatedLatencyMs - 45.0) < 0.2, 
                "Calculated latency matches physical 45.0 ms within 0.2 ms across mixed sample rates");
}

void testMultiRunStatisticsAndOutlierRejection() {
    std::cout << "[TEST] Multi-Run Statistical Aggregation & Robust Estimator (M11/M11.1)\n";

    syncwave::AcousticCalibrationConfig config;
    config.runs = 7;
    config.minValidRuns = 5;
    syncwave::AcousticCalibrator calibrator(config);

    syncwave::ChirpGenerator gen(config.chirpParams);
    auto ref = gen.generateReferenceChirp();

    // Create 7 synthetic capture runs with true delays:
    // Run 1: 24.1 ms, Run 2: 24.3 ms, Run 3: 24.2 ms, Run 4: 24.4 ms, Run 5: 24.2 ms, Run 6: 24.1 ms, Run 7: 24.3 ms
    std::vector<double> delaysMs = { 24.1, 24.3, 24.2, 24.4, 24.2, 24.1, 24.3 };
    std::vector<std::vector<float>> buffers;

    for (double d : delaysMs) {
        size_t leadInSamples = gen.chirpMasterFrameIndex();
        size_t delaySamples = static_cast<size_t>(std::round((d / 1000.0) * 48000.0));
        size_t totalArrival = leadInSamples + delaySamples;

        std::vector<float> buf(totalArrival + ref.size() + 2400, 0.0f);
        for (size_t i = 0; i < ref.size(); ++i) {
            buf[totalArrival + i] = ref[i];
        }
        buffers.push_back(buf);
    }

    auto result = calibrator.evaluateSyntheticRuns(
        "dev-test", "Test Speaker", "mic-test", "Test Mic",
        buffers, { 0, 0, 0, 0, 0, 0, 0 }, 48000, 48000);

    TEST_ASSERT(result.isValid, "7-run synthetic calibration is valid");
    TEST_ASSERT(result.validRuns == 7, "All 7 runs are valid");
    TEST_ASSERT(std::abs(result.medianLatencyMs - 24.2) < 0.05, "Median latency is 24.2 ms");
    TEST_ASSERT(result.stdDevMs < 0.20, "Uncertainty (std dev) is < 0.20 ms");
    TEST_ASSERT(result.minLatencyMs >= 24.0 && result.maxLatencyMs <= 24.5, "Min/Max bounds correct");
}

void testAcousticCalibrationStricterAcceptanceCriteria() {
    std::cout << "[TEST] AcousticCalibrator: Acceptance Criteria & Failure Non-Overwrite (M11.1)\n";

    syncwave::AcousticCalibrationConfig config;
    config.runs = 7;
    config.minValidRuns = 5;
    syncwave::AcousticCalibrator calibrator(config);

    syncwave::ChirpGenerator gen(config.chirpParams);
    auto ref = gen.generateReferenceChirp();

    // Scenario A: Only 4 valid runs out of 7 (3 pure silence runs) -> Must fail (4 < 5)
    std::vector<std::vector<float>> buffersA;
    for (size_t i = 0; i < 7; ++i) {
        if (i < 4) {
            // Valid chirp at 50 ms
            size_t totalArrival = gen.chirpMasterFrameIndex() + 2400;
            std::vector<float> buf(totalArrival + ref.size() + 1000, 0.0f);
            for (size_t k = 0; k < ref.size(); ++k) buf[totalArrival + k] = ref[k];
            buffersA.push_back(buf);
        } else {
            // Silence buffer (fails detection)
            std::vector<float> silenceBuf(48000, 0.0f);
            buffersA.push_back(silenceBuf);
        }
    }

    auto resultA = calibrator.evaluateSyntheticRuns(
        "dev-fail", "Fail Speaker", "mic-test", "Test Mic",
        buffersA, {0,0,0,0,0,0,0}, 48000, 48000);

    TEST_ASSERT(!resultA.isValid, "Calibration rejected when valid runs (4) < minValidRuns (5)");
    TEST_ASSERT(resultA.validRuns == 4, "Detected exactly 4 valid runs");
    TEST_ASSERT(resultA.validationMessage.find("Insufficient valid runs") != std::string::npos, 
                "Validation message states insufficient valid runs");

    // Scenario B: High variance / unstable latency runs (e.g. 50 ms, 120 ms, 200 ms, 350 ms, 500 ms)
    std::vector<std::vector<float>> buffersB;
    std::vector<double> wildDelays = { 50.0, 120.0, 200.0, 350.0, 500.0, 60.0, 180.0 };
    for (double d : wildDelays) {
        size_t totalArrival = gen.chirpMasterFrameIndex() + static_cast<size_t>(std::round(d * 48.0));
        std::vector<float> buf(totalArrival + ref.size() + 1000, 0.0f);
        for (size_t k = 0; k < ref.size(); ++k) buf[totalArrival + k] = ref[k];
        buffersB.push_back(buf);
    }

    auto resultB = calibrator.evaluateSyntheticRuns(
        "dev-wild", "Wild Speaker", "mic-test", "Test Mic",
        buffersB, {0,0,0,0,0,0,0}, 48000, 48000);

    TEST_ASSERT(!resultB.isValid, "Calibration rejected when standard deviation exceeds max acceptable limit");
    TEST_ASSERT(resultB.stdDevMs > config.maxAcceptableStdDevMs, "Uncertainty exceeded 15.0 ms limit");
}

void testCorrelationDetectorMultiPeakAndSecondaryReflection() {
    std::cout << "[TEST] CorrelationDetector: Multi-Peak & Secondary Reflection Diagnostics (M11.1)\n";

    syncwave::ChirpParameters params;
    params.sampleRate = 48000;
    params.durationSec = 0.100;
    params.amplitude = 0.30f;
    syncwave::ChirpGenerator gen(params);
    auto ref = gen.generateReferenceChirp();

    // Primary direct arrival at 100 ms (sample 4800), amp 1.0
    // Strong secondary multipath reflection at 132.6 ms (sample 6365, +32.6 ms separation like M11 hardware!), amp 0.6
    std::vector<float> target(30000, 0.0f);
    const size_t primarySample = 4800;
    const size_t secondarySample = 6365;

    for (size_t i = 0; i < ref.size(); ++i) {
        target[primarySample + i] += ref[i];
        target[secondarySample + i] += ref[i] * 0.60f;
    }

    syncwave::CorrelationDetector detector;
    auto res = detector.detect(ref, target, 48000);

    TEST_ASSERT(res.isDetected, "Direct wave successfully detected");
    TEST_ASSERT(std::abs(res.arrivalTimeSec * 1000.0 - 100.0) < 0.1, "Primary arrival locked to 100.0 ms");
    TEST_ASSERT(res.secondaryPeakScore > 0.50f, "Secondary peak detected with score > 0.50");
    TEST_ASSERT(std::abs(res.peakSeparationMs - 32.6) < 0.2, "Secondary peak separation correctly measured at ~32.6 ms");
    TEST_ASSERT(res.peakToSecondaryRatio > 1.3f && res.peakToSecondaryRatio < 1.9f, 
                "Peak to secondary ratio reflects relative reflection amplitude (~1.6x)");
}

void testAcousticCalibrationDirectionAndTwoDeviceAlignment() {
    std::cout << "[TEST] Acoustic Latency Calibration Direction & Two-Device Proof (M11.1)\n";

    // Device A (Realtek Speakers):
    // Software latency = 80.0 ms, Measured acoustic arrival offset = 107.0 ms
    // Effective latency = 80.0 + 107.0 = 187.0 ms
    syncwave::OutputLatencyModel modelRealtek;
    modelRealtek.deviceId = "realtek-speakers";
    modelRealtek.deviceName = "Realtek Speakers";
    modelRealtek.sampleRate = 48000;
    modelRealtek.wasapiStreamLatencyMs = 10.0;
    modelRealtek.wasapiPaddingMs = 20.0;
    modelRealtek.queueLatencyMs = 50.0;
    modelRealtek.optionalCalibrationOffsetMs = 107.0;
    modelRealtek.updateTotals();

    // Device B (realme Buds T310):
    // Software latency = 80.0 ms, Measured acoustic arrival offset = 275.0 ms (e.g. Bluetooth A2DP transport buffer)
    // Under Model B: Effective latency = 275.0 ms (no double-counting of software latency)
    syncwave::OutputLatencyModel modelBuds;
    modelBuds.deviceId = "buds-t310";
    modelBuds.deviceName = "Buds T310";
    modelBuds.sampleRate = 48000;
    modelBuds.wasapiStreamLatencyMs = 10.0;
    modelBuds.wasapiPaddingMs = 20.0;
    modelBuds.queueLatencyMs = 50.0;
    modelBuds.optionalCalibrationOffsetMs = 275.0;
    modelBuds.updateTotals();

    auto plan = syncwave::SyncController::computeSoftwareAlignmentPlan({ modelRealtek, modelBuds });

    TEST_ASSERT(plan.isValid, "Two-device alignment plan is valid");
    TEST_ASSERT(plan.syncState == syncwave::SyncState::PhysicallyCalibrated, "SyncState is PhysicallyCalibrated");
    TEST_ASSERT(std::abs(plan.targetLatencyMs - 275.0) < 1e-4, "Target timeline aligns to the slowest acoustic path (275.0 ms under Model B)");

    // CRITICAL DIRECTION CHECK:
    // Faster acoustic device (Realtek, arrival 107ms) MUST receive extra delay:
    // Delay = 275.0 - 107.0 = 168.0 ms!
    // Slower acoustic device (Buds, arrival 275ms) MUST receive ZERO extra delay:
    // Delay = 275.0 - 275.0 = 0.0 ms!
    TEST_ASSERT(std::abs(plan.calculatedDelaysMs[0] - 168.0) < 1e-4, 
                "Fast acoustic path (Realtek) receives positive delay to wait for slow acoustic path");
    TEST_ASSERT(plan.calculatedDelaysMs[1] == 0.0, 
                "Slow acoustic path (Buds) receives zero delay (emitted immediately)");
}

void testCalibrationStorePersistence() {
    std::cout << "[TEST] CalibrationStore: JSON Persistence & CRUD Operations (M11)\n";

    std::string testPath = "build/test_store_calibrations.json";
    std::filesystem::remove(testPath);

    syncwave::CalibrationStore store(testPath);

    syncwave::AcousticCalibrationRecord r1;
    r1.deviceId = "{0.0.0.00000000}.{realtek-test}";
    r1.deviceName = "Speakers (Realtek Audio)";
    r1.microphoneId = "{0.0.1.00000000}.{mic-test}";
    r1.microphoneName = "Microphone Array";
    r1.measuredLatencyMs = 24.35;
    r1.uncertaintyMs = 0.42;
    r1.confidence = 0.96f;
    r1.runCount = 5;
    r1.validRunCount = 5;
    r1.sampleRate = 48000;

    syncwave::AcousticCalibrationRecord r2;
    r2.deviceId = "{0.0.0.00000000}.{buds-test}";
    r2.deviceName = "Headphones (realme Buds T310)";
    r2.microphoneId = "{0.0.1.00000000}.{mic-test}";
    r2.microphoneName = "Microphone Array";
    r2.measuredLatencyMs = 185.20;
    r2.uncertaintyMs = 1.15;
    r2.confidence = 0.94f;
    r2.runCount = 5;
    r2.validRunCount = 5;
    r2.sampleRate = 44100;

    TEST_ASSERT(store.save(r1), "Saved Realtek calibration record");
    TEST_ASSERT(store.save(r2), "Saved Buds calibration record");

    auto list = store.list();
    TEST_ASSERT(list.size() == 2, "List contains exactly 2 records");

    // Reload from file to verify persistence
    syncwave::CalibrationStore store2(testPath);
    auto loadedR1 = store2.get(r1.deviceId);
    TEST_ASSERT(loadedR1.has_value(), "Found Realtek record after reload");
    TEST_ASSERT(std::abs(loadedR1->measuredLatencyMs - 24.35) < 1e-4, "Realtek latency matches exactly");
    TEST_ASSERT(std::abs(loadedR1->uncertaintyMs - 0.42) < 1e-4, "Realtek uncertainty matches");
    TEST_ASSERT(loadedR1->deviceName == r1.deviceName, "Device friendly name matches");

    auto loadedR2 = store2.get(r2.deviceId);
    TEST_ASSERT(loadedR2.has_value(), "Found Buds record after reload");
    TEST_ASSERT(std::abs(loadedR2->measuredLatencyMs - 185.20) < 1e-4, "Buds latency matches");

    // Remove one record
    TEST_ASSERT(store2.remove(r1.deviceId), "Removed Realtek record");
    TEST_ASSERT(store2.list().size() == 1, "Only 1 record remains after removal");
    TEST_ASSERT(!store2.get(r1.deviceId).has_value(), "Realtek record no longer found");

    // Clear
    store2.clear();
    TEST_ASSERT(store2.list().empty(), "Store empty after clear");

    std::filesystem::remove(testPath);
}

void testAcousticCalibrationIntegrationWithLatencyModel() {
    std::cout << "[TEST] OutputLatencyModel & SyncController Acoustic Integration (Model B)\n";

    // Device A (Realtek): SW latency = 80.0 ms, End-to-end acoustic arrival calibration = 107.40 ms
    syncwave::OutputLatencyModel modelA;
    modelA.deviceId = "dev-a";
    modelA.deviceName = "Realtek";
    modelA.sampleRate = 48000;
    modelA.wasapiPaddingMs = 20.0;
    modelA.queueLatencyMs = 60.0;
    modelA.optionalCalibrationOffsetMs = 107.40;
    modelA.updateTotals();

    TEST_ASSERT(modelA.estimatedSoftwareLatencyMs == 80.0, "Model A software latency is 80.0 ms");
    TEST_ASSERT(modelA.effectiveLatencyMs == 107.40, "Model A effective latency equals calibrated arrival offset (107.40 ms, no double-counting)");

    // Device B (Buds): SW latency = 80.0 ms, End-to-end acoustic arrival calibration = 131.68 ms
    syncwave::OutputLatencyModel modelB;
    modelB.deviceId = "dev-b";
    modelB.deviceName = "Buds";
    modelB.sampleRate = 48000;
    modelB.wasapiPaddingMs = 20.0;
    modelB.queueLatencyMs = 60.0;
    modelB.optionalCalibrationOffsetMs = 131.68;
    modelB.updateTotals();

    TEST_ASSERT(modelB.estimatedSoftwareLatencyMs == 80.0, "Model B software latency is 80.0 ms");
    TEST_ASSERT(modelB.effectiveLatencyMs == 131.68, "Model B effective latency equals calibrated arrival offset (131.68 ms, no double-counting)");

    // Compute alignment plan:
    // Target latency = max(107.40, 131.68) = 131.68 ms
    // Device A delay = 131.68 - 107.40 = 24.28 ms
    // Device B delay = 131.68 - 131.68 = 0.0 ms
    auto plan = syncwave::SyncController::computeSoftwareAlignmentPlan({ modelA, modelB });

    TEST_ASSERT(plan.isValid, "Alignment plan is valid");
    TEST_ASSERT(plan.syncState == syncwave::SyncState::PhysicallyCalibrated, 
                "SyncState is marked PhysicallyCalibrated");
    TEST_ASSERT(std::abs(plan.targetLatencyMs - 131.68) < 1e-4, "Target latency is 131.68 ms");
    TEST_ASSERT(std::abs(plan.calculatedDelaysMs[0] - 24.28) < 1e-4, 
                "Device A delay is compensated by exactly 24.28 ms to physically align with Buds");
    TEST_ASSERT(plan.calculatedDelaysFrames[0] == 1165, "Device A delay frames is 1165 @ 48kHz (round(24.28 * 48))");
    TEST_ASSERT(plan.calculatedDelaysMs[1] == 0.0, "Device B (slowest acoustic path) has 0.0 ms delay");
    TEST_ASSERT(plan.calculatedDelaysFrames[1] == 0, "Device B delay frames is 0");
}

void testModelBAccountingNoDoubleCounting() {
    std::cout << "[TEST] Milestone 11.2: Model B Accounting & Zero Double-Counting Audit\n";

    syncwave::OutputLatencyModel m;
    m.deviceId = "test-endpoint";
    m.deviceName = "Audited Endpoint";
    m.sampleRate = 48000;
    m.wasapiStreamLatencyMs = 10.0;
    m.wasapiPaddingMs = 22.0;
    m.queueLatencyMs = 0.0;
    m.resamplerLatencyMs = 0.0;

    // Case 1: Uncalibrated (no acoustic measurement)
    m.optionalCalibrationOffsetMs = 0.0;
    m.updateTotals();
    TEST_ASSERT(m.estimatedSoftwareLatencyMs == 32.0, "Uncalibrated: Est SW is 32.0 ms");
    TEST_ASSERT(m.effectiveLatencyMs == 32.0, "Uncalibrated: Effective latency defaults to software latency");

    // Case 2: Calibrated under Model B
    m.optionalCalibrationOffsetMs = 131.68;
    m.updateTotals();
    TEST_ASSERT(m.estimatedSoftwareLatencyMs == 32.0, "Calibrated: Est SW remains 32.0 ms");
    TEST_ASSERT(m.effectiveLatencyMs == 131.68, "Calibrated: Effective latency is exactly calibrated arrival (131.68 ms)");

    // Case 3: Transient queue spike (like the 258.67 ms initialization backlog)
    // Changing software buffer / queue depth must NOT alter the physical acoustic arrival reference!
    m.queueLatencyMs = 258.67;
    m.updateTotals();
    TEST_ASSERT(std::abs(m.estimatedSoftwareLatencyMs - 290.67) < 0.01, "Queue spike inflates Est SW to 290.67 ms");
    TEST_ASSERT(m.effectiveLatencyMs == 131.68, "CRITICAL: Effective latency is NOT inflated by queue spike (remains 131.68 ms)");

    // Case 4: Changing WASAPI padding must also not inflate effective latency when calibrated
    m.wasapiPaddingMs = 50.0;
    m.updateTotals();
    TEST_ASSERT(m.effectiveLatencyMs == 131.68, "CRITICAL: Effective latency is immune to WASAPI padding double-counting");
}

void testBudsRunAccountingAndMadCalculation() {
    std::cout << "[TEST] Milestone 11.2: Buds 5/7 Run Accounting & MAD Outlier Rejection Math\n";

    // Exact 7 runs recorded for OnePlus Buds:
    std::vector<syncwave::SingleRunMeasurement> runs(7);
    runs[0].runIndex = 1; runs[0].measuredLatencyMs = 140.48; runs[0].confidence = 0.90f; runs[0].isSuccess = true;
    runs[1].runIndex = 2; runs[1].measuredLatencyMs = 121.51; runs[1].confidence = 0.92f; runs[1].isSuccess = true;
    runs[2].runIndex = 3; runs[2].measuredLatencyMs = 136.29; runs[2].confidence = 0.94f; runs[2].isSuccess = true;
    runs[3].runIndex = 4; runs[3].measuredLatencyMs = 130.29; runs[3].confidence = 0.95f; runs[3].isSuccess = true;
    runs[4].runIndex = 5; runs[4].measuredLatencyMs = 133.06; runs[4].confidence = 0.96f; runs[4].isSuccess = true;
    runs[5].runIndex = 6; runs[5].measuredLatencyMs = -334.26; runs[5].confidence = 0.10f; // invalid timing
    runs[6].runIndex = 7; runs[6].measuredLatencyMs = 109.00; runs[6].confidence = 0.88f; runs[6].isSuccess = true;

    // Filter by plausibility window [0, 800 ms]
    std::vector<syncwave::SingleRunMeasurement> plausibleRuns;
    for (const auto& r : runs) {
        if (r.measuredLatencyMs >= 0.0 && r.measuredLatencyMs <= 800.0) {
            plausibleRuns.push_back(r);
        }
    }
    TEST_ASSERT(plausibleRuns.size() == 6, "Plausibility filter rejects Run 6 (-334.26 ms), leaving 6 runs");

    std::vector<double> latencies;
    for (const auto& m : plausibleRuns) latencies.push_back(m.measuredLatencyMs);
    std::sort(latencies.begin(), latencies.end()); // 109.00, 121.51, 130.29, 133.06, 136.29, 140.48
    double median = 0.5 * (latencies[2] + latencies[3]); // 131.675 ms
    TEST_ASSERT(std::abs(median - 131.675) < 1e-3, "Initial 6-run median is 131.675 ms");

    std::vector<double> devs;
    for (double v : latencies) devs.push_back(std::abs(v - median));
    std::sort(devs.begin(), devs.end());
    double mad = 0.5 * (devs[2] + devs[3]); // (4.615 + 8.805) / 2 = 6.71 ms
    TEST_ASSERT(std::abs(mad - 6.71) < 0.01, "MAD is exactly 6.71 ms");

    double threshold = std::max(3.0 * mad, 15.0); // max(20.13, 15.0) = 20.13 ms
    TEST_ASSERT(std::abs(threshold - 20.13) < 0.01, "Outlier threshold 3*MAD is 20.13 ms");

    // Check Run 7:
    double devRun7 = std::abs(109.00 - median); // 22.675 ms
    TEST_ASSERT(devRun7 > threshold, "Run 7 deviation (22.68 ms) > threshold (20.13 ms) -> REJECTED as statistical outlier");

    // 5 runs retained
    std::vector<double> retained = { 121.51, 130.29, 133.06, 136.29, 140.48 };
    double sum = 0.0;
    for (double v : retained) sum += v;
    double mean = sum / 5.0;
    TEST_ASSERT(std::abs(mean - 132.326) < 0.01, "Retained 5 runs mean is 132.33 ms");

    double sqSum = 0.0;
    for (double v : retained) sqSum += (v - mean) * (v - mean);
    double stdDev = std::sqrt(sqSum / 4.0);
    TEST_ASSERT(std::abs(stdDev - 7.14) < 0.02, "Retained 5 runs std dev is 7.14 ms (matches uncertainty +/- 7.14 ms)");
}

void testCalibrationStoreMetadataType() {
    std::cout << "[TEST] Milestone 11.2: CalibrationStore measurement_type & madMs Persistence\n";

    std::string testPath = "test_calibrations_meta.json";
    if (std::filesystem::exists(testPath)) {
        std::filesystem::remove(testPath);
    }

    syncwave::CalibrationStore store(testPath);

    syncwave::AcousticCalibrationRecord rec;
    rec.deviceId = "dev-1";
    rec.deviceName = "Realtek Speakers";
    rec.microphoneId = "mic-1";
    rec.microphoneName = "AMD Mic";
    rec.measurementType = "end_to_end_acoustic_arrival";
    rec.measuredLatencyMs = 107.40;
    rec.uncertaintyMs = 0.68;
    rec.madMs = 0.45;
    rec.confidence = 0.95f;
    rec.runCount = 7;
    rec.validRunCount = 5;

    bool saved = store.save(rec);
    TEST_ASSERT(saved, "Successfully saved record with measurement_type and madMs");

    syncwave::CalibrationStore storeReload(testPath);
    auto loaded = storeReload.get("dev-1");
    TEST_ASSERT(loaded.has_value(), "Successfully retrieved stored record");
    TEST_ASSERT(loaded->measurementType == "end_to_end_acoustic_arrival", "Loaded measurement_type is end_to_end_acoustic_arrival");
    TEST_ASSERT(std::abs(loaded->measuredLatencyMs - 107.40) < 1e-4, "Loaded measuredLatencyMs is 107.40 ms");
    TEST_ASSERT(std::abs(loaded->madMs - 0.45) < 1e-4, "Loaded madMs is 0.45 ms");

    std::filesystem::remove(testPath);
}

void testSoftwareHardwareBoundaryAppliedOnce() {
    std::cout << "[TEST] Milestone 11.2: Software-Hardware Boundary & Single Delay Allocation\n";

    // Setup OutputRouter with 2 outputs
    syncwave::AudioDevice devA; devA.id = "dev-a"; devA.name = "Realtek"; devA.isActive = true;
    syncwave::AudioDevice devB; devB.id = "dev-b"; devB.name = "Buds"; devB.isActive = true;

    syncwave::OutputRouter router;
    router.addOutput(devA);
    router.addOutput(devB);
    router.initializeOutputsForTesting(48000, 2);

    // Set calibrated arrival offsets
    router.setDeviceCalibrationOffsetMs(0, 107.40);
    router.setDeviceCalibrationOffsetMs(1, 131.68);

    auto models = router.getLatencyModels();
    TEST_ASSERT(models[0].optionalCalibrationOffsetMs == 107.40, "Device 0 has calibration offset 107.40 ms");
    TEST_ASSERT(models[0].effectiveLatencyMs == 107.40, "Device 0 effective latency is 107.40 ms under Model B");
    TEST_ASSERT(models[1].optionalCalibrationOffsetMs == 131.68, "Device 1 has calibration offset 131.68 ms");
    TEST_ASSERT(models[1].effectiveLatencyMs == 131.68, "Device 1 effective latency is 131.68 ms under Model B");

    auto plan = syncwave::SyncController::computeSoftwareAlignmentPlan(models);
    TEST_ASSERT(plan.isValid, "Plan is valid");
    TEST_ASSERT(std::abs(plan.targetLatencyMs - 131.68) < 1e-4, "Target latency is 131.68 ms");
    TEST_ASSERT(std::abs(plan.calculatedDelaysMs[0] - 24.28) < 1e-4, "Realtek calculated delay is 24.28 ms");
    TEST_ASSERT(plan.calculatedDelaysMs[1] == 0.0, "Buds calculated delay is 0.0 ms");

    router.applySyncPlan(plan);

    auto telem = router.getOutputTelemetry();
    TEST_ASSERT(std::abs(telem[0].configuredDelayMs - 24.28) < 0.1, "Realtek configuredDelayMs in DelayBuffer is 24.28 ms");
    TEST_ASSERT(telem[0].appliedDelayFrames == 1165, "Realtek appliedDelayFrames in DelayBuffer is exactly 1165 frames");
    TEST_ASSERT(telem[1].configuredDelayMs == 0.0, "Buds configuredDelayMs is 0.0 ms");
    TEST_ASSERT(telem[1].appliedDelayFrames == 0, "Buds appliedDelayFrames is 0 frames");

    // Verify delay was NOT applied to resampler or queue (no double application)
    TEST_ASSERT(telem[0].currentRateAdjustmentPpm == 0.0, "Resampler rate adjustment is 0 ppm (delay handled purely by DelayBuffer)");
    TEST_ASSERT(telem[1].currentRateAdjustmentPpm == 0.0, "Resampler rate adjustment is 0 ppm");

    router.closeOutputs();
}

void testSyntheticDriftMatrixM12() {
    std::cout << "[TEST] Synthetic Drift Matrix & Disturbance Recovery (M12)\n";

    syncwave::DriftControllerConfig config;
    config.deadbandMs = 1.0;
    config.maxAdjustmentPpm = 100.0;
    config.kp = 10.0;
    config.enableFeedforward = true;
    config.minObservationSec = 5.0;
    config.minSamples = 30;
    config.minConfidence = 0.90;

    syncwave::DriftController controller(config);
    controller.setEnabled(true);

    // Matrix of tested relative drifts
    std::vector<double> driftPpmCases = { 0.0, 10.0, 25.0, 50.0, -10.0, -25.0, -50.0 };

    for (double drift : driftPpmCases) {
        syncwave::SyncError err;
        err.isValid = true;
        err.confidence = 0.95;
        err.observationDurationSec = 10.0;
        err.filteredDriftPpm = drift;
        err.filteredPhaseErrorMs = 0.0; // zero phase error -> pure feedforward

        auto corr = controller.calculateCorrection(err);
        double expectedCorr = -drift; // feedforward cancels clock drift

        TEST_ASSERT(std::abs(corr.targetRateAdjustmentPpm - expectedCorr) < 1e-4,
                    ("Drift " + std::to_string(drift) + " ppm yields exact negative correction " + std::to_string(expectedCorr) + " ppm").c_str());
        TEST_ASSERT(std::abs(corr.targetRateAdjustmentPpm) <= 50.0,
                    "Correction does not saturate at ±100 ppm clamp");
    }

    // Temporary disturbance recovery test:
    // Output experiences large sudden disturbance: phase error +8.0 ms and drift +40.0 ppm
    syncwave::SyncError disturbance;
    disturbance.isValid = true;
    disturbance.confidence = 0.95;
    disturbance.observationDurationSec = 10.0;
    disturbance.filteredDriftPpm = 40.0;
    disturbance.filteredPhaseErrorMs = 8.0;

    auto corrDisturb = controller.calculateCorrection(disturbance);
    // feedforward = -40 ppm; proportional = (8.0 - 1.0) * 10 = +70 ppm; sum = +30 ppm
    TEST_ASSERT(corrDisturb.targetRateAdjustmentPpm == 30.0, "Disturbance response: feedforward (-40) + phase feedback (+70) = +30 ppm");
    TEST_ASSERT(corrDisturb.state == syncwave::DriftCorrectionState::Correcting, "State is Correcting");
    TEST_ASSERT(corrDisturb.targetRateAdjustmentPpm < 100.0, "Controller handles +8ms disturbance without hitting clamp");

    // Disturbance subsides: phase error returns to 0.5 ms (inside deadband), drift settles to 0.0 ppm
    syncwave::SyncError recovered;
    recovered.isValid = true;
    recovered.confidence = 0.95;
    recovered.observationDurationSec = 10.0;
    recovered.filteredDriftPpm = 0.0;
    recovered.filteredPhaseErrorMs = 0.5;

    auto corrRecovered = controller.calculateCorrection(recovered);
    TEST_ASSERT(corrRecovered.targetRateAdjustmentPpm == 0.0, "Recovered state returns to 0.0 ppm adjustment");
    TEST_ASSERT(corrRecovered.state == syncwave::DriftCorrectionState::Locked, "State returns to Locked");
}

void testDisconnectReconnectOutputSafetyM12() {
    std::cout << "[TEST] Disconnect & Reconnect Safety Lifecycle (M12)\n";

    syncwave::AudioDevice dev0; dev0.id = "dev-0"; dev0.name = "Realtek"; dev0.isActive = true;
    syncwave::AudioDevice dev1; dev1.id = "dev-1"; dev1.name = "Buds"; dev1.isActive = true;

    syncwave::OutputRouter router;
    router.addOutput(dev0);
    router.addOutput(dev1);
    TEST_ASSERT(router.initializeOutputsForTesting(48000, 2), "Router initialized for testing with 2 outputs");

    // Both outputs initially available
    TEST_ASSERT(router.getOutput(0)->isAvailable(), "Dev 0 available");
    TEST_ASSERT(router.getOutput(1)->isAvailable(), "Dev 1 available");

    // Route frames
    std::vector<float> frames(480 * 2, 0.5f);
    router.route(frames.data(), 480);
    TEST_ASSERT(router.getOutput(0)->queue()->availableToRead() == 480, "Dev 0 received 480 frames");
    TEST_ASSERT(router.getOutput(1)->queue()->availableToRead() == 480, "Dev 1 received 480 frames");

    // 1. Simulate disconnect of dev-1
    router.onDeviceDisconnected("dev-1");
    TEST_ASSERT(router.getOutput(0)->isAvailable(), "Dev 0 remains available after Dev 1 disconnect");
    TEST_ASSERT(!router.getOutput(1)->isAvailable(), "Dev 1 marked unavailable");
    TEST_ASSERT(router.getOutput(1)->driftCorrectionState() == syncwave::DriftCorrectionState::Disconnected,
                "Dev 1 drift controller marked Disconnected");

    // 2. Route frames while disconnected: dev-0 must receive audio without disturbance; dev-1 skipped
    router.route(frames.data(), 480);
    TEST_ASSERT(router.getOutput(0)->queue()->availableToRead() == 960, "Dev 0 safely received additional 480 frames");
    TEST_ASSERT(router.getOutput(1)->queue()->availableToRead() == 480, "Dev 1 queue unperturbed (received 0 new frames)");

    // 3. Simulate reconnect of dev-1
    // Reinitialize dev-1 queue for testing
    router.getOutput(1)->initializeForTesting(48000, 2);
    TEST_ASSERT(router.getOutput(1)->isAvailable(), "Dev 1 isAvailable restored upon reinitialization");

    // 4. Route frames after reconnect: both outputs receive audio on master timeline
    router.route(frames.data(), 480);
    TEST_ASSERT(router.getOutput(0)->queue()->availableToRead() == 1440, "Dev 0 received 1440 cumulative frames");
    TEST_ASSERT(router.getOutput(1)->queue()->availableToRead() == 480, "Dev 1 seamlessly rejoins master timeline");

    router.closeOutputs();
}

void testQueueStressAndBoundedMemoryM12() {
    std::cout << "[TEST] Output Queue Stress & Memory Bound Invariance (M12)\n";

    syncwave::AudioDevice dev; dev.id = "dev-stress"; dev.name = "StressDevice"; dev.isActive = true;
    syncwave::DeviceOutput out(dev);
    TEST_ASSERT(out.initializeForTesting(48000, 2), "Initialized testing DeviceOutput");

    // RingBuffer capacity is 48,000 frames (1 second)
    TEST_ASSERT(out.queue()->capacityFrames() == 48000, "Queue capacity is exactly 48,000 frames");

    // Push 40,000 frames: fits within capacity
    std::vector<float> bulk(40000 * 2, 0.1f);
    size_t w1 = out.push(bulk.data(), 40000);
    TEST_ASSERT(w1 == 40000, "First push of 40,000 frames completely accepted");
    TEST_ASSERT(out.queue()->availableToRead() == 40000, "Available to read is 40,000");

    // Push another 20,000 frames: only 8,000 should fit, remaining 12,000 should register as overrun
    std::vector<float> overflow(20000 * 2, 0.2f);
    size_t w2 = out.push(overflow.data(), 20000);
    TEST_ASSERT(w2 == 8000, "Second push writes exactly 8,000 frames to fill capacity");
    TEST_ASSERT(out.queue()->availableToRead() == 48000, "Queue is full at 48,000 frames");

    auto telem = out.getTelemetry();
    TEST_ASSERT(telem.queueOverruns == 12000, "Queue overruns strictly counted (12,000 dropped frames)");

    // Drain 24,000 frames (simulating consumer playback)
    std::vector<float> drain(24000 * 2);
    size_t read = out.queue()->read(drain.data(), 24000);
    TEST_ASSERT(read == 24000, "Consumer drained 24,000 frames");
    TEST_ASSERT(out.queue()->availableToRead() == 24000, "Available to read is now 24,000 frames");

    // Push 20,000 frames again: now fits cleanly without increasing overruns
    size_t w3 = out.push(overflow.data(), 20000);
    TEST_ASSERT(w3 == 20000, "Push into drained space accepted without overrun");
    TEST_ASSERT(out.getTelemetry().queueOverruns == 12000, "Overrun count did not increase");

    out.close();
}

// ============================================================================
// Milestone 13: Audio/Video Synchronization Architecture Tests
// ============================================================================

void testTimelineModelConversionsM13() {
    std::cout << "[TEST] TimelineModel: Media PTS to Master Frame & Reverse Conversions (M13)\n";

    syncwave::TimelineModel model(48000);

    // Initial state: media 0 us -> master frame 0
    TEST_ASSERT(model.sampleRate() == 48000, "Default sample rate is 48000 Hz");
    TEST_ASSERT(!model.hasAnchor(), "Initially model has no anchor");
    TEST_ASSERT(model.mediaUsToMasterFrame(0) == 0, "Media 0 us maps to master frame 0");
    TEST_ASSERT(model.masterFrameToMediaUs(0) == 0, "Master frame 0 maps to media 0 us");

    // 1.0 second conversion: 1,000,000 us -> 48,000 frames
    TEST_ASSERT(model.mediaUsToMasterFrame(1000000) == 48000, "Media 1.0s maps to 48,000 frames");
    TEST_ASSERT(model.masterFrameToMediaUs(48000) == 1000000, "Master frame 48,000 maps to 1.0s (1,000,000 us)");
    TEST_ASSERT(std::abs(model.masterFrameToMediaMs(48000) - 1000.0) < 0.001, "Master frame 48,000 maps to 1000.0 ms");
    TEST_ASSERT(model.mediaMsToMasterFrame(1000.0) == 48000, "Media 1000.0 ms maps to 48,000 frames");

    // Fractional conversion: 23,219 us @ 48kHz = round(23219 * 48 / 1000) = round(1114.512) = 1115
    uint64_t f_frac = model.mediaUsToMasterFrame(23219);
    TEST_ASSERT(f_frac == 1115, "Fractional media PTS 23,219 us maps to frame 1115");

    // Reversible identity test across arbitrary timestamps
    for (int64_t testUs : { 50000, 250000, 1500000, 12500000 }) {
        uint64_t frame = model.mediaUsToMasterFrame(testUs);
        int64_t recoveredUs = model.masterFrameToMediaUs(frame);
        int64_t errUs = std::abs(recoveredUs - testUs);
        // Error should be within single-sample quantization window (1/48000 s ~= 20.8 us)
        TEST_ASSERT(errUs <= 21, "Roundtrip conversion error <= single-sample quantization");
    }

    // Sample rate adaptation (e.g. 44.1 kHz)
    model.setSampleRate(44100);
    TEST_ASSERT(model.mediaUsToMasterFrame(1000000) == 44100, "Media 1.0s maps to 44,100 frames at 44.1kHz");
    TEST_ASSERT(model.masterFrameToMediaUs(44100) == 1000000, "Master frame 44,100 maps to 1.0s at 44.1kHz");
}

void testTimelineModelSeekAndDiscontinuityM13() {
    std::cout << "[TEST] TimelineModel: Seek, Anchor Re-Association, and Discontinuity (M13)\n";

    syncwave::TimelineModel model(48000);

    // Playback ran for 3 seconds (144,000 frames)
    // Then a seek forward occurred to media position 10.0 seconds (10,000,000 us)
    model.onSeek(10000000, 144000);
    TEST_ASSERT(model.hasAnchor(), "Model is now anchored");
    TEST_ASSERT(model.anchorMediaUs() == 10000000, "Anchor media time is 10,000,000 us");
    TEST_ASSERT(model.anchorMasterFrame() == 144000, "Anchor master frame is 144,000");

    // Immediately at the seek point:
    TEST_ASSERT(model.mediaUsToMasterFrame(10000000) == 144000, "Seek target 10.0s maps directly to current master frame 144,000");
    TEST_ASSERT(model.masterFrameToMediaUs(144000) == 10000000, "Master frame 144,000 maps to 10.0s");

    // Advancing 1.0s after seek (to 11.0s media, 192,000 frames):
    TEST_ASSERT(model.mediaUsToMasterFrame(11000000) == 192000, "Media 11.0s maps to master frame 192,000");
    TEST_ASSERT(model.masterFrameToMediaUs(192000) == 11000000, "Master frame 192,000 maps to 11.0s");

    // Seek backward: Jump from 11.0s back to 2.0s (2,000,000 us) at current master frame 192,000
    model.onSeek(2000000, 192000);
    TEST_ASSERT(model.anchorMediaUs() == 2000000, "Anchor updated to 2,000,000 us");
    TEST_ASSERT(model.anchorMasterFrame() == 192000, "Anchor master frame updated to 192,000");
    TEST_ASSERT(model.mediaUsToMasterFrame(2000000) == 192000, "Seek backward target maps to master frame 192,000");
    TEST_ASSERT(model.masterFrameToMediaUs(192000) == 2000000, "Master frame 192,000 maps back to 2.0s");

    // Advancing 0.5s after seek backward:
    TEST_ASSERT(model.mediaUsToMasterFrame(2500000) == 192000 + 24000, "Media 2.5s maps to 216,000 frames");

    // Rapid successive seeks (stressing anchor state consistency)
    for (int64_t targetSec = 1; targetSec <= 10; ++targetSec) {
        model.onSeek(targetSec * 1000000, 200000 + targetSec * 1000);
        TEST_ASSERT(model.anchorMediaUs() == targetSec * 1000000, "Rapid seek anchor updated");
    }

    // Reset clears anchor
    model.reset();
    TEST_ASSERT(!model.hasAnchor(), "Reset successfully cleared anchor");
}

void testAvOffsetSignConventionM13() {
    std::cout << "[TEST] A/V Offset Sign Convention & Mathematical Definition (M13)\n";

    // Standard definition: offset = audioTimeMs - videoTimeMs
    // Case 1: Audio is at 500 ms, Video is at 450 ms
    // Audio is ahead by 50 ms -> positive offset
    double offset1 = syncwave::TimelineModel::calculateAvOffsetMs(500.0, 450.0);
    TEST_ASSERT(offset1 == +50.0, "Positive offset (+50 ms) indicates Audio LEADS Video");

    // Case 2: Audio is at 400 ms, Video is at 480 ms
    // Audio is behind by 80 ms -> negative offset
    double offset2 = syncwave::TimelineModel::calculateAvOffsetMs(400.0, 480.0);
    TEST_ASSERT(offset2 == -80.0, "Negative offset (-80 ms) indicates Audio LAGS Video");

    // Case 3: Perfectly synchronized
    double offset3 = syncwave::TimelineModel::calculateAvOffsetMs(1000.0, 1000.0);
    TEST_ASSERT(offset3 == 0.0, "Zero offset indicates perfect A/V lock");
}

void testDeterministicMediaSourceM13() {
    std::cout << "[TEST] DeterministicMediaSource: Frame Stepping, Markers, and Seek (M13)\n";

    syncwave::DeterministicMediaSource source(48000, 30, 5.0); // 5 seconds @ 30 fps
    TEST_ASSERT(source.sampleRate() == 48000, "Sample rate is 48000");
    TEST_ASSERT(source.fps() == 30, "FPS is 30");
    TEST_ASSERT(source.durationSec() == 5.0, "Duration is 5.0s");

    // Read first block (frame 0)
    auto b0 = source.readNextBlock();
    TEST_ASSERT(b0.videoFrameNumber == 0, "First block video frame index is 0");
    TEST_ASSERT(b0.mediaTimestampUs == 0, "First block media timestamp is 0 us");
    TEST_ASSERT(b0.audioFrameCount == 1600, "Audio frames per 30fps block @ 48kHz is 1600 (48000/30)");
    TEST_ASSERT(b0.audioPcm.size() == 3200, "Stereo buffer has 3200 floats");
    TEST_ASSERT(!b0.hasAudioMarker, "Frame 0 does not cross 1.0s boundary marker");

    // Step forward 29 blocks to reach frame 30 (1.0 second boundary)
    bool markerDetected = false;
    for (int i = 1; i < 31; ++i) {
        auto b = source.readNextBlock();
        if (b.hasAudioMarker) {
            markerDetected = true;
            TEST_ASSERT(b.videoFrameNumber >= 29 && b.videoFrameNumber <= 31, "Marker beep occurs at ~1.0s (frame 30)");
        }
    }
    TEST_ASSERT(markerDetected, "1.0s periodic audio/video synchronization marker successfully generated");

    // Seek forward to 3.0 seconds
    source.seekMs(3000.0);
    TEST_ASSERT(source.currentMediaMs() == 3000.0, "Current media time after seek is 3000.0 ms");
    auto bSeek = source.readNextBlock();
    TEST_ASSERT(bSeek.mediaTimestampUs == 3000000, "Block after seek starts at 3,000,000 us");
    TEST_ASSERT(bSeek.videoFrameNumber == 90, "Video frame at 3.0s is frame 90 (30 * 3)");

    // Seek backward to 0.5 seconds
    source.seekMs(500.0);
    TEST_ASSERT(source.currentMediaMs() == 500.0, "Current media time after seek backward is 500.0 ms");
    auto bBack = source.readNextBlock();
    TEST_ASSERT(bBack.mediaTimestampUs == 500000, "Block after seek starts at 500,000 us");
    TEST_ASSERT(bBack.videoFrameNumber == 15, "Video frame at 0.5s is frame 15 (30 * 0.5)");
}

void testMediaSourceMasterBusBridgeM13() {
    std::cout << "[TEST] Audio Bridge: Ingesting Media Audio Chunks into MasterAudioBus (M13)\n";

    syncwave::MasterAudioBus bus(syncwave::MasterAudioBus::canonicalFormat(), 48000);
    syncwave::DeterministicMediaSource source(48000, 30, 5.0);
    syncwave::TimelineModel timeline(48000);

    // Push 10 media blocks (16,000 frames, 1/3 second) into MasterAudioBus
    for (int i = 0; i < 10; ++i) {
        auto block = source.readNextBlock();
        size_t written = bus.write(block.audioPcm.data(), block.audioFrameCount);
        TEST_ASSERT(written == block.audioFrameCount, "Wrote full audio block into MasterAudioBus");
    }

    TEST_ASSERT(bus.availableFrames() == 16000, "Available frames in bus is exactly 16,000");
    TEST_ASSERT(bus.totalFramesWritten() == 16000, "Total frames written is 16,000");

    // Timeline mapping verification
    double mediaTimeMs = timeline.masterFrameToMediaMs(bus.totalFramesWritten());
    double expectedTimeMs = (16000.0 * 1000.0) / 48000.0;
    TEST_ASSERT(std::abs(mediaTimeMs - expectedTimeMs) < 0.1, "Master frame count accurately reflects media elapsed time (333.3 ms)");

    // Simulate seeking: Flush MasterAudioBus unread frames and re-anchor timeline
    bus.flush();
    timeline.onSeek(2000000, bus.totalFramesWritten());
    source.seekMs(2000.0);

    TEST_ASSERT(bus.availableFrames() == 0, "Bus is empty after seek flush");
    TEST_ASSERT(timeline.hasAnchor(), "Timeline is anchored to 2.0s");

    // Ingest 5 blocks from new seek position
    for (int i = 0; i < 5; ++i) {
        auto block = source.readNextBlock();
        bus.write(block.audioPcm.data(), block.audioFrameCount);
    }

    TEST_ASSERT(bus.availableFrames() == 8000, "Bus has 8,000 frames from post-seek ingest");
    double postSeekTimeMs = timeline.masterFrameToMediaMs(bus.totalFramesWritten());
    // 2000 ms + (8000 / 48000 * 1000) = 2166.67 ms
    TEST_ASSERT(std::abs(postSeekTimeMs - 2166.67) < 0.5, "Post-seek timeline accurately tracks media position without stale audio");
}

void testVlcMediaEngineLifecycleAndControlsM13() {
    std::cout << "[TEST] VlcMediaEngine: LibVLC Lifecycle, File Load, and State Query (M13)\n";

    syncwave::MasterAudioBus bus(syncwave::MasterAudioBus::canonicalFormat(), 48000);
    syncwave::OutputRouter router;

    syncwave::VlcMediaEngine engine(bus, router);

    // Verify initial state
    TEST_ASSERT(!engine.isPlaying(), "Initially not playing");
    TEST_ASSERT(!engine.isPaused(), "Initially not paused");
    TEST_ASSERT(engine.stateString() == "Stopped", "Initial state string is Stopped");

    // Load generated test MP4
    bool loaded = engine.load("test_av.mp4");
    TEST_ASSERT(loaded, "Successfully loaded test_av.mp4 into VlcMediaEngine");

    // Verify initial playback controls without crash
    bool started = engine.play();
    TEST_ASSERT(started, "Play command returned success");

    // Query sync state
    auto syncState = engine.getSyncState();
    TEST_ASSERT(syncState.playbackRate == 1.0, "Playback rate is 1.0");

    // Playback rate adjustment
    engine.setPlaybackRate(1.05f);
    TEST_ASSERT(std::abs(engine.playbackRate() - 1.05f) < 0.01f, "Playback rate updated to 1.05");

    // Pause / Resume
    engine.pause();
    engine.resume();

    // Seek
    bool seekOk = engine.seek(2000.0);
    TEST_ASSERT(seekOk, "Seek to 2000.0 ms returned true");

    // Stop and cleanup
    engine.stop();
    TEST_ASSERT(!engine.isPlaying(), "Engine stopped");
}

void testSyncWavePublicEngineApiM14() {
    std::cout << "[TEST] Public Engine API & Diagnostics Model (M14)\n";

    auto engine = syncwave::createSyncWaveEngine();
    TEST_ASSERT(engine != nullptr, "Engine instance successfully created");
    TEST_ASSERT(!engine->isRunning(), "Engine initially not running");

    // Device enumeration via public API
    auto renderDevs = engine->enumerateOutputDevices(false);
    TEST_ASSERT(!renderDevs.empty(), "Public API enumerates output devices");

    auto capDevs = engine->enumerateCaptureDevices(false);
    TEST_ASSERT(!capDevs.empty(), "Public API enumerates capture devices");

    // Diagnostics snapshot query
    auto diag = engine->getDiagnostics();
    TEST_ASSERT(!diag.isEngineRunning, "Snapshot correctly reflects stopped state");
    TEST_ASSERT(diag.masterSampleRate == 48000, "Snapshot reports master 48000 Hz");
    TEST_ASSERT(diag.masterChannels == 2, "Snapshot reports stereo master bus");
    TEST_ASSERT(diag.system.uptimeSec >= 0.0, "Snapshot reports non-negative uptime");
    TEST_ASSERT(diag.system.memoryUsageMb > 0.0, "Snapshot reports positive memory usage");

    // Mode configuration
    engine->setSyncMode("software");
    engine->setSyncMode("adaptive");

    // Graceful stop on already stopped engine
    engine->stop();
    TEST_ASSERT(!engine->isRunning(), "Multiple stops are safe and idempotent");
}

int main() {
    std::cout << "======================================\n";
    std::cout << "      SyncWave Test Suite (Phase 11)  \n";
    std::cout << "======================================\n";

    testStringConversions();
    testDeviceDataModel();
    testEventModel();
    testNotificationClientComLifecycle();
    testDeviceManagerMonitoringLifecycle();
    testDeviceManagerEnumerationIntegration();
    testAudioFormat();
    testToneGenerator();
    testWasapiOutputIntegration();
    testRingBufferBasics();
    testRingBufferDataIntegrity();
    testRingBufferConcurrencyStress();
    testMasterAudioBusIntegration();
    testResampler();
    testWasapiCaptureIntegration();
    testAudioEngineCaptureIntegration();
    testDeviceOutputModel();
    testOutputRouterLifecycle();
    testOutputRouterFanOut();
    testOutputRouterIndependentQueues();
    testOutputRouterQueueOverflow();
    testOutputRouterMultiSampleRateResampling();
    testOutputRouterDisconnectHandling();
    testOutputRouterCleanShutdown();
    testAudioEngineMultiOutputIntegration();
    testDeviceClockBasics();
    testDeviceClockLinearRegression();
    testDeviceClockInsufficientSamples();
    testDriftEstimatorCalculations();
    testDriftEstimatorPairwise();
    testDriftEstimatorRobustness();
    testWasapiClockSnapshotIntegration();
    testOutputRouterClockTelemetry();

    // Milestone 8 Tests
    testDeviceClockUnitsAndConversions();
    testDeviceClockMultiWindowEstimation();
    testDriftAndOffsetSeparation();
    testDriftEstimatorWindowedAndConfidence();
    testDeviceOutputPlayheadEstimation();
    testSyncPulseGenerator();
    testOutputRouterMultiWindowPairwise();

    // Milestone 9 Tests
    testDelayBufferBasics();
    testDelayBufferIntegrityAndWraparound();
    testOutputLatencyModel();
    testSyncControllerAlignmentMath();
    testDeviceOutputDelayBufferIntegration();
    testOutputRouterSyncPlanIntegration();

    // Milestone 10 Tests
    testResamplerRateAdjustmentAndSlewing();
    testFilteredDriftEstimatorOutlierRejection();
    testDriftControllerDeadbandAndClamping();
    testDriftControllerStateTransitions();
    testSyntheticMultiClockDriftSimulation();
    testDeviceOutputAndRouterDriftIntegration();

    // Milestone 10.1 Audit Tests
    testDriftControllerPhysicalDirection();
    testResamplerPullModeMathematicalVerification();
    testSeparateFrequencyAndPhaseCorrection();
    testSyntheticControllerValidationCasesAtoH();

    // Milestone 11 & 11.1 Tests
    testChirpGeneratorBasicsAndTukeyWindow();
    testCorrelationDetectorExactPeak();
    testCorrelationDetectorKnownSyntheticDelays();
    testCorrelationDetectorNoisySignal();
    testCorrelationDetectorMultiEcho();
    testCorrelationDetectorFailedCorrelation();
    testSampleRateDomainConversion();
    testMultiRunStatisticsAndOutlierRejection();
    testAcousticCalibrationStricterAcceptanceCriteria();
    testCorrelationDetectorMultiPeakAndSecondaryReflection();
    testAcousticCalibrationDirectionAndTwoDeviceAlignment();
    testCalibrationStorePersistence();
    testAcousticCalibrationIntegrationWithLatencyModel();

    // Milestone 11.2 Accounting Audit Tests
    testModelBAccountingNoDoubleCounting();
    testBudsRunAccountingAndMadCalculation();
    testCalibrationStoreMetadataType();
    testSoftwareHardwareBoundaryAppliedOnce();

    // Milestone 12 Long-Duration Stress Tests
    testSyntheticDriftMatrixM12();
    testDisconnectReconnectOutputSafetyM12();
    testQueueStressAndBoundedMemoryM12();

    // Milestone 13 Audio/Video Synchronization Tests
    testTimelineModelConversionsM13();
    testTimelineModelSeekAndDiscontinuityM13();
    testAvOffsetSignConventionM13();
    testDeterministicMediaSourceM13();
    testMediaSourceMasterBusBridgeM13();
    testVlcMediaEngineLifecycleAndControlsM13();

    // Milestone 14 Public Engine & Diagnostics Model Tests
    testSyncWavePublicEngineApiM14();

    std::cout << "======================================\n";
    std::cout << "Summary: " << g_testsPassed << " passed, " << g_testsFailed << " failed.\n";
    std::cout << "======================================\n";

    return (g_testsFailed == 0) ? 0 : 1;
}
