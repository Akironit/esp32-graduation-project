// TemperatureSensors.cpp
#include "TemperatureSensors.h"

#include "Logger.h"

namespace {
constexpr const char* TAG_TEMP = "TEMP";
constexpr const char* TEMP_NVS_NAMESPACE = "temps";
}

void TemperatureSensors::begin(uint8_t pin) {
    oneWire = new OneWire(pin);
    sensors = new DallasTemperature(oneWire);

    sensors->begin();
    sensors->setWaitForConversion(false);

    loadRegistry();
    scanBus();
    requestTemperatureConversion();

    Logger::info(TAG_TEMP, "DS18B20 initialized");
    Logger::infof(TAG_TEMP, "Known sensors: %u", entryCount);
}

void TemperatureSensors::update() {
    if (sensors == nullptr) {
        return;
    }

    const unsigned long now = millis();

    if (conversionPending) {
        if (now - conversionStartMs >= conversionDelayMs) {
            readTemperatures();
        }

        return;
    }

    if (rescanRequested || now - lastRescanMs >= DS18B20_RESCAN_INTERVAL_MS) {
        rescanRequested = false;
        lastRescanMs = now;
        scanBus();
    }

    if (now - lastUpdateMs >= updateIntervalMs) {
        lastUpdateMs = now;
        requestTemperatureConversion();
    }
}

void TemperatureSensors::forceRead() {
    if (sensors == nullptr) {
        return;
    }

    if (conversionPending) {
        return;
    }

    requestTemperatureConversion();
}

void TemperatureSensors::rescan() {
    if (sensors == nullptr) {
        return;
    }

    sensors->begin();
    sensors->setWaitForConversion(false);
    if (conversionPending) {
        rescanRequested = true;
        return;
    }

    scanBus();
    requestTemperatureConversion();
}

uint8_t TemperatureSensors::getSensorCount() const {
    return entryCount;
}

float TemperatureSensors::getTemperatureC(uint8_t index) const {
    if (index >= entryCount || !entries[index].connected || !entries[index].enabled || !entries[index].hasTemperature) {
        return DEVICE_DISCONNECTED_C;
    }

    return entries[index].temperatureC;
}

bool TemperatureSensors::getTemperatureByRole(TempSensorRole role, float& temperatureC) const {
    for (uint8_t i = 0; i < entryCount; i++) {
        if (entries[i].role == role && entries[i].enabled && entries[i].connected && entries[i].hasTemperature) {
            temperatureC = entries[i].temperatureC;
            return true;
        }
    }

    temperatureC = DEVICE_DISCONNECTED_C;
    return false;
}

uint8_t TemperatureSensors::getEntryCount() const {
    return entryCount;
}

const TempSensorEntry* TemperatureSensors::getEntry(uint8_t index) const {
    if (index >= entryCount) {
        return nullptr;
    }

    return &entries[index];
}

bool TemperatureSensors::assignRole(uint8_t index, TempSensorRole role) {
    if (index >= entryCount) {
        return false;
    }

    if (role == TempSensorRole::Indoor || role == TempSensorRole::Outdoor) {
        clearRole(role, index);
        entries[index].enabled = true;
    } else if (role == TempSensorRole::Unused) {
        entries[index].enabled = false;
        entries[index].hasTemperature = false;
    } else {
        entries[index].enabled = true;
    }

    entries[index].role = role;
    saveRegistry();
    Logger::infof(TAG_TEMP, "Sensor %u assigned role=%s", index + 1, roleName(role));
    return true;
}

bool TemperatureSensors::forget(uint8_t index) {
    if (index >= entryCount) {
        return false;
    }

    for (uint8_t i = index; i + 1 < entryCount; i++) {
        entries[i] = entries[i + 1];
    }

    entryCount--;
    saveRegistry();
    Logger::infof(TAG_TEMP, "Sensor %u forgotten", index + 1);
    return true;
}

bool TemperatureSensors::swapRoles() {
    int indoorIndex = -1;
    int outdoorIndex = -1;

    for (uint8_t i = 0; i < entryCount; i++) {
        if (entries[i].role == TempSensorRole::Indoor) {
            indoorIndex = i;
        } else if (entries[i].role == TempSensorRole::Outdoor) {
            outdoorIndex = i;
        }
    }

    if (indoorIndex < 0 || outdoorIndex < 0) {
        return false;
    }

    entries[indoorIndex].role = TempSensorRole::Outdoor;
    entries[outdoorIndex].role = TempSensorRole::Indoor;
    saveRegistry();
    Logger::info(TAG_TEMP, "Indoor and outdoor sensor roles swapped");
    return true;
}

int TemperatureSensors::findEntryBySelector(const String& selector) const {
    String trimmed = selector;
    trimmed.trim();

    if (trimmed.length() == 0) {
        return -1;
    }

    bool numeric = true;
    for (uint16_t i = 0; i < trimmed.length(); i++) {
        if (!isDigit(trimmed[i])) {
            numeric = false;
            break;
        }
    }

    if (numeric) {
        const int displayIndex = trimmed.toInt();
        if (displayIndex >= 1 && displayIndex <= entryCount) {
            return displayIndex - 1;
        }
        return -1;
    }

    DeviceAddress address;
    if (!parseAddress(trimmed, address)) {
        return -1;
    }

    return findEntryByAddress(address);
}

const char* TemperatureSensors::roleName(TempSensorRole role) const {
    switch (role) {
        case TempSensorRole::Indoor:
            return "Indoor";
        case TempSensorRole::Outdoor:
            return "Outdoor";
        case TempSensorRole::Unused:
            return "Unused";
        case TempSensorRole::Unknown:
        default:
            return "Unknown";
    }
}

void TemperatureSensors::loadRegistry() {
    entryCount = 0;

    if (!preferences.begin(TEMP_NVS_NAMESPACE, true)) {
        Logger::warning(TAG_TEMP, "Failed to open DS18B20 registry for reading");
        return;
    }

    const uint8_t savedCount = min(preferences.getUChar("count", 0), (uint8_t)TEMP_MAX_SENSORS);
    for (uint8_t i = 0; i < savedCount; i++) {
        char key[16];
        snprintf(key, sizeof(key), "addr%u", i);
        if (preferences.getBytesLength(key) != 8) {
            continue;
        }

        TempSensorEntry entry;
        preferences.getBytes(key, entry.address, 8);

        snprintf(key, sizeof(key), "role%u", i);
        const uint8_t roleValue = preferences.getUChar(key, static_cast<uint8_t>(TempSensorRole::Unknown));
        entry.role = roleValue <= static_cast<uint8_t>(TempSensorRole::Unused)
            ? static_cast<TempSensorRole>(roleValue)
            : TempSensorRole::Unknown;

        snprintf(key, sizeof(key), "en%u", i);
        entry.enabled = preferences.getBool(key, true);

        entries[entryCount++] = entry;
    }

    preferences.end();
    Logger::infof(TAG_TEMP, "Loaded DS18B20 registry: %u sensors", entryCount);
}

void TemperatureSensors::saveRegistry() {
    if (!preferences.begin(TEMP_NVS_NAMESPACE, false)) {
        Logger::warning(TAG_TEMP, "Failed to open DS18B20 registry for writing");
        return;
    }

    preferences.putUChar("count", entryCount);
    for (uint8_t i = 0; i < entryCount; i++) {
        char key[16];
        snprintf(key, sizeof(key), "addr%u", i);
        preferences.putBytes(key, entries[i].address, 8);

        snprintf(key, sizeof(key), "role%u", i);
        preferences.putUChar(key, static_cast<uint8_t>(entries[i].role));

        snprintf(key, sizeof(key), "en%u", i);
        preferences.putBool(key, entries[i].enabled);
    }

    preferences.end();
}

void TemperatureSensors::scanBus() {
    if (sensors == nullptr) {
        return;
    }

    bool seen[TEMP_MAX_SENSORS] = {};

    const uint8_t busCount = min(sensors->getDeviceCount(), (uint8_t)TEMP_MAX_SENSORS);
    bool registryChanged = false;

    for (uint8_t i = 0; i < busCount; i++) {
        DeviceAddress address;
        if (!sensors->getAddress(address, i)) {
            continue;
        }

        const int index = findEntryByAddress(address);
        if (index >= 0) {
            seen[index] = true;
            entries[index].connected = true;
            entries[index].missedScanCount = 0;
            entries[index].lastSeenMs = millis();
        } else if (addEntry(address, true)) {
            registryChanged = true;
        }
    }

    for (uint8_t i = 0; i < entryCount; i++) {
        if (seen[i]) {
            continue;
        }

        if (entries[i].connected && entries[i].missedScanCount < DS18B20_SCAN_MISS_LIMIT) {
            entries[i].missedScanCount++;
            Logger::tracef(
                TAG_TEMP,
                "DS18B20 sensor %u missed scan %u/%u, keeping last value",
                i + 1,
                entries[i].missedScanCount,
                DS18B20_SCAN_MISS_LIMIT
            );
            continue;
        }

        entries[i].connected = false;
        entries[i].hasTemperature = false;
        entries[i].temperatureC = DEVICE_DISCONNECTED_C;
    }

    if (registryChanged) {
        saveRegistry();
    }

    Logger::infof(TAG_TEMP, "DS18B20 scan complete: bus=%u known=%u", busCount, entryCount);
}

int TemperatureSensors::findEntryByAddress(const DeviceAddress& address) const {
    for (uint8_t i = 0; i < entryCount; i++) {
        if (addressEquals(entries[i].address, address)) {
            return i;
        }
    }

    return -1;
}

bool TemperatureSensors::addEntry(const DeviceAddress& address, bool connected) {
    if (entryCount >= TEMP_MAX_SENSORS) {
        Logger::warning(TAG_TEMP, "DS18B20 registry is full, new sensor ignored");
        return false;
    }

    memcpy(entries[entryCount].address, address, 8);
    entries[entryCount].role = TempSensorRole::Unknown;
    entries[entryCount].enabled = true;
    entries[entryCount].connected = connected;
    entries[entryCount].hasTemperature = false;
    entries[entryCount].temperatureC = DEVICE_DISCONNECTED_C;
    entries[entryCount].lastSeenMs = connected ? millis() : 0;
    entries[entryCount].missedScanCount = 0;
    entries[entryCount].failedReadCount = 0;
    entryCount++;
    Logger::infof(TAG_TEMP, "New DS18B20 sensor registered as Unknown: %u", entryCount);
    return true;
}

void TemperatureSensors::clearRole(TempSensorRole role, int exceptIndex) {
    for (uint8_t i = 0; i < entryCount; i++) {
        if ((int)i != exceptIndex && entries[i].role == role) {
            entries[i].role = TempSensorRole::Unknown;
            entries[i].enabled = true;
        }
    }
}

bool TemperatureSensors::parseAddress(const String& value, DeviceAddress& address) const {
    String hex;
    for (uint16_t i = 0; i < value.length(); i++) {
        const char c = value[i];
        if (isHexadecimalDigit(c)) {
            hex += c;
        }
    }

    if (hex.length() != 16) {
        return false;
    }

    for (uint8_t i = 0; i < 8; i++) {
        const String byteString = hex.substring(i * 2, i * 2 + 2);
        address[i] = (uint8_t)strtoul(byteString.c_str(), nullptr, 16);
    }

    return true;
}

bool TemperatureSensors::addressEquals(const DeviceAddress& a, const DeviceAddress& b) const {
    return memcmp(a, b, 8) == 0;
}

void TemperatureSensors::readTemperatures() {
    for (uint8_t i = 0; i < entryCount; i++) {
        if (!entries[i].connected || !entries[i].enabled) {
            entries[i].hasTemperature = false;
            entries[i].temperatureC = DEVICE_DISCONNECTED_C;
            entries[i].failedReadCount = 0;
            continue;
        }

        const float temperature = sensors->getTempC(entries[i].address);
        if (temperature != DEVICE_DISCONNECTED_C) {
            entries[i].hasTemperature = true;
            entries[i].temperatureC = temperature;
            entries[i].failedReadCount = 0;
            entries[i].missedScanCount = 0;
            entries[i].lastSeenMs = millis();
            continue;
        }

        if (entries[i].hasTemperature && entries[i].failedReadCount < DS18B20_READ_FAIL_LIMIT) {
            entries[i].failedReadCount++;
            Logger::tracef(
                TAG_TEMP,
                "DS18B20 sensor %u read failed %u/%u, keeping last value",
                i + 1,
                entries[i].failedReadCount,
                DS18B20_READ_FAIL_LIMIT
            );
            continue;
        }

        entries[i].hasTemperature = false;
        entries[i].temperatureC = DEVICE_DISCONNECTED_C;
    }

    conversionPending = false;
}

void TemperatureSensors::requestTemperatureConversion() {
    if (sensors == nullptr) {
        return;
    }

    if (conversionPending) {
        return;
    }

    sensors->requestTemperatures();
    conversionStartMs = millis();
    conversionPending = true;
}

void TemperatureSensors::printStatus(Print& output) {
    output.println();
    output.println("[TEMP] Temperature sensors status");

    if (entryCount == 0) {
        output.println("[TEMP] No DS18B20 sensors known");
        return;
    }

    for (uint8_t i = 0; i < entryCount; i++) {
        output.print(i + 1);
        output.print(") ");
        printAddress(output, entries[i].address);
        output.print("  role=");
        output.print(roleName(entries[i].role));
        output.print("  connected=");
        output.print(entries[i].connected ? 1 : 0);
        output.print("  enabled=");
        output.print(entries[i].enabled ? 1 : 0);
        output.print("  temp=");

        if (!entries[i].hasTemperature) {
            output.println("N/A");
        } else {
            output.print(entries[i].temperatureC, 2);
            output.println(" C");
        }
    }
}

void TemperatureSensors::printAddresses(Print& output) {
    output.println();
    output.println("[TEMP] DS18B20 addresses");

    if (entryCount == 0) {
        output.println("[TEMP] No DS18B20 sensors known");
        return;
    }

    for (uint8_t i = 0; i < entryCount; i++) {
        output.print("[TEMP] Sensor ");
        output.print(i + 1);
        output.print(" address: ");
        printAddress(output, entries[i].address);
        output.println();
    }
}

void TemperatureSensors::printAddress(Print& output, const DeviceAddress& address) {
    for (uint8_t i = 0; i < 8; i++) {
        if (i > 0) {
            output.print(" ");
        }
        if (address[i] < 16) {
            output.print("0");
        }

        output.print(address[i], HEX);
    }
}
