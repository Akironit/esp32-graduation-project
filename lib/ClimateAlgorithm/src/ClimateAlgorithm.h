#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "AppConfig.h"
#include "DeviceController.h"
#include "DeviceState.h"

struct AutoControlSettings {
    bool autoEnabled = true;
    bool dryRun = true;
    float targetTempC = 22.5f;
    float coolingStartDeltaC = 0.7f;
    float heatingStartDeltaC = 0.7f;

    bool allowVentCooling = true;
    bool allowAcCooling = true;
    bool allowAcHeating = true;
    float outdoorCoolingMinDeltaC = 10.0f;
    float outdoorCoolingMaxTempC = 24.0f;

    bool autoVentAlwaysOn = true;
    uint8_t autoVentDefaultStep = 2;
    uint8_t autoVentMinStep = 1;
    uint8_t autoVentCoolingStep = 4;
    uint8_t autoVentMaxStep = 6;
    uint8_t bathExhaustCompStep = 1;
    uint8_t hoodCompStep1 = 1;
    uint8_t hoodCompStep2 = 2;
    uint8_t hoodCompStep3 = 3;
    bool additiveVentCompensation = false;
    float coldOutdoorTempLimitC = -5.0f;
    uint8_t coldOutdoorMaxVentStep = 2;

    bool keepAcFanOnInAuto = true;
    bool keepAcFanOnWithVent = true;
    uint8_t acFanOnlyMode = 1;
    uint8_t acFanMinSpeed = 1;
    uint8_t acFanNormalSpeed = 2;
    uint8_t acFanBoostSpeed = 3;
    uint8_t acFanMaxSpeed = 4;
    bool acFanAutoAllowed = true;
    bool acDynamicControlEnabled = true;
    float acCoolingFullPowerDeltaC = 3.0f;
    float acCoolingMinTempOffsetC = 1.0f;
    float acCoolingMaxTempOffsetC = 5.0f;
    float acCoolingMinSetpointC = 18.0f;
    uint8_t acCoolingMinFanSpeed = 2;
    uint8_t acCoolingMaxFanSpeed = 4;
    float acHeatingFullPowerDeltaC = 3.0f;
    float acHeatingMinTempOffsetC = 1.0f;
    float acHeatingMaxTempOffsetC = 4.0f;
    float acHeatingMaxSetpointC = 30.0f;
    uint8_t acHeatingMinFanSpeed = 2;
    uint8_t acHeatingMaxFanSpeed = 4;

    unsigned long decisionIntervalMs = 5000;
    unsigned long minStateHoldMs = 60000;
    unsigned long ventCoolingCheckIntervalMs = 5400000UL;
    float ventCoolingMinDropC = 0.3f;
    bool ventCoolingStepUpOnFail = true;
    bool ventCoolingFallbackToAc = true;

    bool safeOnIndoorSensorMissing = true;
    bool safeOnCriticalEquipmentError = true;
    bool diagnosticVerbose = false;

    unsigned long ventCompensationUpdateIntervalMs = 1000;
    unsigned long ventCompensationOffDelayMs = 10000;
    bool ventCompensationImmediateUp = true;
    bool ventCompensationImmediateDown = false;
};

struct AutoControlStatus {
    ControllerActivity activity = ControllerActivity::Idle;

    float indoorTempC = DEVICE_DISCONNECTED_C;
    bool indoorTempValid = false;
    float outdoorTempC = DEVICE_DISCONNECTED_C;
    bool outdoorTempValid = false;
    float targetTempC = 22.5f;
    float deltaTempC = 0.0f;
    bool needCooling = false;
    bool needHeating = false;
    bool ventCoolingAllowedNow = false;
    bool acCoolingAllowedNow = false;
    bool acHeatingAllowedNow = false;

    bool bathExhaustOn = false;
    uint8_t hoodLevel = 0;
    bool vfdOnline = false;
    bool acAvailable = false;

    uint8_t baseVentRequirementStep = 0;
    uint8_t bathCompStep = 0;
    uint8_t hoodCompStep = 0;
    uint8_t exhaustCompRequirementStep = 0;
    uint8_t coolingVentRequirementStep = 0;
    uint8_t requestedVentStepBeforeLimit = 0;
    uint8_t requestedVentStepAfterLimit = 0;
    bool coldOutdoorLimitActive = false;

    bool desiredVfdPower = false;
    uint8_t desiredVfdStep = 0;
    float desiredVfdHz = 0.0f;
    bool desiredAcPower = false;
    uint8_t desiredAcMode = 0;
    uint8_t desiredAcTargetTemp = 0;
    uint8_t desiredAcFanSpeed = 0;
    float acCoolingRatio = 0.0f;
    float acHeatingRatio = 0.0f;

    char reason[192] = "Not evaluated";
    char lastApplyResult[96] = "No command applied yet";
    char lastSkippedReason[96] = "No command skipped yet";
    unsigned long lastCommandMs = 0;
    unsigned long lastDecisionMs = 0;
    unsigned long lastVentCompensationMs = 0;
    unsigned long stateEnteredMs = 0;
    bool inputValid = false;
};

class ClimateAlgorithm {
public:
    void begin(DeviceState* state, DeviceController* controller);
    void update();

    void setSettings(const AutoControlSettings& settings, bool autosave = true);
    void setTargetTemp(float targetTempC);
    AutoControlSettings getSettings() const;
    AutoControlSettings defaults() const;
    void resetToDefaults();
    bool loadSettings();
    bool saveSettings();
    void markSettingsDirty();
    void processAutosave();

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
    unsigned long lastVentCompensationMs = 0;
    unsigned long stateEnteredMs = 0;
    unsigned long ventCoolingCheckStartMs = 0;
    float ventCoolingStartIndoorTempC = DEVICE_DISCONNECTED_C;
    uint8_t ventCoolingCurrentStep = 0;
    bool hasDecision = false;
    bool autoSafeModeActive = false;
    bool autoSettingsDirty = false;
    unsigned long lastSettingsChangeMs = 0;
    unsigned long lastSafeRetryMs = 0;
    bool lastAppliedAcPower = false;
    uint8_t lastAppliedAcMode = 255;
    uint8_t lastAppliedAcTemp = 255;
    uint8_t lastAppliedAcFan = 255;
    bool lastAppliedVfdPower = false;
    uint8_t lastAppliedVfdStep = 255;
    float lastAppliedVfdHz = -1.0f;
    bool hasAppliedAc = false;
    bool hasAppliedVfd = false;
    unsigned long lastAcCommandMs = 0;
    unsigned long lastVfdCommandMs = 0;
    uint8_t heldExhaustCompRequirementStep = 0;
    bool exhaustCompDecreasePending = false;
    unsigned long exhaustCompDecreaseStartedMs = 0;

    void refreshInputs();
    ControllerActivity calculateActivity();
    ControllerActivity applyStateHold(ControllerActivity requested);
    void applyActivity(ControllerActivity activity);
    void updateFastVentCompensation();
    void updateVentRequirements(ControllerActivity activity);
    void updateDesiredStateForActivity(ControllerActivity activity);
    void applyDesiredState();
    bool inputDataValid() const;
    bool indoorTemperatureValid() const;
    bool outdoorTemperatureValid() const;
    bool canUseVentCooling() const;
    bool canUseAcCooling() const;
    bool canUseAcHeating() const;
    uint8_t hoodCompensationStep(uint8_t hoodLevel) const;
    void calculateDynamicCoolingAc(uint8_t& targetTemp, uint8_t& fanSpeed);
    void calculateDynamicHeatingAc(uint8_t& targetTemp, uint8_t& fanSpeed);
    uint8_t calculateAcFanSpeedForVfdStep(uint8_t vfdStep) const;
    float vfdStepToHz(uint8_t step) const;
    uint8_t clampAcSetpoint(float value) const;
    void enterAutoSafeMode(const char* reason);
    void updateSafeRecovery();
    void resetVentCoolingCheck();
    void setReason(const char* reason);
    void setApplyResult(const char* result);
    void setSkippedReason(const char* reason);
    void logDecision() const;
};
