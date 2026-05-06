#pragma once

#include <Arduino.h>
#include <IPAddress.h>

#include "TemperatureSensors.h"

struct AcStateSnapshot {
    bool bound = false;
    bool powerOn = false;
    uint8_t temperature = 0;
    uint8_t mode = 0;
    uint8_t fanMode = 0;
    bool primaryController = false;
    uint8_t controllerAddress = 0;
    bool seenPrimaryController = false;
    bool seenSecondaryController = false;
    bool updatePending = false;
    bool framePending = false;
    bool debugEnabled = false;
    bool hasReceivedFrame = false;
    unsigned long lastFrameAgeMs = 0;
    uint8_t updateFields = 0;
};

struct TemperatureStateSnapshot {
    uint8_t sensorCount = 0;
    float values[TEMP_MAX_SENSORS] = {};
};

struct InputStateSnapshot {
    bool ioExpanderReady = false;
    bool buttonBackPressed = false;
    bool buttonLeftPressed = false;
    bool buttonRightPressed = false;
    bool buttonOkPressed = false;
};

struct DisplayStateSnapshot {
    bool ready = false;
    uint8_t pageIndex = 0;
    const char* pageName = "";
};

struct DeviceState {
    bool wifiConnected = false;
    IPAddress ip;

    AcStateSnapshot ac;
    TemperatureStateSnapshot temperatures;
    InputStateSnapshot input;
    DisplayStateSnapshot display;

    unsigned long uptimeMs = 0;
};
