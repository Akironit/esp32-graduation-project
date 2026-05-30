// VfdController.h
#pragma once

#include <Arduino.h>

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
    bool hasRecentWriteAck(uint16_t address, uint16_t value, unsigned long sinceMs) const;
    unsigned long getLastWriteAckMs() const;
    uint16_t getLastWriteAckAddress() const;
    uint16_t getLastWriteAckValue() const;
    bool hasActivity() const;
    unsigned long getLastActivityAgeMs() const;
    bool isBusy() const;

private:
    static constexpr uint8_t SLAVE_ID = 1;
    static constexpr uint8_t READ_HOLDING = 0x03;
    static constexpr uint8_t WRITE_SINGLE = 0x06;
    static constexpr uint8_t MAX_REG_COUNT = 16;
    static constexpr size_t TX_BUFFER_SIZE = 8;
    static constexpr size_t RX_BUFFER_SIZE = 64;
    static constexpr uint8_t OP_QUEUE_SIZE = 8;
    static constexpr unsigned long RESPONSE_TIMEOUT_MS = 250;
    static constexpr unsigned long INTER_REQUEST_DELAY_MS = 150;
    static constexpr unsigned long INTER_REQUEST_ERROR_DELAY_MS = 500;
    static constexpr uint8_t LINK_ERROR_THRESHOLD = 3;

    enum class RtuState : uint8_t {
        Idle,
        WaitingResponse,
        Cooldown
    };

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

    HardwareSerial* bus = nullptr;
    uint8_t deRePin = 18;
    RtuState rtuState = RtuState::Idle;
    VfdOperation activeOperation;
    VfdOperation opQueue[OP_QUEUE_SIZE];
    uint8_t opHead = 0;
    uint8_t opTail = 0;
    uint8_t opCount = 0;
    uint8_t txBuffer[TX_BUFFER_SIZE] = {};
    uint8_t rxBuffer[RX_BUFFER_SIZE] = {};
    size_t txLen = 0;
    size_t rxLen = 0;
    size_t expectedResponseLen = 0;
    uint8_t expectedFunction = 0;
    uint32_t tokenCounter = 1;
    uint32_t inFlightToken = 0;
    unsigned long requestStartedMs = 0;
    unsigned long requestFinishedMs = 0;
    bool lastRequestHadError = false;

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
    uint32_t timeoutErrorCount = 0;
    uint32_t packetErrorCount = 0;
    uint32_t malformedErrorCount = 0;
    uint32_t exceptionErrorCount = 0;
    uint32_t suppressedRxErrorLogCount = 0;
    uint8_t consecutiveErrorCount = 0;
    uint32_t lastToken = 0;
    uint8_t lastErrorCode = 0;
    uint32_t lastWriteAckToken = 0;
    uint16_t lastWriteAckAddress = 0;
    uint16_t lastWriteAckValue = 0;
    unsigned long lastWriteAckMs = 0;
    bool lastWriteAckOk = false;
    bool activitySeen = false;
    bool everOnline = false;
    bool communicationError = false;
    unsigned long lastActivityMs = 0;
    unsigned long lastOkMs = 0;
    unsigned long lastRxErrorSummaryMs = 0;
    bool pollFrequencyNext = false;

    uint32_t nextToken();
    uint8_t frequencyToStep(float hz) const;
    uint16_t crc16Modbus(const uint8_t* data, size_t len) const;

    bool enqueueRead(uint16_t address, uint16_t count, const char* action);
    bool enqueueWrite(uint16_t address, uint16_t value, const char* action);
    bool enqueueOperation(const VfdOperation& operation);
    bool dequeueOperation(VfdOperation& operation);
    bool hasQueuedOperation() const;
    bool isDuplicateQueued(const VfdOperation& operation) const;
    bool startTransaction(const VfdOperation& operation);
    void finishTransaction(bool hadError);
    unsigned long interRequestDelayMs() const;

    void readAvailableResponseBytes();
    bool updateExpectedResponseLength();
    bool responseComplete();
    void handleResponse();
    void handleTimeout();
    void recordTransactionError(uint8_t code, const char* reason);
    void logRawRx(uint8_t code, const char* reason);
    void parseReadResponse();
    void parseWriteResponse();
};
