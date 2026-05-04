#include "Mcp23017Expander.h"

#include "Logger.h"

namespace {
constexpr const char* TAG_MCP = "MCP";

constexpr uint8_t REG_IODIRA = 0x00;
constexpr uint8_t REG_IODIRB = 0x01;
constexpr uint8_t REG_GPINTENA = 0x04;
constexpr uint8_t REG_GPINTENB = 0x05;
constexpr uint8_t REG_INTCONA = 0x08;
constexpr uint8_t REG_INTCONB = 0x09;
constexpr uint8_t REG_IOCONA = 0x0A;
constexpr uint8_t REG_IOCONB = 0x0B;
constexpr uint8_t REG_GPPUA = 0x0C;
constexpr uint8_t REG_GPPUB = 0x0D;
constexpr uint8_t REG_GPIOA = 0x12;
constexpr uint8_t REG_GPIOB = 0x13;
constexpr uint8_t REG_OLATA = 0x14;
constexpr uint8_t REG_OLATB = 0x15;

constexpr uint8_t IOCON_MIRROR = 0x40;
}


bool Mcp23017Expander::begin(
    TwoWire& wire,
    uint8_t address,
    int sdaPin,
    int sclPin,
    uint32_t clockHz
) {
    this->wire = &wire;
    this->address = address;

    wire.begin(sdaPin, sclPin);
    wire.setClock(clockHz);

    if (!isConnected()) {
        Logger::errorf(TAG_MCP, "MCP23017 not found at 0x%02X", address);
        return false;
    }

    // Safe default: all pins are inputs, pull-ups are disabled.
    writeRegister(REG_IODIRA, 0xFF);
    writeRegister(REG_IODIRB, 0xFF);
    writeRegister(REG_GPINTENA, 0x00);
    writeRegister(REG_GPINTENB, 0x00);
    writeRegister(REG_INTCONA, 0x00);
    writeRegister(REG_INTCONB, 0x00);
    writeRegister(REG_GPPUA, 0x00);
    writeRegister(REG_GPPUB, 0x00);
    writeRegister(REG_OLATA, 0x00);
    writeRegister(REG_OLATB, 0x00);

    Logger::infof(TAG_MCP, "MCP23017 initialized at 0x%02X", address);
    return true;
}


bool Mcp23017Expander::isConnected() {
    if (wire == nullptr) {
        return false;
    }

    wire->beginTransmission(address);
    return wire->endTransmission() == 0;
}


bool Mcp23017Expander::pinMode(uint8_t pin, uint8_t mode) {
    uint8_t regOffset = 0;
    uint8_t bit = 0;

    if (!splitPin(pin, regOffset, bit)) {
        return false;
    }

    const bool input = (mode == INPUT || mode == INPUT_PULLUP);
    const bool pullup = (mode == INPUT_PULLUP);

    if (!updateRegisterBit(REG_IODIRA + regOffset, bit, input)) {
        return false;
    }

    return updateRegisterBit(REG_GPPUA + regOffset, bit, pullup);
}


bool Mcp23017Expander::digitalWrite(uint8_t pin, uint8_t value) {
    uint8_t regOffset = 0;
    uint8_t bit = 0;

    if (!splitPin(pin, regOffset, bit)) {
        return false;
    }

    return updateRegisterBit(REG_OLATA + regOffset, bit, value == HIGH);
}


int Mcp23017Expander::digitalRead(uint8_t pin) {
    uint8_t regOffset = 0;
    uint8_t bit = 0;

    if (!splitPin(pin, regOffset, bit)) {
        return LOW;
    }

    uint8_t value = 0;

    if (!readRegister(REG_GPIOA + regOffset, value)) {
        return LOW;
    }

    return (value & (1 << bit)) ? HIGH : LOW;
}


bool Mcp23017Expander::configureInterruptOutputs(bool mirrorPins) {
    const uint8_t value = mirrorPins ? IOCON_MIRROR : 0x00;

    return writeRegister(REG_IOCONA, value)
        && writeRegister(REG_IOCONB, value);
}


bool Mcp23017Expander::enableInterruptOnChange(uint8_t pin) {
    uint8_t regOffset = 0;
    uint8_t bit = 0;

    if (!splitPin(pin, regOffset, bit)) {
        return false;
    }

    // INTCON bit 0 means "compare against previous value", i.e. any change.
    if (!updateRegisterBit(REG_INTCONA + regOffset, bit, false)) {
        return false;
    }

    return updateRegisterBit(REG_GPINTENA + regOffset, bit, true);
}


bool Mcp23017Expander::disableInterrupt(uint8_t pin) {
    uint8_t regOffset = 0;
    uint8_t bit = 0;

    if (!splitPin(pin, regOffset, bit)) {
        return false;
    }

    return updateRegisterBit(REG_GPINTENA + regOffset, bit, false);
}


bool Mcp23017Expander::clearInterrupts() {
    uint16_t ignored = 0;
    return readPort(ignored);
}


bool Mcp23017Expander::writePort(uint16_t value) {
    return writeRegister(REG_OLATA, value & 0xFF)
        && writeRegister(REG_OLATB, (value >> 8) & 0xFF);
}


bool Mcp23017Expander::readPort(uint16_t& value) {
    uint8_t portA = 0;
    uint8_t portB = 0;

    if (!readRegister(REG_GPIOA, portA) || !readRegister(REG_GPIOB, portB)) {
        return false;
    }

    value = ((uint16_t)portB << 8) | portA;
    return true;
}


bool Mcp23017Expander::writeRegister(uint8_t reg, uint8_t value) {
    if (wire == nullptr) {
        return false;
    }

    wire->beginTransmission(address);
    wire->write(reg);
    wire->write(value);
    return wire->endTransmission() == 0;
}


bool Mcp23017Expander::readRegister(uint8_t reg, uint8_t& value) {
    if (wire == nullptr) {
        return false;
    }

    wire->beginTransmission(address);
    wire->write(reg);

    if (wire->endTransmission(false) != 0) {
        return false;
    }

    if (wire->requestFrom(address, (uint8_t)1) != 1) {
        return false;
    }

    value = wire->read();
    return true;
}


bool Mcp23017Expander::updateRegisterBit(uint8_t reg, uint8_t bit, bool enabled) {
    uint8_t value = 0;

    if (!readRegister(reg, value)) {
        return false;
    }

    if (enabled) {
        value |= (1 << bit);
    } else {
        value &= ~(1 << bit);
    }

    return writeRegister(reg, value);
}


bool Mcp23017Expander::splitPin(uint8_t pin, uint8_t& regOffset, uint8_t& bit) {
    if (pin >= 16) {
        return false;
    }

    regOffset = pin / 8;
    bit = pin % 8;
    return true;
}
