// TemperatureSensors.cpp
#include "TemperatureSensors.h"

#include "Logger.h"

namespace {
constexpr const char* TAG_TEMP = "TEMP";
}

void TemperatureSensors::begin(uint8_t pin) {
    oneWire = new OneWire(pin);
    sensors = new DallasTemperature(oneWire);

    sensors->begin();

    for (uint8_t i = 0; i < TEMP_MAX_SENSORS; i++) {
        temperatures[i] = DEVICE_DISCONNECTED_C;
    }

    readAddresses();
    readTemperatures();

    Logger::info(TAG_TEMP, "DS18B20 initialized");
    Logger::infof(TAG_TEMP, "Found sensors: %u", sensorCount);
}

void TemperatureSensors::update() {
    if (sensors == nullptr) {
        return;
    }

    unsigned long now = millis();

    if (now - lastUpdateMs >= updateIntervalMs) {
        lastUpdateMs = now;
        readTemperatures();
    }
}

void TemperatureSensors::forceRead() {
    if (sensors == nullptr) {
        return;
    }

    readTemperatures();
}

void TemperatureSensors::rescan() {
    if (sensors == nullptr) {
        return;
    }

    sensors->begin();

    for (uint8_t i = 0; i < TEMP_MAX_SENSORS; i++) {
        temperatures[i] = DEVICE_DISCONNECTED_C;
    }

    readAddresses();
    readTemperatures();
}

uint8_t TemperatureSensors::getSensorCount() const {
    return sensorCount;
}

float TemperatureSensors::getTemperatureC(uint8_t index) const {
    if (index >= sensorCount) {
        return DEVICE_DISCONNECTED_C;
    }

    return temperatures[index];
}

void TemperatureSensors::readAddresses() {
    sensorCount = sensors->getDeviceCount();

    if (sensorCount > TEMP_MAX_SENSORS) {
        sensorCount = TEMP_MAX_SENSORS;
    }

    for (uint8_t i = 0; i < sensorCount; i++) {
        sensors->getAddress(addresses[i], i);
    }
}

void TemperatureSensors::readTemperatures() {
    sensors->requestTemperatures();

    for (uint8_t i = 0; i < sensorCount; i++) {
        temperatures[i] = sensors->getTempC(addresses[i]);
    }
}

void TemperatureSensors::printStatus(Print& output) {
    output.println();
    output.println("[TEMP] Temperature sensors status");

    if (sensorCount == 0) {
        output.println("[TEMP] No DS18B20 sensors found");
        return;
    }

    for (uint8_t i = 0; i < sensorCount; i++) {
        output.print("[TEMP] Sensor ");
        output.print(i);
        output.print(" ");

        printAddress(output, addresses[i]);

        output.print(" = ");

        if (temperatures[i] == DEVICE_DISCONNECTED_C) {
            output.println("DISCONNECTED");
        } else {
            output.print(temperatures[i], 2);
            output.println(" °C");
        }
    }
}

void TemperatureSensors::printAddresses(Print& output) {
    output.println();
    output.println("[TEMP] DS18B20 addresses");

    if (sensorCount == 0) {
        output.println("[TEMP] No DS18B20 sensors found");
        return;
    }

    for (uint8_t i = 0; i < sensorCount; i++) {
        output.print("[TEMP] Sensor ");
        output.print(i);
        output.print(" address: ");

        printAddress(output, addresses[i]);

        output.println();
    }
}

void TemperatureSensors::printAddress(Print& output, const DeviceAddress& address) {
    for (uint8_t i = 0; i < 8; i++) {
        if (address[i] < 16) {
            output.print("0");
        }

        output.print(address[i], HEX);
    }
}
