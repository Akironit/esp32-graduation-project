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

    bool forward();
    bool reverse();
    bool stop();

    bool setFrequency(float hz);
    void update();
    void pollStatus();
    void suppressPolling(unsigned long durationMs = 5000);

    bool readRegister(uint16_t address, uint16_t count);
    bool writeRegister(uint16_t address, uint16_t value);

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
    uint8_t getConsecutiveErrorCount() const;
    uint32_t getLastToken() const;
    uint8_t getLastErrorCode() const;
    bool hasActivity() const;
    unsigned long getLastActivityAgeMs() const;
    bool isBusy() const;
    bool isPollingSuppressed() const;

private:
    static constexpr unsigned long REQUEST_TIMEOUT_GUARD_MS = 3000;
    static constexpr unsigned long REQUEST_COOLDOWN_MS = 100;
    static constexpr unsigned long REQUEST_ERROR_COOLDOWN_MS = 1500;
    static constexpr uint8_t OP_QUEUE_SIZE = 8;
    static constexpr uint8_t LINK_ERROR_THRESHOLD = 5;

    enum class VfdOpType : uint8_t {
        None,
        ReadHolding,
        WriteSingle
    };

    struct VfdOperation {
        VfdOpType type = VfdOpType::None;
        uint16_t address = 0;
        uint16_t valueOrCount = 0;
        const char* action = "none";
    };

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
    bool lastRequestHadError = false;
    VfdOperation activeOperation;
    uint32_t inFlightToken = 0;
    unsigned long requestStartedMs = 0;
    unsigned long requestFinishedMs = 0;
    unsigned long pollingSuppressedUntilMs = 0;
    VfdOperation opQueue[OP_QUEUE_SIZE];
    uint8_t opHead = 0;
    uint8_t opTail = 0;
    uint8_t opCount = 0;

    uint32_t nextToken();
    uint8_t frequencyToStep(float hz) const;

    bool enqueueRead(uint16_t address, uint16_t count, const char* action);
    bool enqueueWrite(uint16_t address, uint16_t value, const char* action);
    bool dequeueOperation(VfdOperation& operation);
    bool hasQueuedOperation() const;
    bool isDuplicateQueued(const VfdOperation& operation) const;
    void sendOperation(const VfdOperation& operation);
    void completeRequest(bool hadError);
    void recordError(Error error, uint32_t token, bool countAsTotalError, const char* level);

    void onData(ModbusMessage msg, uint32_t token);
    void onError(Error error, uint32_t token);

    static VfdController* activeInstance;

    static void handleData(ModbusMessage msg, uint32_t token);
    static void handleError(Error error, uint32_t token);
};
