#include "../src/windows/ComHelper.h"
#include "../src/windows/DeviceManager.h"
#include "../src/app/CommandInterface.h"

#include <iostream>
#include <cassert>
#include <string>
#include <vector>

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

            // Look up by ID
            auto queried = dm.getDeviceById(defaultDev->id);
            TEST_ASSERT(queried.has_value(), "getDeviceById successfully retrieves default device");
            if (queried) {
                TEST_ASSERT(queried->name == defaultDev->name, "Retrieved device name matches default device name");
            }
        } else {
            std::cout << "    (No default audio render device configured on host)\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Integration test error: " << e.what() << "\n";
        TEST_ASSERT(false, "DeviceManager initialization or enumeration threw exception");
    }
}

int main() {
    std::cout << "======================================\n";
    std::cout << "      SyncWave Test Suite (Phase 1)   \n";
    std::cout << "======================================\n";

    testStringConversions();
    testDeviceDataModel();
    testDeviceManagerEnumerationIntegration();

    std::cout << "======================================\n";
    std::cout << "Summary: " << g_testsPassed << " passed, " << g_testsFailed << " failed.\n";
    std::cout << "======================================\n";

    return (g_testsFailed == 0) ? 0 : 1;
}
