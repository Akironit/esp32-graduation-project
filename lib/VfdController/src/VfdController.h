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
    void pollStatus();

    void readRegister(uint16_t address, uint16_t count);
    void writeRegister(uint16_t address, uint16_t value);

    bool isInitialized() const;
    bool isOnline() const;
    bool hasEverBeenOnline() const;
    bool hasCommunicationError() const;
    bool hasStatusWord() const;
    uint16_t getStatusWord() const;
    bool isRunning() const;
    bool isCommandedRunning() const;
    const char* getLastAction() const;
    bool hasRequestedFrequency() const;
    float getRequestedFrequencyHz() const;
    bool hasActualFrequency() const;
    float getActualFrequencyHz() const;
    uint8_t getActualStep() const;
    uint32_t getRequestCount() const;
    uint32_t getOkCount() const;
    uint32_t getErrorCount() const;
    uint32_t getLastToken() const;
    uint8_t getLastErrorCode() const;
    bool hasActivity() const;
    unsigned long getLastActivityAgeMs() const;
    bool isBusy() const;

private:
    static constexpr unsigned long REQUEST_TIMEOUT_GUARD_MS = 1000;

    ModbusClientRTU client;
    uint32_t tokenCounter = 1;
    bool initialized = false;
    const char* lastAction = "none";
    bool requestedFrequencySet = false;
    float requestedFrequencyHz = 0.0f;
    bool statusWordSet = false;
    uint16_t statusWord = 0;
    bool running = false;
    bool commandedRunning = false;
    bool actualFrequencySet = false;
    float actualFrequencyHz = 0.0f;
    uint8_t actualStep = 0;
    uint32_t requestCount = 0;
    uint32_t okCount = 0;
    uint32_t errorCount = 0;
    uint32_t crcErrorCount = 0;
    uint8_t consecutiveErrorCount = 0;
    uint32_t lastToken = 0;
    uint8_t lastErrorCode = 0;
    bool activitySeen = false;
    bool everOnline = false;
    bool communicationError = false;
    unsigned long lastActivityMs = 0;
    unsigned long lastOkMs = 0;
    uint32_t statusWordToken = 0;
    uint32_t monitorFrequencyToken = 0;
    bool pollFrequencyNext = false;
    bool requestInFlight = false;
    uint32_t inFlightToken = 0;
    unsigned long requestStartedMs = 0;

    uint32_t nextToken();
    uint8_t frequencyToStep(float hz) const;

    uint32_t queueReadHolding(uint16_t address, uint16_t count);
    uint32_t queueWriteSingle(uint16_t address, uint16_t value);

    void onData(ModbusMessage msg, uint32_t token);
    void onError(Error error, uint32_t token);

    static VfdController* activeInstance;

    static void handleData(ModbusMessage msg, uint32_t token);
    static void handleError(Error error, uint32_t token);
};
