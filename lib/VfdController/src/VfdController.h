// VfdController.h
#pragma once

#include <Arduino.h>
#include <ModbusClientRTU.h>

class VfdController {
public:
    explicit VfdController(uint8_t deRePin = 18);

    void begin(
        HardwareSerial& serial,
        int8_t rxPin = 33,
        int8_t txPin = 32,
        uint32_t baudRate = 19200,
        uint32_t serialConfig = SERIAL_8E1
    );

    void forward();
    void reverse();
    void stop();

    void setFrequency(float hz);

    void readRegister(uint16_t address, uint16_t count);
    void writeRegister(uint16_t address, uint16_t value);

    bool isInitialized() const;
    const char* getLastAction() const;
    bool hasRequestedFrequency() const;
    float getRequestedFrequencyHz() const;
    uint32_t getRequestCount() const;
    uint32_t getOkCount() const;
    uint32_t getErrorCount() const;
    uint32_t getLastToken() const;
    uint8_t getLastErrorCode() const;
    bool hasActivity() const;
    unsigned long getLastActivityAgeMs() const;

private:
    ModbusClientRTU client;
    uint32_t tokenCounter = 1;
    bool initialized = false;
    const char* lastAction = "none";
    bool requestedFrequencySet = false;
    float requestedFrequencyHz = 0.0f;
    uint32_t requestCount = 0;
    uint32_t okCount = 0;
    uint32_t errorCount = 0;
    uint32_t lastToken = 0;
    uint8_t lastErrorCode = 0;
    bool activitySeen = false;
    unsigned long lastActivityMs = 0;

    uint32_t nextToken();

    void queueReadHolding(uint16_t address, uint16_t count);
    void queueWriteSingle(uint16_t address, uint16_t value);

    void onData(ModbusMessage msg, uint32_t token);
    void onError(Error error, uint32_t token);

    static VfdController* activeInstance;

    static void handleData(ModbusMessage msg, uint32_t token);
    static void handleError(Error error, uint32_t token);
};
