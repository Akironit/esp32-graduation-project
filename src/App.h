// App.h
#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "NetworkManager.h"
#include "Secrets.h"
#include "FujiHeatPump.h"
#include "SerialConsole.h"
#include "VfdController.h"
#include "TemperatureSensors.h"
#include "Mcp23017Expander.h"
#include "DisplayUi.h"
#include "ButtonInput.h"
#include "DeviceState.h"
#include "DeviceController.h"
#include "HomeAssistantBridge.h"

// -=-=-=-=-= Pin definitions and settings -=-=-=-=-=-
// AC LIN bus (Serial1)
#define AC_LIN_RX1_PIN  16
#define AC_LIN_TX1_PIN  17
#define AC_RESET_PIN    36
#define IS_SECONDARY_CONTROLLER true
#define AC_DEBUG false

// VFD RS485 bus (Serial2)
#define RS485_TX2_PIN   32
#define RS485_RX2_PIN   33
#define RS485_DERE_PIN  18
#define RS485_BAUD  19200

#define TEMP_ONE_WIRE_PIN 4

// I2C bus
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22
#define MCP23017_ADDRESS 0x20
#define MCP_INT_A_PIN 34
#define MCP_INT_B_PIN 35

// MCP23017 pins: GPA0-GPA7 are 0-7, GPB0-GPB7 are 8-15.
#define MCP_PIN_EXHAUST_VENT 4
#define MCP_PIN_GPA5 5
#define MCP_PIN_GPA6 6
#define MCP_PIN_GPA7 7

// UI buttons on MCP23017 port A, active LOW with internal pull-ups.
#define MCP_BUTTON_BACK_PIN 0
#define MCP_BUTTON_LEFT_PIN 1
#define MCP_BUTTON_RIGHT_PIN 2
#define MCP_BUTTON_OK_PIN 3


class App {
public:
    void begin();
    void update();

private:
    void updateHeatPump();
    void updateDeviceState();
    void updateVfdStatus();
    bool updateVfdCommandSync();
    void configureIoExpanderInputs();
    void updateIoExpanderInputs();
    void processIoExpanderPort();
    void handleIoExpanderInputChange(uint8_t pin, int currentState);
    void handleButtonEvent(const char* name, ButtonInput::Event event);
    void loadUserSettings();
    void saveUserSettings();
    void updateVentilationInputs(int gpa5State, int gpa6State, int gpa7State, int exhaustState);
    void requestVfdCommandSync(const char* reason);
    bool isVfdDesiredStateReached() const;
    float vfdStepToHz(uint8_t step) const;
    void logVfdStateChanges();

    FujiHeatPump hp;
    SerialConsole console;
    VfdController vfd{RS485_DERE_PIN};
    TemperatureSensors tempSensors;
    Mcp23017Expander ioExpander;
    DisplayUi display;
    DeviceState state;
    DeviceController controller;
    HomeAssistantBridge homeAssistant;
    Preferences preferences;
    bool ioExpanderReady = false;
    int lastGpa5State = HIGH;
    int lastGpa6State = HIGH;
    int lastGpa7State = HIGH;
    int lastExhaustVentState = HIGH;
    ButtonInput buttonBack;
    ButtonInput buttonLeft;
    ButtonInput buttonRight;
    ButtonInput buttonOk;
    bool buttonsActive = false;
    unsigned long lastButtonPollMs = 0;
    uint32_t lastUptimeSecond = UINT32_MAX;
    unsigned long lastVfdStatusPollMs = 0;
    unsigned long lastVfdCommandSyncMs = 0;
    bool vfdCommandSyncActive = false;
    uint16_t vfdCommandSyncAttempts = 0;
    bool lastLoggedVfdDesiredPower = false;
    uint8_t lastLoggedVfdDesiredStep = 255;
    bool lastLoggedVfdRunning = false;
    uint8_t lastLoggedVfdActualStep = 255;
    int16_t lastLoggedVfdActualFreq10 = INT16_MIN;
    bool lastLoggedVfdOnline = false;
    bool lastLoggedVfdError = false;
    NetworkManager network{
        WIFI_SSID,
        WIFI_PASSWORD,
        DEVICE_HOSTNAME,
        OTA_PASSWORD
    };
};
