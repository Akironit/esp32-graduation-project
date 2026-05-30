// VfdController.cpp
#include "VfdController.h"

#include <cstring>

#include "Logger.h"

namespace {
constexpr const char* TAG_VFD = "VFD";
constexpr uint16_t REG_COMMAND_FREQUENCY = 0x2001;
constexpr uint16_t REG_STATUS_WORD = 0x2100;
constexpr uint16_t REG_OPERATION_FREQUENCY = 0x3000;
constexpr unsigned long ONLINE_TIMEOUT_MS = 10000;

bool isCrcNoise(Error error) {
    return error == CRC_ERROR || error == ASCII_CRC_ERR;
}
}


VfdController* VfdController::activeInstance = nullptr;


VfdController::VfdController(uint8_t deRePin)
    : client(deRePin) {
}


void VfdController::begin(
    HardwareSerial& serial,
    int8_t rxPin,
    int8_t txPin,
    uint32_t baudRate,
    uint32_t serialConfig
) {
    activeInstance = this;

    RTUutils::prepareHardwareSerial(serial);
    serial.begin(baudRate, serialConfig, rxPin, txPin);

    client.onDataHandler(&VfdController::handleData);
    client.onErrorHandler(&VfdController::handleError);

    client.begin(serial);
    initialized = true;
    activitySeen = false;
    lastAction = "initialized";

    Logger::info(TAG_VFD, "Controller initialized");
}


bool VfdController::forward() {
    commandedRunning = true;
    return enqueueWrite(0x2000, 0x0001, "forward");
}


bool VfdController::reverse() {
    commandedRunning = true;
    return enqueueWrite(0x2000, 0x0002, "reverse");
}


bool VfdController::stop() {
    commandedRunning = false;
    return enqueueWrite(0x2000, 0x0005, "stop");
}


bool VfdController::setFrequency(float hz) {
    if (hz < 0.0f) {
        hz = 0.0f;
    }

    if (hz > 50.0f) {
        hz = 50.0f;
    }

    uint16_t value = (uint16_t)lroundf(hz * 100.0f);
    requestedFrequencySet = true;
    requestedFrequencyHz = hz;
    return enqueueWrite(REG_COMMAND_FREQUENCY, value, "set frequency");
}


void VfdController::update() {
    if (!initialized) {
        return;
    }

    const unsigned long now = millis();
    if (requestInFlight) {
        if (now - requestStartedMs > REQUEST_TIMEOUT_GUARD_MS) {
            Logger::warningf(
                TAG_VFD,
                "Request timeout guard token=%lu age=%lu ms action=%s",
                (unsigned long)inFlightToken,
                (unsigned long)(now - requestStartedMs),
                lastAction
            );
            recordError(TIMEOUT, inFlightToken, true, "error");
            completeRequest(true);
        }
        return;
    }

    if (requestFinishedMs > 0) {
        const unsigned long cooldownMs = lastRequestHadError ? REQUEST_ERROR_COOLDOWN_MS : REQUEST_COOLDOWN_MS;
        if (now - requestFinishedMs < cooldownMs) {
            return;
        }
    }

    VfdOperation operation;
    if (!dequeueOperation(operation)) {
        return;
    }

    sendOperation(operation);
}


void VfdController::pollStatus() {
    if (!initialized) {
        return;
    }

    if (requestInFlight || hasQueuedOperation()) {
        Logger::tracef(TAG_VFD, "Status poll skipped: pending request token=%lu queued=%u", (unsigned long)inFlightToken, opCount);
        return;
    }

    if (pollFrequencyNext) {
        enqueueRead(REG_OPERATION_FREQUENCY, 2, "poll frequency");
    } else {
        enqueueRead(REG_STATUS_WORD, 1, "poll status");
    }

    pollFrequencyNext = !pollFrequencyNext;
}

void VfdController::suppressPolling(unsigned long durationMs) {
    pollingSuppressedUntilMs = millis() + durationMs;
}


bool VfdController::readRegister(uint16_t address, uint16_t count) {
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
    return requestInFlight;
}

bool VfdController::isPollingSuppressed() const {
    return (int32_t)(pollingSuppressedUntilMs - millis()) > 0;
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


bool VfdController::enqueueRead(uint16_t address, uint16_t count, const char* action) {
    VfdOperation operation;
    operation.type = VfdOpType::ReadHolding;
    operation.address = address;
    operation.valueOrCount = count;
    operation.action = action;

    if (isDuplicateQueued(operation)) {
        Logger::tracef(TAG_VFD, "Read request already queued action=%s address=0x%04X count=%u", action, address, count);
        return true;
    }

    if (opCount >= OP_QUEUE_SIZE) {
        Logger::errorf(TAG_VFD, "Request queue full action=%s address=0x%04X", action, address);
        return false;
    }

    opQueue[opTail] = operation;
    opTail = (opTail + 1) % OP_QUEUE_SIZE;
    opCount++;
    Logger::tracef(TAG_VFD, "Enqueued read action=%s address=0x%04X count=%u queued=%u", action, address, count, opCount);
    return true;
}


bool VfdController::enqueueWrite(uint16_t address, uint16_t value, const char* action) {
    VfdOperation operation;
    operation.type = VfdOpType::WriteSingle;
    operation.address = address;
    operation.valueOrCount = value;
    operation.action = action;

    if (isDuplicateQueued(operation)) {
        Logger::tracef(TAG_VFD, "Write request already queued action=%s address=0x%04X value=0x%04X", action, address, value);
        return true;
    }

    if (opCount >= OP_QUEUE_SIZE) {
        Logger::errorf(TAG_VFD, "Request queue full action=%s address=0x%04X", action, address);
        return false;
    }

    opQueue[opTail] = operation;
    opTail = (opTail + 1) % OP_QUEUE_SIZE;
    opCount++;
    Logger::tracef(TAG_VFD, "Enqueued write action=%s address=0x%04X value=0x%04X queued=%u", action, address, value, opCount);
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
    if (requestInFlight
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


void VfdController::sendOperation(const VfdOperation& operation) {
    const uint32_t token = nextToken();
    Error error = SUCCESS;

    if (operation.type == VfdOpType::ReadHolding) {
        error = client.addRequest(token, 1, READ_HOLD_REGISTER, operation.address, operation.valueOrCount);
    } else if (operation.type == VfdOpType::WriteSingle) {
        error = client.addRequest(token, 1, WRITE_HOLD_REGISTER, operation.address, operation.valueOrCount);
    } else {
        return;
    }

    lastAction = operation.action;
    lastToken = token;

    if (error != SUCCESS) {
        recordError(error, token, true, "error");
        requestFinishedMs = millis();
        lastRequestHadError = true;
        return;
    }

    requestInFlight = true;
    activeOperation = operation;
    inFlightToken = token;
    requestStartedMs = millis();
    requestCount++;

    if (operation.type == VfdOpType::ReadHolding && operation.address == REG_STATUS_WORD) {
        statusWordToken = token;
    } else if (operation.type == VfdOpType::ReadHolding && operation.address == REG_OPERATION_FREQUENCY) {
        monitorFrequencyToken = token;
    }

    Logger::tracef(
        TAG_VFD,
        "TX token=%lu action=%s type=%u address=0x%04X value=0x%04X queued=%u",
        (unsigned long)token,
        operation.action,
        (unsigned)operation.type,
        operation.address,
        operation.valueOrCount,
        opCount
    );
}


void VfdController::completeRequest(bool hadError) {
    requestInFlight = false;
    activeOperation = VfdOperation();
    inFlightToken = 0;
    requestFinishedMs = millis();
    lastRequestHadError = hadError;
}


void VfdController::recordError(Error error, uint32_t token, bool countAsTotalError, const char* level) {
    ModbusError modbusError(error);
    if (countAsTotalError) {
        errorCount++;
    }
    if (isCrcNoise(error)) {
        crcErrorCount++;
    }
    if (consecutiveErrorCount < 255) {
        consecutiveErrorCount++;
    }

    lastToken = token;
    lastErrorCode = (uint8_t)error;
    activitySeen = true;
    lastActivityMs = millis();
    if (consecutiveErrorCount >= LINK_ERROR_THRESHOLD || (lastOkMs > 0 && millis() - lastOkMs > ONLINE_TIMEOUT_MS)) {
        communicationError = true;
    }

    if (strcmp(level, "trace") == 0) {
        Logger::tracef(
            TAG_VFD,
            "ERR token=%lu code=%02X (%s)",
            (unsigned long)token,
            (uint8_t)error,
            (const char*)modbusError
        );
    } else {
        Logger::errorf(
            TAG_VFD,
            "ERR token=%lu code=%02X (%s)",
            (unsigned long)token,
            (uint8_t)error,
            (const char*)modbusError
        );
    }
}


void VfdController::onData(ModbusMessage msg, uint32_t token) {
    if (!requestInFlight || token != inFlightToken) {
        Logger::tracef(TAG_VFD, "Stale RX ignored token=%lu active=%lu", (unsigned long)token, (unsigned long)inFlightToken);
        return;
    }

    completeRequest(false);
    okCount++;
    lastToken = token;
    activitySeen = true;
    everOnline = true;
    communicationError = false;
    consecutiveErrorCount = 0;
    lastActivityMs = millis();
    lastOkMs = lastActivityMs;

    if (token == statusWordToken && msg.size() >= 5 && msg.getFunctionCode() == READ_HOLD_REGISTER) {
        statusWord = ((uint16_t)msg[3] << 8) | msg[4];
        statusWordSet = true;
        running = statusWord == 0x0001 || statusWord == 0x0002;
        Logger::tracef(TAG_VFD, "Status word 0x%04X running=%u", statusWord, running ? 1 : 0);
    }

    if (token == monitorFrequencyToken && msg.size() >= 7 && msg.getFunctionCode() == READ_HOLD_REGISTER) {
        const uint16_t rawFrequency = ((uint16_t)msg[3] << 8) | msg[4];
        const uint16_t rawSettingFrequency = ((uint16_t)msg[5] << 8) | msg[6];
        actualFrequencyHz = rawFrequency / 100.0f;
        actualFrequencySet = true;
        actualStep = frequencyToStep(actualFrequencyHz);
        requestedFrequencySet = true;
        requestedFrequencyHz = rawSettingFrequency / 100.0f;
        Logger::tracef(
            TAG_VFD,
            "Monitor output %.2f Hz step %u setting %.2f Hz",
            actualFrequencyHz,
            actualStep,
            requestedFrequencyHz
        );
    }

    Logger::tracef(
        TAG_VFD,
        "OK token=%lu server=%u FC=%u len=%u",
        (unsigned long)token,
        msg.getServerID(),
        msg.getFunctionCode(),
        (unsigned)msg.size()
    );

    char data[160] = {};
    size_t offset = 0;

    for (auto& byteValue : msg) {
        if (offset + 4 >= sizeof(data)) {
            break;
        }

        offset += snprintf(data + offset, sizeof(data) - offset, "%02X ", byteValue);
    }

    Logger::tracef(TAG_VFD, "Data: %s", data);
}


void VfdController::onError(Error error, uint32_t token) {
    if (!requestInFlight || token != inFlightToken) {
        Logger::tracef(TAG_VFD, "Stale error ignored token=%lu active=%lu", (unsigned long)token, (unsigned long)inFlightToken);
        return;
    }

    completeRequest(true);
    recordError(error, token, !isCrcNoise(error), isCrcNoise(error) ? "trace" : "error");
}


void VfdController::handleData(ModbusMessage msg, uint32_t token) {
    if (activeInstance != nullptr) {
        activeInstance->onData(msg, token);
    }
}


void VfdController::handleError(Error error, uint32_t token) {
    if (activeInstance != nullptr) {
        activeInstance->onError(error, token);
    }
}
