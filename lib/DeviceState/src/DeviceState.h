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

struct VfdStateSnapshot {
    bool initialized = false;
    const char* lastAction = "none";
    bool hasRequestedFrequency = false;
    float requestedFrequencyHz = 0.0f;
    uint32_t requestCount = 0;
    uint32_t okCount = 0;
    uint32_t errorCount = 0;
    uint32_t lastToken = 0;
    uint8_t lastErrorCode = 0;
    bool hasActivity = false;
    unsigned long lastActivityAgeMs = 0;
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

struct HomeAssistantStateSnapshot {
    bool enabled = false;
    bool connected = false;
    uint32_t reconnectCount = 0;
    uint32_t publishCount = 0;
    uint32_t commandCount = 0;
    bool hasPublished = false;
    unsigned long lastPublishAgeMs = 0;
};

struct DeviceState {
    bool wifiConnected = false;
    IPAddress ip;

    AcStateSnapshot ac;
    TemperatureStateSnapshot temperatures;
    VfdStateSnapshot vfd;
    InputStateSnapshot input;
    DisplayStateSnapshot display;
    HomeAssistantStateSnapshot homeAssistant;

    unsigned long uptimeMs = 0;
};
