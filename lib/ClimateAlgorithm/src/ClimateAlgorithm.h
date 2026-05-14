#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "DeviceController.h"
#include "DeviceState.h"

struct AutoControlSettings {
    float targetTempC = 22.0f;
    float hysteresisC = 0.5f;
    float coolingStartDeltaC = 0.7f;
    float heatingStartDeltaC = 0.7f;
    float outdoorCoolingMarginC = 1.5f;
    float outdoorCoolingMaxC = 24.0f;
    float ventCoolMinDropC = 0.3f;
    uint8_t minVentStep = 1;
    uint8_t normalVentStep = 2;
    uint8_t coolingVentStep = 4;
    uint8_t maxVentStep = 6;
    uint8_t exhaustBoostStep = 1;
    uint8_t kitchenHoodBoostStep = 1;
    uint8_t acCoolTempOffsetC = 0;
    uint8_t acHeatTempOffsetC = 0;
    uint8_t acCoolFanMode = 2;
    uint8_t acHeatFanMode = 2;
    bool keepAcFanOnWithVent = true;
    bool keepAcFanOnInAuto = true;
    uint8_t acFanOnlyMode = 1;
    uint8_t acFanMinSpeed = 1;
    uint8_t acFanNormalSpeed = 2;
    uint8_t acFanBoostSpeed = 3;
    unsigned long decisionIntervalMs = 5000;
    unsigned long minStateHoldMs = 60000;
    unsigned long ventCoolTimeoutMs = 600000;
    bool allowAcCooling = true;
    bool allowAcHeating = true;
    bool allowVentCooling = true;
    bool dryRun = true;
};

struct AutoControlStatus {
    ControllerActivity activity = ControllerActivity::Idle;
    float indoorTempC = DEVICE_DISCONNECTED_C;
    float outdoorTempC = DEVICE_DISCONNECTED_C;
    float targetTempC = 22.0f;
    float deltaC = 0.0f;
    bool vfdDesiredPower = false;
    uint8_t vfdDesiredStep = 0;
    bool acDesiredPower = false;
    uint8_t acDesiredMode = 0;
    uint8_t acDesiredTemp = 0;
    uint8_t acDesiredFan = 0;
    char reason[96] = "Not evaluated";
    unsigned long lastDecisionMs = 0;
    unsigned long stateEnteredMs = 0;
    bool inputValid = false;
};

class ClimateAlgorithm {
public:
    void begin(DeviceState* state, DeviceController* controller);
    void update();

    void setSettings(const AutoControlSettings& settings);
    AutoControlSettings getSettings() const;
    AutoControlSettings defaults() const;
    void resetToDefaults();
    bool loadSettings();
    bool saveSettings();

    AutoControlStatus getStatus() const;
    const char* activityName(ControllerActivity activity) const;

private:
    DeviceState* state = nullptr;
    DeviceController* controller = nullptr;
    Preferences preferences;
    AutoControlSettings settings;
    AutoControlStatus status;
    ControllerActivity currentActivity = ControllerActivity::Idle;
    unsigned long lastDecisionMs = 0;
    unsigned long stateEnteredMs = 0;
    unsigned long ventCoolStartMs = 0;
    float ventCoolStartIndoorTempC = DEVICE_DISCONNECTED_C;
    bool hasDecision = false;

    ControllerActivity calculateActivity();
    ControllerActivity applyStateHold(ControllerActivity requested);
    void applyActivity(ControllerActivity activity);
    bool inputDataValid() const;
    bool indoorTemperatureValid() const;
    bool outdoorTemperatureValid() const;
    bool canUseVentCooling() const;
    bool canUseAcCooling() const;
    bool canUseAcHeating() const;
    uint8_t calculateVentStep(ControllerActivity activity) const;
    uint8_t calculateAcFanSpeedForVfdStep(uint8_t vfdStep) const;
    float vfdStepToHz(uint8_t step) const;
    uint8_t acTargetTemperature(int8_t offset) const;
    void setReason(const char* reason);
    void logDecision() const;
};
