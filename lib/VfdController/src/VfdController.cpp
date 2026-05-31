// VfdController.cpp
#include "VfdController.h"

#include "Logger.h"

namespace {
constexpr const char* TAG_VFD = "VFD";
constexpr uint16_t REG_COMMAND_WORD = 0x2000;
constexpr uint16_t REG_COMMAND_FREQUENCY = 0x2001;
constexpr uint16_t REG_STATUS_WORD = 0x2100;
constexpr uint16_t REG_OPERATION_FREQUENCY = 0x3000;
constexpr unsigned long ONLINE_TIMEOUT_MS = 10000;
constexpr unsigned long RX_ERROR_SUMMARY_INTERVAL_MS = 30000;

constexpr uint8_t ERR_TIMEOUT = 0xE0;
constexpr uint8_t ERR_PACKET = 0xE1;
constexpr uint8_t ERR_CRC = 0xE2;
constexpr uint8_t ERR_EXCEPTION = 0xE3;
constexpr uint8_t ERR_OVERFLOW = 0xE4;
constexpr uint8_t ERR_MALFORMED = 0xE5;

const char* errorName(uint8_t code) {
    switch (code) {
        case ERR_TIMEOUT: return "timeout";
        case ERR_PACKET: return "packet";
        case ERR_CRC: return "crc";
        case ERR_EXCEPTION: return "exception";
        case ERR_OVERFLOW: return "overflow";
        case ERR_MALFORMED: return "malformed";
        default: return "error";
    }
}
}


VfdController::VfdController(uint8_t deRePin)
    : deRePin(deRePin) {
}


void VfdController::begin(
    HardwareSerial& serial,
    int8_t rxPin,
    int8_t txPin,
    uint32_t baudRate,
    uint32_t serialConfig
) {
    bus = &serial;
    bus->begin(baudRate, serialConfig, rxPin, txPin);
    bus->setTimeout(0);

    pinMode(deRePin, OUTPUT);
    digitalWrite(deRePin, LOW);

    initialized = true;
    activitySeen = false;
    rtuState = RtuState::Idle;
    lastAction = "initialized";

    Logger::info(TAG_VFD, "Controller initialized with native RTU master");
}


bool VfdController::forward() {
    return enqueueWrite(REG_COMMAND_WORD, 0x0001, "forward");
}


bool VfdController::reverse() {
    return enqueueWrite(REG_COMMAND_WORD, 0x0002, "reverse");
}


bool VfdController::stop() {
    return enqueueWrite(REG_COMMAND_WORD, 0x0005, "stop");
}


bool VfdController::setFrequency(float hz) {
    if (hz < 20.0f) {
        hz = 20.0f;
    }

    if (hz > 50.0f) {
        hz = 50.0f;
    }

    const uint16_t value = (uint16_t)lroundf(hz * 100.0f);
    requestedFrequencySet = true;
    requestedFrequencyHz = hz;
    return enqueueWrite(REG_COMMAND_FREQUENCY, value, "set frequency");
}


void VfdController::update() {
    if (!initialized || bus == nullptr) {
        return;
    }

    const unsigned long now = millis();
    if (rtuState == RtuState::WaitingResponse) {
        readAvailableResponseBytes();
        if (responseComplete()) {
            handleResponse();
            return;
        }

        if (now - requestStartedMs >= RESPONSE_TIMEOUT_MS) {
            handleTimeout();
            return;
        }

        return;
    }

    if (rtuState == RtuState::Cooldown) {
        if (now - requestFinishedMs < interRequestDelayMs()) {
            return;
        }
        rtuState = RtuState::Idle;
    }

    if (rtuState != RtuState::Idle) {
        return;
    }

    VfdOperation operation;
    if (!dequeueOperation(operation)) {
        return;
    }

    startTransaction(operation);
}


void VfdController::pollStatus() {
    if (!initialized || rtuState != RtuState::Idle || hasQueuedOperation()) {
        return;
    }

    if (pollFrequencyNext) {
        enqueueRead(REG_OPERATION_FREQUENCY, 2, "poll frequency");
    } else {
        enqueueRead(REG_STATUS_WORD, 1, "poll status");
    }

    pollFrequencyNext = !pollFrequencyNext;
}


bool VfdController::readRegister(uint16_t address, uint16_t count) {
    if (count == 0 || count > MAX_REG_COUNT) {
        return false;
    }
    return enqueueRead(address, count, "read register");
}


bool VfdController::writeRegister(uint16_t address, uint16_t value) {
    return enqueueWrite(address, value, "write register");
}


bool VfdController::isInitialized() const {
    return initialized;
}


bool VfdController::isOnline() const {
    return initialized && !communicationError && activitySeen && okCount > 0 && millis() - lastOkMs <= ONLINE_TIMEOUT_MS;
}


bool VfdController::hasEverBeenOnline() const {
    return everOnline;
}


bool VfdController::hasCommunicationError() const {
    return communicationError || (everOnline && millis() - lastOkMs > ONLINE_TIMEOUT_MS);
}


bool VfdController::hasStatusWord() const {
    return statusWordSet;
}


uint16_t VfdController::getStatusWord() const {
    return statusWord;
}


bool VfdController::isRunning() const {
    return running;
}


bool VfdController::isCommandedRunning() const {
    return commandedRunning;
}


const char* VfdController::getLastAction() const {
    return lastAction;
}


bool VfdController::hasRequestedFrequency() const {
    return requestedFrequencySet;
}


float VfdController::getRequestedFrequencyHz() const {
    return requestedFrequencyHz;
}


bool VfdController::hasActualFrequency() const {
    return actualFrequencySet;
}


float VfdController::getActualFrequencyHz() const {
    return actualFrequencyHz;
}


uint8_t VfdController::getActualStep() const {
    return actualStep;
}


uint32_t VfdController::getRequestCount() const {
    return requestCount;
}


uint32_t VfdController::getOkCount() const {
    return okCount;
}


uint32_t VfdController::getErrorCount() const {
    return errorCount;
}


uint8_t VfdController::getConsecutiveErrorCount() const {
    return consecutiveErrorCount;
}


uint32_t VfdController::getLastToken() const {
    return lastToken;
}


uint8_t VfdController::getLastErrorCode() const {
    return lastErrorCode;
}


bool VfdController::hasRecentWriteAck(uint16_t address, uint16_t value, unsigned long sinceMs) const {
    return lastWriteAckOk
        && lastWriteAckAddress == address
        && lastWriteAckValue == value
        && lastWriteAckMs >= sinceMs;
}


unsigned long VfdController::getLastWriteAckMs() const {
    return lastWriteAckMs;
}


uint16_t VfdController::getLastWriteAckAddress() const {
    return lastWriteAckAddress;
}


uint16_t VfdController::getLastWriteAckValue() const {
    return lastWriteAckValue;
}


bool VfdController::hasActivity() const {
    return activitySeen;
}


unsigned long VfdController::getLastActivityAgeMs() const {
    if (!activitySeen) {
        return 0;
    }

    return millis() - lastActivityMs;
}


bool VfdController::isBusy() const {
    return rtuState == RtuState::WaitingResponse || opCount > 0;
}


uint32_t VfdController::nextToken() {
    return tokenCounter++;
}


uint8_t VfdController::frequencyToStep(float hz) const {
    if (hz < 20.0f) {
        return 0;
    }

    if (hz >= 50.0f) {
        return 6;
    }

    return 1 + (uint8_t)((hz - 20.0f) / 6.0f);
}


uint16_t VfdController::crc16Modbus(const uint8_t* data, size_t len) const {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}


bool VfdController::enqueueRead(uint16_t address, uint16_t count, const char* action) {
    if (count == 0 || count > MAX_REG_COUNT) {
        return false;
    }

    VfdOperation operation;
    operation.type = VfdOpType::ReadHolding;
    operation.address = address;
    operation.valueOrCount = count;
    operation.action = action;
    return enqueueOperation(operation);
}


bool VfdController::enqueueWrite(uint16_t address, uint16_t value, const char* action) {
    VfdOperation operation;
    operation.type = VfdOpType::WriteSingle;
    operation.address = address;
    operation.valueOrCount = value;
    operation.action = action;
    return enqueueOperation(operation);
}


bool VfdController::enqueueOperation(const VfdOperation& operation) {
    if (isDuplicateQueued(operation)) {
        return true;
    }

    if (opCount >= OP_QUEUE_SIZE) {
        Logger::warningf(TAG_VFD, "Request queue full action=%s address=0x%04X", operation.action, operation.address);
        return false;
    }

    opQueue[opTail] = operation;
    opTail = (opTail + 1) % OP_QUEUE_SIZE;
    opCount++;
    return true;
}


bool VfdController::dequeueOperation(VfdOperation& operation) {
    if (opCount == 0) {
        return false;
    }

    operation = opQueue[opHead];
    opQueue[opHead] = VfdOperation();
    opHead = (opHead + 1) % OP_QUEUE_SIZE;
    opCount--;
    return true;
}


bool VfdController::hasQueuedOperation() const {
    return opCount > 0;
}


bool VfdController::isDuplicateQueued(const VfdOperation& operation) const {
    if (rtuState == RtuState::WaitingResponse
        && activeOperation.type == operation.type
        && activeOperation.address == operation.address
        && activeOperation.valueOrCount == operation.valueOrCount) {
        return true;
    }

    for (uint8_t i = 0; i < opCount; i++) {
        const uint8_t index = (opHead + i) % OP_QUEUE_SIZE;
        const VfdOperation& queued = opQueue[index];
        if (queued.type == operation.type
            && queued.address == operation.address
            && queued.valueOrCount == operation.valueOrCount) {
            return true;
        }
    }

    return false;
}


bool VfdController::startTransaction(const VfdOperation& operation) {
    if (bus == nullptr) {
        return false;
    }

    while (bus->available()) {
        bus->read();
    }

    txLen = 0;
    rxLen = 0;
    expectedResponseLen = 0;
    activeOperation = operation;
    expectedFunction = operation.type == VfdOpType::ReadHolding ? READ_HOLDING : WRITE_SINGLE;
    inFlightToken = nextToken();
    lastToken = inFlightToken;
    lastAction = operation.action;
    requestStartedMs = millis();

    txBuffer[txLen++] = SLAVE_ID;
    txBuffer[txLen++] = expectedFunction;
    txBuffer[txLen++] = (uint8_t)(operation.address >> 8);
    txBuffer[txLen++] = (uint8_t)(operation.address & 0xFF);
    txBuffer[txLen++] = (uint8_t)(operation.valueOrCount >> 8);
    txBuffer[txLen++] = (uint8_t)(operation.valueOrCount & 0xFF);
    const uint16_t crc = crc16Modbus(txBuffer, txLen);
    txBuffer[txLen++] = (uint8_t)(crc & 0xFF);
    txBuffer[txLen++] = (uint8_t)(crc >> 8);

    digitalWrite(deRePin, HIGH);
    delayMicroseconds(100);
    bus->write(txBuffer, txLen);
    bus->flush();
    delayMicroseconds(200);
    digitalWrite(deRePin, LOW);

    requestCount++;
    rtuState = RtuState::WaitingResponse;
    Logger::debugf(
        TAG_VFD,
        "TX token=%lu fc=%02X addr=0x%04X value=0x%04X action=%s",
        (unsigned long)inFlightToken,
        expectedFunction,
        operation.address,
        operation.valueOrCount,
        operation.action
    );
    return true;
}


void VfdController::finishTransaction(bool hadError) {
    requestFinishedMs = millis();
    lastRequestHadError = hadError;
    activeOperation = VfdOperation();
    inFlightToken = 0;
    expectedResponseLen = 0;
    rtuState = RtuState::Cooldown;
}


unsigned long VfdController::interRequestDelayMs() const {
    return lastRequestHadError ? INTER_REQUEST_ERROR_DELAY_MS : INTER_REQUEST_DELAY_MS;
}


void VfdController::readAvailableResponseBytes() {
    while (bus != nullptr && bus->available()) {
        if (rxLen >= sizeof(rxBuffer)) {
            recordTransactionError(ERR_OVERFLOW, "rx overflow");
            finishTransaction(true);
            return;
        }
        rxBuffer[rxLen++] = (uint8_t)bus->read();
    }
}


bool VfdController::updateExpectedResponseLength() {
    if (rxLen < 2) {
        return false;
    }

    if (rxBuffer[1] == (expectedFunction | 0x80)) {
        expectedResponseLen = 5;
        return true;
    }

    if (rxBuffer[1] != expectedFunction) {
        expectedResponseLen = rxLen >= 5 ? rxLen : 0;
        return expectedResponseLen > 0;
    }

    if (expectedFunction == READ_HOLDING) {
        if (rxLen < 3) {
            return false;
        }
        const uint8_t byteCount = rxBuffer[2];
        expectedResponseLen = 3 + byteCount + 2;
        if (expectedResponseLen > sizeof(rxBuffer)) {
            expectedResponseLen = sizeof(rxBuffer);
        }
        return true;
    }

    if (expectedFunction == WRITE_SINGLE) {
        expectedResponseLen = 8;
        return true;
    }

    return false;
}


bool VfdController::responseComplete() {
    if (!updateExpectedResponseLength()) {
        return false;
    }

    return expectedResponseLen > 0 && rxLen >= expectedResponseLen;
}


void VfdController::handleResponse() {
    activitySeen = true;
    lastActivityMs = millis();

    if (expectedResponseLen < 5 || rxLen < expectedResponseLen) {
        recordTransactionError(ERR_MALFORMED, "short response");
        finishTransaction(true);
        return;
    }

    if (rxBuffer[0] != SLAVE_ID) {
        recordTransactionError(ERR_PACKET, "wrong slave");
        finishTransaction(true);
        return;
    }

    const uint16_t receivedCrc = (uint16_t)rxBuffer[expectedResponseLen - 2] | ((uint16_t)rxBuffer[expectedResponseLen - 1] << 8);
    const uint16_t calculatedCrc = crc16Modbus(rxBuffer, expectedResponseLen - 2);
    if (receivedCrc != calculatedCrc) {
        Logger::tracef(
            TAG_VFD,
            "CRC mismatch token=%lu received=0x%04X calculated=0x%04X len=%u expected=%u function=0x%02X",
            (unsigned long)lastToken,
            receivedCrc,
            calculatedCrc,
            (unsigned)rxLen,
            (unsigned)expectedResponseLen,
            expectedFunction
        );
        recordTransactionError(ERR_CRC, "crc");
        finishTransaction(true);
        return;
    }

    if (rxBuffer[1] == (expectedFunction | 0x80)) {
        recordTransactionError(ERR_EXCEPTION, "exception");
        finishTransaction(true);
        return;
    }

    if (rxBuffer[1] != expectedFunction) {
        recordTransactionError(ERR_PACKET, "wrong function");
        finishTransaction(true);
        return;
    }

    if (expectedFunction == READ_HOLDING) {
        const uint8_t byteCount = rxBuffer[2];
        if (byteCount != activeOperation.valueOrCount * 2) {
            recordTransactionError(ERR_MALFORMED, "byte count");
            finishTransaction(true);
            return;
        }
        parseReadResponse();
    } else if (expectedFunction == WRITE_SINGLE) {
        const uint16_t echoAddress = ((uint16_t)rxBuffer[2] << 8) | rxBuffer[3];
        const uint16_t echoValue = ((uint16_t)rxBuffer[4] << 8) | rxBuffer[5];
        if (echoAddress != activeOperation.address || echoValue != activeOperation.valueOrCount) {
            recordTransactionError(ERR_PACKET, "write echo");
            finishTransaction(true);
            return;
        }
        parseWriteResponse();
    }

    okCount++;
    everOnline = true;
    if (consecutiveErrorCount > 0) {
        Logger::infof(TAG_VFD, "Link recovered after %u consecutive RX errors", consecutiveErrorCount);
        suppressedRxErrorLogCount = 0;
    }
    communicationError = false;
    consecutiveErrorCount = 0;
    lastOkMs = millis();
    Logger::debugf(
        TAG_VFD,
        "RX OK token=%lu fc=%02X len=%u",
        (unsigned long)lastToken,
        expectedFunction,
        (unsigned)expectedResponseLen
    );
    finishTransaction(false);
}


void VfdController::handleTimeout() {
    recordTransactionError(ERR_TIMEOUT, "timeout");
    finishTransaction(true);
}


void VfdController::recordTransactionError(uint8_t code, const char* reason) {
    const unsigned long now = millis();
    errorCount++;
    switch (code) {
        case ERR_TIMEOUT: timeoutErrorCount++; break;
        case ERR_PACKET: packetErrorCount++; break;
        case ERR_CRC: crcErrorCount++; break;
        case ERR_EXCEPTION: exceptionErrorCount++; break;
        case ERR_MALFORMED: malformedErrorCount++; break;
        default: break;
    }
    if (consecutiveErrorCount < 255) {
        consecutiveErrorCount++;
    }

    lastErrorCode = code;
    activitySeen = true;
    lastActivityMs = now;
    if (consecutiveErrorCount >= LINK_ERROR_THRESHOLD || (everOnline && now - lastOkMs > ONLINE_TIMEOUT_MS)) {
        communicationError = true;
    }

    if (consecutiveErrorCount <= 3) {
        Logger::warningf(
            TAG_VFD,
            "RX ERR token=%lu code=%02X reason=%s len=%u consecutive=%u",
            (unsigned long)lastToken,
            code,
            reason,
            (unsigned)rxLen,
            consecutiveErrorCount
        );
        logRawRx(code, reason);
        return;
    }

    suppressedRxErrorLogCount++;
    if (now - lastRxErrorSummaryMs >= RX_ERROR_SUMMARY_INTERVAL_MS) {
        lastRxErrorSummaryMs = now;
        Logger::warningf(
            TAG_VFD,
            "RX errors summary: timeout=%lu wrong=%lu crc=%lu malformed=%lu exception=%lu suppressed=%lu consecutive=%u",
            (unsigned long)timeoutErrorCount,
            (unsigned long)packetErrorCount,
            (unsigned long)crcErrorCount,
            (unsigned long)malformedErrorCount,
            (unsigned long)exceptionErrorCount,
            (unsigned long)suppressedRxErrorLogCount,
            consecutiveErrorCount
        );
        suppressedRxErrorLogCount = 0;
    }
}


void VfdController::logRawRx(uint8_t code, const char* reason) {
    char raw[160] = {};
    size_t offset = 0;
    const size_t len = rxLen < sizeof(rxBuffer) ? rxLen : sizeof(rxBuffer);
    for (size_t i = 0; i < len && offset + 4 < sizeof(raw); i++) {
        offset += snprintf(raw + offset, sizeof(raw) - offset, "%02X ", rxBuffer[i]);
    }

    Logger::tracef(
        TAG_VFD,
        "RX ERR token=%lu code=%02X reason=%s len=%u raw=%s",
        (unsigned long)lastToken,
        code,
        reason,
        (unsigned)rxLen,
        raw
    );
}


void VfdController::parseReadResponse() {
    const uint16_t firstRegister = ((uint16_t)rxBuffer[3] << 8) | rxBuffer[4];

    if (activeOperation.address == REG_STATUS_WORD && activeOperation.valueOrCount >= 1) {
        statusWord = firstRegister;
        statusWordSet = true;
        running = statusWord == 0x0001 || statusWord == 0x0002;
        Logger::tracef(TAG_VFD, "Status word 0x%04X running=%u", statusWord, running ? 1 : 0);
        return;
    }

    if (activeOperation.address == REG_OPERATION_FREQUENCY && activeOperation.valueOrCount >= 1) {
        actualFrequencyHz = firstRegister / 100.0f;
        actualFrequencySet = true;
        actualStep = frequencyToStep(actualFrequencyHz);
        if (activeOperation.valueOrCount >= 2) {
            const uint16_t settingRegister = ((uint16_t)rxBuffer[5] << 8) | rxBuffer[6];
            requestedFrequencySet = true;
            requestedFrequencyHz = settingRegister / 100.0f;
        }
        Logger::tracef(TAG_VFD, "Monitor output %.2f Hz step %u setting %.2f Hz", actualFrequencyHz, actualStep, requestedFrequencyHz);
        return;
    }

    Logger::infof(TAG_VFD, "Read 0x%04X count=%u first=0x%04X", activeOperation.address, activeOperation.valueOrCount, firstRegister);
}


void VfdController::parseWriteResponse() {
    const uint16_t address = ((uint16_t)rxBuffer[2] << 8) | rxBuffer[3];
    const uint16_t value = ((uint16_t)rxBuffer[4] << 8) | rxBuffer[5];
    lastWriteAckOk = true;
    lastWriteAckToken = lastToken;
    lastWriteAckAddress = address;
    lastWriteAckValue = value;
    lastWriteAckMs = millis();
    if (address == REG_COMMAND_WORD) {
        commandedRunning = value == 0x0001 || value == 0x0002;
    } else if (address == REG_COMMAND_FREQUENCY) {
        requestedFrequencySet = true;
        requestedFrequencyHz = value / 100.0f;
    }
}
