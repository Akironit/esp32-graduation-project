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

    Logger::info(TAG_VFD, "Controller initialized");
}


void VfdController::forward() {
    queueWriteSingle(0x2000, 0x0001);
}


void VfdController::reverse() {
    queueWriteSingle(0x2000, 0x0002);
}


void VfdController::stop() {
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
    queueWriteSingle(0x2001, value);
}


void VfdController::readRegister(uint16_t address, uint16_t count) {
    queueReadHolding(address, count);
}


void VfdController::writeRegister(uint16_t address, uint16_t value) {
    queueWriteSingle(address, value);
}


uint32_t VfdController::nextToken() {
    return tokenCounter++;
}


void VfdController::queueReadHolding(uint16_t address, uint16_t count) {
    uint32_t token = nextToken();

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
    Logger::infof(
        TAG_VFD,
        "OK token=%lu server=%u FC=%u len=%u",
        (unsigned long)token,
        msg.getServerID(),
        msg.getFunctionCode(),
        (unsigned)msg.size()
    );

    Logger::raw("[VFD] Data: ");

    for (auto& byteValue : msg) {
        Logger::rawf("%02X ", byteValue);
    }

    Logger::raw("\n");
}


void VfdController::onError(Error error, uint32_t token) {
    ModbusError modbusError(error);

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
