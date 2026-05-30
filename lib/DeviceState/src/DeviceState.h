#pragma once

#include <Arduino.h>
#include <IPAddress.h>

#include "TemperatureSensors.h"

struct AcStateSnapshot {
    bool bound = false;
    bool powerOn = false;
    uint8_t temperature = 0;
    uint8_t controllerTemp = 0;
    uint8_t mode = 0;
    uint8_t fanMode = 0;
    bool primaryController = false;
    uint8_t controllerAddress = 0;
    bool seenPrimaryController = false;
    bool seenSecondaryController = false;
    bool updatePending = false;
    bool framePending = false;
    bool debugEnabled = false;
    bool hasAnyFrame = false;
    unsigned long lastAnyFrameAgeMs = 0;
    uint8_t lastFrameSourceAddress = 0;
    uint8_t lastFrameDestinationAddress = 0;
    uint8_t lastFrameType = 0;
    bool hasReceivedFrame = false;
    unsigned long lastFrameAgeMs = 0;
    bool communicationError = false;
    uint8_t consecutiveErrorCount = 0;
    uint32_t errorCount = 0;
    uint8_t updateFields = 0;
};

enum class DiagnosticSeverity : uint8_t {
    Info = 0,
    Warning,
    Error
};

enum class DiagnosticCode : uint8_t {
    AcLinkLost = 0,
    AcBusActiveButDeviceNotAddressed,
    CoolingRequiredButUnavailable,
    HeatingRequiredButUnavailable,
    VfdLinkLost,
    VfdCommunicationUnstable,
    IndoorSensorMissing,
    OutdoorSensorMissing,
    WifiDisconnected,
    HomeAssistantDisconnected,
    UnassignedTemperatureSensorsDetected,
    AutoSafeModeActive,
    SettingsInvalid
};

struct DiagnosticItem {
    DiagnosticSeverity severity = DiagnosticSeverity::Info;
    DiagnosticCode code = DiagnosticCode::SettingsInvalid;
    bool active = false;
    char title[32] = "";
    char details[64] = "";
    char recommendation[64] = "";
    uint32_t firstSeenMs = 0;
    uint32_t lastSeenMs = 0;
};

static constexpr uint8_t MAX_DIAGNOSTIC_ITEMS = 16;

struct DiagnosticsSnapshot {
    DiagnosticItem items[MAX_DIAGNOSTIC_ITEMS];
    uint8_t itemCount = 0;
    uint8_t warningCount = 0;
    uint8_t errorCount = 0;
    uint32_t updatedAtMs = 0;
};

struct TemperatureStateSnapshot {
    struct Sensor {
        DeviceAddress address = {};
        TempSensorRole role = TempSensorRole::Unknown;
        bool enabled = true;
        bool connected = false;
        bool hasTemperature = false;
        float temperatureC = DEVICE_DISCONNECTED_C;
        uint8_t missedScanCount = 0;
        uint8_t failedReadCount = 0;
    };

    uint8_t sensorCount = 0;
    float values[TEMP_MAX_SENSORS] = {};
    Sensor sensors[TEMP_MAX_SENSORS];
};

struct VfdStateSnapshot {
    bool initialized = false;
    bool online = false;
    bool everOnline = false;
    bool communicationError = false;
    bool hasStatusWord = false;
    uint16_t statusWord = 0;
    bool running = false;
    bool commandedRunning = false;
    const char* lastAction = "none";
    bool hasRequestedFrequency = false;
    float requestedFrequencyHz = 0.0f;
    bool hasActualFrequency = false;
    float actualFrequencyHz = 0.0f;
    uint8_t actualStep = 0;
    uint32_t requestCount = 0;
    uint32_t okCount = 0;
    uint32_t errorCount = 0;
    uint8_t consecutiveErrorCount = 0;
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

struct VentilationStateSnapshot {
    uint8_t baseRequirementStep = 0;
    uint8_t bathCompStep = 0;
    uint8_t hoodCompStep = 0;
    uint8_t exhaustCompRequirementStep = 0;
    uint8_t coolingRequirementStep = 0;
    uint8_t requestedStepBeforeLimit = 0;
    uint8_t requestedStepAfterLimit = 0;
    uint8_t desiredVfdStep = 0;
    bool desiredVfdPower = false;
    bool coldOutdoorLimitActive = false;
    uint32_t compensationUpdateIntervalSec = 0;
    uint32_t compensationUpdateRemainingSec = 0;
    uint32_t compensationOffDelaySec = 0;
    uint32_t compensationOffDelayRemainingSec = 0;
    bool compensationOffDelayActive = false;
    bool additiveCompensation = false;
    char reason[96] = "Not evaluated";
};

enum class DeviceMode : uint8_t {
    Auto = 0,
    Manual,
    Safe,
    Disabled
};

enum class ControllerActivity : uint8_t {
    Start = 0,
    Normal,
    VentCool,
    AcCool,
    Heat,
    Vent,
    Error,
    Hold,
    Idle,
};

struct ControllerStateSnapshot {
    DeviceMode mode = DeviceMode::Auto;
    ControllerActivity activity = ControllerActivity::Normal;
    uint8_t warningCount = 0;
    uint8_t errorCount = 0;
};

struct EnvironmentStateSnapshot {
    bool hasIndoorTemp = false;
    bool hasOutdoorTemp = false;
    float indoorTempC = DEVICE_DISCONNECTED_C;
    float outdoorTempC = DEVICE_DISCONNECTED_C;
    float targetIndoorTempC = 22.0f;
    float targetToleranceC = 1.0f;
    float coolingStartDeltaC = 0.7f;
    float heatingStartDeltaC = 0.7f;
    uint8_t kitchenHoodLevel = 0;
    bool exhaustVentEnabled = false;
};

struct UserSettingsSnapshot {
    DeviceMode mode = DeviceMode::Auto;
    bool wifiEnabled = true;
    bool mqttEnabled = true;
    bool autoSaveEnabled = true;
    bool vfdPollingEnabled = true;
    float targetIndoorTempC = 22.0f;
    bool manualAcPower = false;
    uint8_t manualAcMode = 5;
    uint8_t manualAcTemperature = 22;
    uint8_t manualAcModeTemperatures[6] = {22, 22, 22, 22, 22, 22};
    uint8_t manualAcFanMode = 0;
    bool manualVfdPower = false;
    uint8_t manualVfdStep = 0;
};

struct DeviceState {
    bool wifiConnected = false;
    IPAddress ip;

    ControllerStateSnapshot controllerState;
    EnvironmentStateSnapshot environment;
    UserSettingsSnapshot settings;
    AcStateSnapshot ac;
    TemperatureStateSnapshot temperatures;
    VfdStateSnapshot vfd;
    InputStateSnapshot input;
    DisplayStateSnapshot display;
    HomeAssistantStateSnapshot homeAssistant;
    VentilationStateSnapshot ventilation;
    DiagnosticsSnapshot diagnostics;

    unsigned long uptimeMs = 0;
    uint32_t uptimeSeconds = 0;
    uint32_t uptimeHours = 0;
    uint8_t uptimeMinutes = 0;
    uint8_t uptimeSecondPart = 0;
    char uptimeText[16] = "00:00:00";
};
