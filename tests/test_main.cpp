#include "../src/windows/ComHelper.h"
#include "../src/windows/DeviceManager.h"
#include "../src/windows/DeviceNotification.h"
#include "../src/windows/WasapiOutput.h"
#include "../src/audio/AudioFormat.h"
#include "../src/audio/ToneGenerator.h"
#include "../src/audio/RingBuffer.h"
#include "../src/audio/MasterAudioBus.h"
#include "../src/audio/AudioEngine.h"
#include "../src/dsp/Resampler.h"
#include "../src/windows/WasapiCapture.h"
#include "../src/app/CommandInterface.h"

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

int main() {
    std::cout << "======================================\n";
    std::cout << "      SyncWave Test Suite (Phase 5)   \n";
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

    std::cout << "======================================\n";
    std::cout << "Summary: " << g_testsPassed << " passed, " << g_testsFailed << " failed.\n";
    std::cout << "======================================\n";

    return (g_testsFailed == 0) ? 0 : 1;
}
