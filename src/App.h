// App.h
#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "AppConfig.h"
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
#include "ClimateAlgorithm.h"

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
// Recommended GD20 Modbus settings:
// P14.00 = 1 (slave address)
// P14.01 = 4 for 19200 baud, or 3 for 9600 baud during diagnostics
// P14.02 = 1 for 8E1
// P14.03 = 30 ms, or 50 ms if first-byte / wrong-slave errors appear
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
    void updateDiagnostics(const AutoControlStatus& autoStatus, const AutoControlSettings& autoSettings);
    void updateVfdStatus();
    bool updateVfdCommandSync();
    void configureIoExpanderInputs();
    void updateIoExpanderInputs();
    void processIoExpanderPort();
    void handleIoExpanderInputChange(uint8_t pin, int currentState);
    void handleButtonEvent(const char* name, ButtonInput::Event event);
    void loadUserSettings();
    void scheduleUserSettingsSave();
    void updateDeferredSettingsSave();
    void saveUserSettings();
    void updateModeTransition();
    void applyManualSettingsProfile(const char* reason);
    void updateVentilationInputs(uint8_t hoodLevel, int exhaustState);
    void startVfdFastVerify();
    void requestVfdCommandSync(const char* reason);
    void requestVfdCommandSync(const char* reason, uint16_t address, uint16_t value, bool desiredPower, uint8_t desiredStep, float desiredHz);
    void syncManualVfdEffectiveTarget();
    bool sendPendingVfdCommand(const char* source);
    bool isPendingVfdStatusVerified() const;
    bool isVfdDesiredStateReached() const;
    float vfdStepToHz(uint8_t step) const;
    void logVfdStateChanges();
    static bool handleHomeAssistantVfdSync(void* context, const char* reason);
    static void handleHomeAssistantSettingsChanged(void* context);
    static void handleHomeAssistantReboot(void* context);

    struct VfdEffectiveTarget {
        bool manualVentAssistEnabled = false;
        bool userPower = false;
        uint8_t userStep = 0;
        uint8_t ventStep = 0;
        uint8_t effectiveStep = 0;
        bool effectivePower = false;
        float effectiveHz = 0.0f;
    };
    VfdEffectiveTarget getManualVfdEffectiveTarget() const;

    enum class VfdSyncState : uint8_t {
        Idle,
        WaitingWriteAck,
        WaitingStatusVerify,
        Failed
    };

    FujiHeatPump hp;
    SerialConsole console;
    VfdController vfd{RS485_DERE_PIN};
    TemperatureSensors tempSensors;
    Mcp23017Expander ioExpander;
    DisplayUi display;
    DeviceState state;
    DeviceController controller;
    HomeAssistantBridge homeAssistant;
    ClimateAlgorithm climateAlgorithm;
    Preferences preferences;
    bool ioExpanderReady = false;
    int lastGpa5State = HIGH;
    int lastGpa6State = HIGH;
    int lastGpa7State = HIGH;
    int lastExhaustVentState = HIGH;
    uint8_t rawHoodLevel = 0;
    uint8_t stableHoodLevel = 0;
    unsigned long rawHoodChangedMs = 0;
    bool hoodDebounceActive = false;
    ButtonInput buttonBack;
    ButtonInput buttonLeft;
    ButtonInput buttonRight;
    ButtonInput buttonOk;
    bool buttonsActive = false;
    unsigned long lastButtonPollMs = 0;
    uint32_t lastUptimeSecond = UINT32_MAX;
    unsigned long lastVfdStatusPollMs = 0;
    unsigned long lastVfdCommandSyncMs = 0;
    bool vfdFastVerifyActive = false;
    unsigned long vfdFastVerifyStartedMs = 0;
    unsigned long lastVfdFastVerifyPollMs = 0;
    unsigned long lastObservedVfdWriteAckMs = 0;
    VfdSyncState vfdSyncState = VfdSyncState::Idle;
    uint16_t pendingVfdAddress = 0;
    uint16_t pendingVfdValue = 0;
    unsigned long pendingVfdStartedMs = 0;
    unsigned long pendingVfdLastSendMs = 0;
    uint8_t pendingVfdRetryCount = 0;
    bool pendingVfdDesiredPower = false;
    uint8_t pendingVfdDesiredStep = 0;
    float pendingVfdDesiredHz = 0.0f;
    int rawExhaustVentState = HIGH;
    unsigned long rawExhaustVentChangedMs = 0;
    bool exhaustDebounceActive = false;
    bool settingsDirty = false;
    unsigned long lastSettingsChangeMs = 0;
    bool modeTransitionInitialized = false;
    DeviceMode lastControllerMode = DeviceMode::Auto;
    bool lastMqttEnabled = MQTT_ENABLED;
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
