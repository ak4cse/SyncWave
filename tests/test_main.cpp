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

#include <iostream>
#include <cassert>
#include <string>
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

    // Apply manual calibration offset (e.g. +50ms physical acoustic latency)
    model.optionalCalibrationOffsetMs = 50.0;
    model.recalculate();
    TEST_ASSERT(std::abs(model.effectiveLatencyMs - 67.5) < 0.001, 
                "effectiveLatencyMs incorporates calibration offset (67.5 ms)");

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

    // Physical calibration offset test:
    // Suppose Fast Dev has 100 ms physical Bluetooth acoustic delay
    models[0].optionalCalibrationOffsetMs = 100.0;
    models[0].updateTotals(); // effective = 120.0 ms
    auto physPlan = syncwave::SyncController::computeSoftwareAlignmentPlan(models);
    TEST_ASSERT(physPlan.syncState == syncwave::SyncState::PhysicallyCalibrated, "State is PhysicallyCalibrated when offsets present");
    TEST_ASSERT(std::abs(physPlan.targetLatencyMs - 120.0) < 0.001, "Target latency is 120.0 ms");
    TEST_ASSERT(std::abs(physPlan.calculatedDelaysMs[0] - 0.0) < 0.001, "Dev0 now gets 0 delay (slowest acoustic path)");
    TEST_ASSERT(std::abs(physPlan.calculatedDelaysMs[1] - 60.0) < 0.001, "Dev1 gets 60 ms delay (120 - 60)");
    // Dev1 @ 44.1kHz: 60ms = 2646 frames
    TEST_ASSERT(physPlan.calculatedDelaysFrames[1] == 2646, "Dev1 60ms @ 44.1kHz is 2646 frames");

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

int main() {
    std::cout << "======================================\n";
    std::cout << "      SyncWave Test Suite (Phase 9)   \n";
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

    std::cout << "======================================\n";
    std::cout << "Summary: " << g_testsPassed << " passed, " << g_testsFailed << " failed.\n";
    std::cout << "======================================\n";

    return (g_testsFailed == 0) ? 0 : 1;
}
