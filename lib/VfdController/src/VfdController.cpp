// VfdController.cpp
#include "VfdController.h"

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


void VfdController::forward() {
    lastAction = "forward";
    commandedRunning = true;
    communicationError = false;
    queueWriteSingle(0x2000, 0x0001);
}


void VfdController::reverse() {
    lastAction = "reverse";
    commandedRunning = true;
    communicationError = false;
    queueWriteSingle(0x2000, 0x0002);
}


void VfdController::stop() {
    lastAction = "stop";
    commandedRunning = false;
    communicationError = false;
    queueWriteSingle(0x2000, 0x0005);
}


void VfdController::setFrequency(float hz) {
    if (hz < 0.0f) {
        hz = 0.0f;
    }

    if (hz > 50.0f) {
        hz = 50.0f;
    }

    uint16_t value = (uint16_t)lroundf(hz * 100.0f);
    lastAction = "set frequency";
    requestedFrequencySet = true;
    requestedFrequencyHz = hz;
    communicationError = false;
    queueWriteSingle(REG_COMMAND_FREQUENCY, value);
}


void VfdController::pollStatus() {
    if (!initialized) {
        return;
    }

    if (pollFrequencyNext) {
        monitorFrequencyToken = queueReadHolding(REG_OPERATION_FREQUENCY, 2);
    } else {
        statusWordToken = queueReadHolding(REG_STATUS_WORD, 1);
    }

    pollFrequencyNext = !pollFrequencyNext;
}


void VfdController::readRegister(uint16_t address, uint16_t count) {
    lastAction = "read register";
    queueReadHolding(address, count);
}


void VfdController::writeRegister(uint16_t address, uint16_t value) {
    lastAction = "write register";
    queueWriteSingle(address, value);
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
    if (!requestInFlight) {
        return false;
    }

    if (millis() - requestStartedMs > REQUEST_TIMEOUT_GUARD_MS) {
        return false;
    }

    return true;
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


uint32_t VfdController::queueReadHolding(uint16_t address, uint16_t count) {
    if (isBusy()) {
        Logger::tracef(TAG_VFD, "Request skipped: Modbus busy token=%lu", (unsigned long)inFlightToken);
        return 0;
    }

    uint32_t token = nextToken();
    requestCount++;
    lastToken = token;

    Error error = client.addRequest(
        token,
        1,
        READ_HOLD_REGISTER,
        address,
        count
    );

    if (error != SUCCESS) {
        onError(error, token);
    } else {
        requestInFlight = true;
        inFlightToken = token;
        requestStartedMs = millis();
    }

    Logger::tracef(
        TAG_VFD,
        "Queued read token=%lu address=0x%04X count=%u",
        (unsigned long)token,
        address,
        count
    );

    return token;
}


uint32_t VfdController::queueWriteSingle(uint16_t address, uint16_t value) {
    if (isBusy()) {
        Logger::tracef(TAG_VFD, "Request skipped: Modbus busy token=%lu", (unsigned long)inFlightToken);
        return 0;
    }

    uint32_t token = nextToken();
    requestCount++;
    lastToken = token;

    Error error = client.addRequest(
        token,
        1,
        WRITE_HOLD_REGISTER,
        address,
        value
    );

    if (error != SUCCESS) {
        onError(error, token);
    } else {
        requestInFlight = true;
        inFlightToken = token;
        requestStartedMs = millis();
    }

    Logger::tracef(
        TAG_VFD,
        "Queued write token=%lu address=0x%04X value=0x%04X",
        (unsigned long)token,
        address,
        value
    );

    return token;
}


void VfdController::onData(ModbusMessage msg, uint32_t token) {
    if (token == inFlightToken) {
        requestInFlight = false;
    }

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
    ModbusError modbusError(error);

    if (isCrcNoise(error)) {
        if (token == inFlightToken) {
            requestInFlight = false;
        }

        crcErrorCount++;
        consecutiveErrorCount++;
        lastToken = token;
        lastErrorCode = (uint8_t)error;
        activitySeen = true;
        lastActivityMs = millis();
        if (consecutiveErrorCount >= 3) {
            communicationError = true;
        }

        Logger::tracef(
            TAG_VFD,
            "ERR token=%lu code=%02X (%s)",
            (unsigned long)token,
            (uint8_t)error,
            (const char*)modbusError
        );
        return;
    }

    if (token == inFlightToken) {
        requestInFlight = false;
    }

    errorCount++;
    consecutiveErrorCount++;
    lastToken = token;
    lastErrorCode = (uint8_t)error;
    activitySeen = true;
    if (consecutiveErrorCount >= 3 || (lastOkMs > 0 && millis() - lastOkMs > ONLINE_TIMEOUT_MS)) {
        communicationError = true;
    }
    lastActivityMs = millis();

    Logger::errorf(
        TAG_VFD,
        "ERR token=%lu code=%02X (%s)",
        (unsigned long)token,
        (uint8_t)error,
        (const char*)modbusError
    );
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
