// TemperatureSensors.cpp
#include "TemperatureSensors.h"

void TemperatureSensors::begin(uint8_t pin) {
    oneWire = new OneWire(pin);
    sensors = new DallasTemperature(oneWire);

    sensors->begin();

    for (uint8_t i = 0; i < TEMP_MAX_SENSORS; i++) {
        temperatures[i] = DEVICE_DISCONNECTED_C;
    }

    readAddresses();
    readTemperatures();

    Serial.println("[TEMP] DS18B20 initialized");
    Serial.print("[TEMP] Found sensors: ");
    Serial.println(sensorCount);
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

void TemperatureSensors::printStatus(Stream& stream) {
    stream.println();
    stream.println("[TEMP] Temperature sensors status");

    if (sensorCount == 0) {
        stream.println("[TEMP] No DS18B20 sensors found");
        return;
    }

    for (uint8_t i = 0; i < sensorCount; i++) {
        stream.print("[TEMP] Sensor ");
        stream.print(i);
        stream.print(" ");

        printAddress(stream, addresses[i]);

        stream.print(" = ");

        if (temperatures[i] == DEVICE_DISCONNECTED_C) {
            stream.println("DISCONNECTED");
        } else {
            stream.print(temperatures[i], 2);
            stream.println(" °C");
        }
    }
}

void TemperatureSensors::printAddresses(Stream& stream) {
    stream.println();
    stream.println("[TEMP] DS18B20 addresses");

    if (sensorCount == 0) {
        stream.println("[TEMP] No DS18B20 sensors found");
        return;
    }

    for (uint8_t i = 0; i < sensorCount; i++) {
        stream.print("[TEMP] Sensor ");
        stream.print(i);
        stream.print(" address: ");

        printAddress(stream, addresses[i]);

        stream.println();
    }
}

void TemperatureSensors::printAddress(Stream& stream, const DeviceAddress& address) {
    for (uint8_t i = 0; i < 8; i++) {
        if (address[i] < 16) {
            stream.print("0");
        }

        stream.print(address[i], HEX);
    }
}