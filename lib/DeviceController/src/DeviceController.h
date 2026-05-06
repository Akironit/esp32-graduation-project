#pragma once

#include <Arduino.h>

#include "DisplayUi.h"
#include "FujiHeatPump.h"
#include "TemperatureSensors.h"
#include "VfdController.h"

class DeviceController {
public:
    void begin(
        FujiHeatPump* heatPump,
        VfdController* vfd,
        DisplayUi* display,
        TemperatureSensors* temperatures = nullptr
    );

    bool setAcDebug(bool enabled);
    bool setAcPrimaryRole(bool primary);
    bool setAcPower(bool enabled);
    bool setAcTemperature(uint8_t temperature);
    bool setAcMode(uint8_t mode);
    bool setAcFanMode(uint8_t fanMode);

    bool vfdForward();
    bool vfdReverse();
    bool vfdStop();
    bool vfdSetFrequency(float hz);
    bool vfdReadRegister(uint16_t address, uint16_t count);
    bool vfdWriteRegister(uint16_t address, uint16_t value);

    bool displayNextPage();
    bool displayPreviousPage();
    bool displaySetPage(DisplayUi::Page page);

    bool temperatureForceRead();
    bool temperatureRescan();

    void restart(uint16_t delayMs = 100);

private:
    FujiHeatPump* heatPump = nullptr;
    VfdController* vfd = nullptr;
    DisplayUi* display = nullptr;
    TemperatureSensors* temperatures = nullptr;
};
