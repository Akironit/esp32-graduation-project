#pragma once

#include <Arduino.h>
#include <Wire.h>

class Mcp23017Expander {
public:
    bool begin(
        TwoWire& wire,
        uint8_t address,
        int sdaPin,
        int sclPin,
        uint32_t clockHz = 100000
    );

    bool isConnected();

    bool pinMode(uint8_t pin, uint8_t mode);
    bool digitalWrite(uint8_t pin, uint8_t value);
    int digitalRead(uint8_t pin);

    bool writePort(uint16_t value);
    bool readPort(uint16_t& value);

private:
    TwoWire* wire = nullptr;
    uint8_t address = 0x20;

    bool writeRegister(uint8_t reg, uint8_t value);
    bool readRegister(uint8_t reg, uint8_t& value);
    bool updateRegisterBit(uint8_t reg, uint8_t bit, bool enabled);

    static bool splitPin(uint8_t pin, uint8_t& regOffset, uint8_t& bit);
};
