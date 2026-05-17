// TemperatureSensors.h
#pragma once

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Preferences.h>

#define TEMP_MAX_SENSORS 8
#define UPDATE_INTERVAL_MS 2000
#define DS18B20_RESCAN_INTERVAL_MS 180000UL
#define DS18B20_SCAN_MISS_LIMIT 3
#define DS18B20_READ_FAIL_LIMIT 3

enum class TempSensorRole : uint8_t {
    Unknown = 0,
    Indoor = 1,
    Outdoor = 2,
    Unused = 3
};

struct TempSensorEntry {
    DeviceAddress address = {};
    TempSensorRole role = TempSensorRole::Unknown;
    bool enabled = true;
    bool connected = false;
    bool hasTemperature = false;
    float temperatureC = DEVICE_DISCONNECTED_C;
    unsigned long lastSeenMs = 0;
    uint8_t missedScanCount = 0;
    uint8_t failedReadCount = 0;
};

class TemperatureSensors {
public:
    void begin(uint8_t pin);
    void update();

    void printStatus(Print& output);
    void printAddresses(Print& output);
    void forceRead();
    void rescan();

    uint8_t getSensorCount() const;
    float getTemperatureC(uint8_t index) const;
    bool getTemperatureByRole(TempSensorRole role, float& temperatureC) const;
    uint8_t getEntryCount() const;
    const TempSensorEntry* getEntry(uint8_t index) const;
    bool assignRole(uint8_t index, TempSensorRole role);
    bool forget(uint8_t index);
    bool swapRoles();
    int findEntryBySelector(const String& selector) const;
    const char* roleName(TempSensorRole role) const;

private:
    OneWire* oneWire = nullptr;
    DallasTemperature* sensors = nullptr;
    Preferences preferences;

    TempSensorEntry entries[TEMP_MAX_SENSORS];
    uint8_t entryCount = 0;

    unsigned long lastUpdateMs = 0;
    unsigned long lastRescanMs = 0;
    const unsigned long updateIntervalMs = UPDATE_INTERVAL_MS;
    bool conversionPending = false;
    bool rescanRequested = false;
    unsigned long conversionStartMs = 0;
    const unsigned long conversionDelayMs = 750;

    void loadRegistry();
    void saveRegistry();
    void scanBus();
    int findEntryByAddress(const DeviceAddress& address) const;
    bool addEntry(const DeviceAddress& address, bool connected);
    void clearRole(TempSensorRole role, int exceptIndex);
    bool parseAddress(const String& value, DeviceAddress& address) const;
    bool addressEquals(const DeviceAddress& a, const DeviceAddress& b) const;
    void readTemperatures();
    void requestTemperatureConversion();
    void printAddress(Print& output, const DeviceAddress& address);
};
