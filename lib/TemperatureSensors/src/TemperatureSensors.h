// TemperatureSensors.h
#pragma once

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define TEMP_MAX_SENSORS 8
#define UPDATE_INTERVAL_MS 2000

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

private:
    OneWire* oneWire = nullptr;
    DallasTemperature* sensors = nullptr;

    DeviceAddress addresses[TEMP_MAX_SENSORS];
    float temperatures[TEMP_MAX_SENSORS];

    uint8_t sensorCount = 0;

    unsigned long lastUpdateMs = 0;
    const unsigned long updateIntervalMs = UPDATE_INTERVAL_MS;
    bool conversionPending = false;
    unsigned long conversionStartMs = 0;
    const unsigned long conversionDelayMs = 750;

    void readAddresses();
    void readTemperatures();
    void requestTemperatureConversion();
    void printAddress(Print& output, const DeviceAddress& address);
};
