// VfdController.cpp
#include "VfdController.h"

#include "Logger.h"

namespace {
constexpr const char* TAG_VFD = "VFD";
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
    queueWriteSingle(0x2000, 0x0001);
}


void VfdController::reverse() {
    lastAction = "reverse";
    queueWriteSingle(0x2000, 0x0002);
}


void VfdController::stop() {
    lastAction = "stop";
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
    queueWriteSingle(0x2001, value);
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


const char* VfdController::getLastAction() const {
    return lastAction;
}


bool VfdController::hasRequestedFrequency() const {
    return requestedFrequencySet;
}


float VfdController::getRequestedFrequencyHz() const {
    return requestedFrequencyHz;
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


uint32_t VfdController::nextToken() {
    return tokenCounter++;
}


void VfdController::queueReadHolding(uint16_t address, uint16_t count) {
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
    }
}


void VfdController::queueWriteSingle(uint16_t address, uint16_t value) {
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
    }
}


void VfdController::onData(ModbusMessage msg, uint32_t token) {
    okCount++;
    lastToken = token;
    activitySeen = true;
    lastActivityMs = millis();

    Logger::infof(
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

    Logger::debugf(TAG_VFD, "Data: %s", data);
}


void VfdController::onError(Error error, uint32_t token) {
    ModbusError modbusError(error);
    errorCount++;
    lastToken = token;
    lastErrorCode = (uint8_t)error;
    activitySeen = true;
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
