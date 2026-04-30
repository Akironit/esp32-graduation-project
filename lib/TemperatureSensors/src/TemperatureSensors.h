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

    void printStatus(Stream& stream);
    void printAddresses(Stream& stream);
    void forceRead();
    void rescan();

    uint8_t getSensorCount() const;

private:
    OneWire* oneWire = nullptr;
    DallasTemperature* sensors = nullptr;

    DeviceAddress addresses[TEMP_MAX_SENSORS];
    float temperatures[TEMP_MAX_SENSORS];

    uint8_t sensorCount = 0;

    unsigned long lastUpdateMs = 0;
    const unsigned long updateIntervalMs = UPDATE_INTERVAL_MS;

    void readAddresses();
    void readTemperatures();
    void printAddress(Stream& stream, const DeviceAddress& address);
};