#include "ClimateAlgorithm.h"

#include <math.h>

#include "Logger.h"

namespace {
constexpr const char* TAG_AUTO = "AUTO";
constexpr const char* AUTO_NVS_NAMESPACE = "autoctrl";

uint8_t clampStep(uint8_t value) {
    return value > 6 ? 6 : value;
}

uint8_t clampAcFan(uint8_t value, bool autoAllowed) {
    if (!autoAllowed && value == 0) {
        return 1;
    }
    return value > 4 ? 4 : value;
}

float clampFloat(float value, float minValue, float maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}
}

void ClimateAlgorithm::begin(DeviceState* state, DeviceController* controller) {
    this->state = state;
    this->controller = controller;
    loadSettings();

    if (this->state != nullptr) {
        this->state->environment.targetIndoorTempC = settings.targetTempC;
        this->state->settings.targetIndoorTempC = settings.targetTempC;
    }

    currentActivity = ControllerActivity::Idle;
    status.activity = currentActivity;
    stateEnteredMs = millis();
    status.stateEnteredMs = stateEnteredMs;
    Logger::info(TAG_AUTO, "Climate algorithm initialized");
}

void ClimateAlgorithm::update() {
    if (state == nullptr || controller == nullptr) {
        return;
    }

    processAutosave();
    refreshInputs();

    if (state->controllerState.mode == DeviceMode::Disabled) {
        state->controllerState.activity = ControllerActivity::Idle;
        status.activity = ControllerActivity::Idle;
        status.lastDecisionMs = millis();
        setReason("Disabled monitoring mode active");
        resetVentCoolingCheck();
        return;
    }

    if (state->controllerState.mode == DeviceMode::Manual) {
        state->controllerState.activity = ControllerActivity::Hold;
        status.activity = ControllerActivity::Hold;
        status.lastDecisionMs = millis();
        setReason("Manual mode active");
        resetVentCoolingCheck();
        return;
    }

    if (state->controllerState.mode == DeviceMode::Safe) {
        updateSafeRecovery();
        if (state->controllerState.mode == DeviceMode::Safe) {
            state->controllerState.activity = ControllerActivity::Error;
            status.activity = ControllerActivity::Error;
            status.lastDecisionMs = millis();
            return;
        }
    }

    if (!settings.autoEnabled) {
        state->controllerState.activity = ControllerActivity::Idle;
        status.activity = ControllerActivity::Idle;
        status.lastDecisionMs = millis();
        setReason("Automatic algorithm is disabled by autoEnabled=0");
        resetVentCoolingCheck();
        return;
    }

    if (settings.safeOnIndoorSensorMissing && !indoorTemperatureValid()) {
        enterAutoSafeMode("Indoor temperature is missing or invalid");
        state->controllerState.activity = ControllerActivity::Error;
        status.activity = ControllerActivity::Error;
        return;
    }

    updateFastVentCompensation();

    const unsigned long now = millis();
    if (now - lastDecisionMs < settings.decisionIntervalMs) {
        return;
    }

    lastDecisionMs = now;
    ControllerActivity requested = calculateActivity();
    requested = applyStateHold(requested);
    applyActivity(requested);
    logDecision();
}

void ClimateAlgorithm::setSettings(const AutoControlSettings& newSettings, bool autosave) {
    const float oldTarget = settings.targetTempC;
    settings = newSettings;
    settings.targetTempC = clampFloat(settings.targetTempC, 16.0f, 30.0f);
    settings.coolingStartDeltaC = clampFloat(settings.coolingStartDeltaC, 0.1f, 5.0f);
    settings.heatingStartDeltaC = clampFloat(settings.heatingStartDeltaC, 0.1f, 5.0f);
    settings.outdoorCoolingMinDeltaC = clampFloat(settings.outdoorCoolingMinDeltaC, 1.0f, 20.0f);
    settings.outdoorCoolingMaxTempC = clampFloat(settings.outdoorCoolingMaxTempC, 5.0f, 35.0f);
    settings.autoVentDefaultStep = clampStep(settings.autoVentDefaultStep);
    settings.autoVentMinStep = clampStep(settings.autoVentMinStep);
    settings.autoVentCoolingStep = clampStep(settings.autoVentCoolingStep);
    settings.autoVentMaxStep = clampStep(settings.autoVentMaxStep);
    settings.bathExhaustCompStep = clampStep(settings.bathExhaustCompStep);
    settings.hoodCompStep1 = clampStep(settings.hoodCompStep1);
    settings.hoodCompStep2 = clampStep(settings.hoodCompStep2);
    settings.hoodCompStep3 = clampStep(settings.hoodCompStep3);
    settings.coldOutdoorTempLimitC = clampFloat(settings.coldOutdoorTempLimitC, -30.0f, 10.0f);
    settings.coldOutdoorMaxVentStep = clampStep(settings.coldOutdoorMaxVentStep);
    settings.acFanMinSpeed = clampAcFan(settings.acFanMinSpeed, settings.acFanAutoAllowed);
    settings.acFanNormalSpeed = clampAcFan(settings.acFanNormalSpeed, settings.acFanAutoAllowed);
    settings.acFanBoostSpeed = clampAcFan(settings.acFanBoostSpeed, settings.acFanAutoAllowed);
    settings.acFanMaxSpeed = clampAcFan(settings.acFanMaxSpeed, settings.acFanAutoAllowed);
    settings.acCoolingFullPowerDeltaC = clampFloat(settings.acCoolingFullPowerDeltaC, 0.1f, 10.0f);
    settings.acCoolingMinTempOffsetC = clampFloat(settings.acCoolingMinTempOffsetC, 0.0f, 10.0f);
    settings.acCoolingMaxTempOffsetC = clampFloat(settings.acCoolingMaxTempOffsetC, settings.acCoolingMinTempOffsetC, 10.0f);
    settings.acCoolingMinSetpointC = clampFloat(settings.acCoolingMinSetpointC, 16.0f, 30.0f);
    settings.acCoolingMinFanSpeed = clampAcFan(settings.acCoolingMinFanSpeed, settings.acFanAutoAllowed);
    settings.acCoolingMaxFanSpeed = clampAcFan(settings.acCoolingMaxFanSpeed, settings.acFanAutoAllowed);
    if (settings.acCoolingMaxFanSpeed < settings.acCoolingMinFanSpeed) {
        settings.acCoolingMaxFanSpeed = settings.acCoolingMinFanSpeed;
    }
    settings.acHeatingFullPowerDeltaC = clampFloat(settings.acHeatingFullPowerDeltaC, 0.1f, 10.0f);
    settings.acHeatingMinTempOffsetC = clampFloat(settings.acHeatingMinTempOffsetC, 0.0f, 10.0f);
    settings.acHeatingMaxTempOffsetC = clampFloat(settings.acHeatingMaxTempOffsetC, settings.acHeatingMinTempOffsetC, 10.0f);
    settings.acHeatingMaxSetpointC = clampFloat(settings.acHeatingMaxSetpointC, 16.0f, 30.0f);
    settings.acHeatingMinFanSpeed = clampAcFan(settings.acHeatingMinFanSpeed, settings.acFanAutoAllowed);
    settings.acHeatingMaxFanSpeed = clampAcFan(settings.acHeatingMaxFanSpeed, settings.acFanAutoAllowed);
    if (settings.acHeatingMaxFanSpeed < settings.acHeatingMinFanSpeed) {
        settings.acHeatingMaxFanSpeed = settings.acHeatingMinFanSpeed;
    }
    if (settings.acFanOnlyMode < 1 || settings.acFanOnlyMode > 5) {
        settings.acFanOnlyMode = 1;
    }
    settings.decisionIntervalMs = constrain(settings.decisionIntervalMs, 1000UL, 60000UL);
    settings.minStateHoldMs = min(settings.minStateHoldMs, 3600000UL);
    settings.ventCoolingCheckIntervalMs = constrain(settings.ventCoolingCheckIntervalMs, 60000UL, 21600000UL);
    settings.ventCoolingMinDropC = clampFloat(settings.ventCoolingMinDropC, 0.0f, 5.0f);
    settings.ventCompensationUpdateIntervalMs = constrain(settings.ventCompensationUpdateIntervalMs, 500UL, 10000UL);
    settings.ventCompensationOffDelayMs = min(settings.ventCompensationOffDelayMs, 300000UL);

    if (fabsf(oldTarget - settings.targetTempC) > 0.01f) {
        resetVentCoolingCheck();
    }

    if (state != nullptr) {
        state->environment.targetIndoorTempC = settings.targetTempC;
        state->environment.coolingStartDeltaC = settings.coolingStartDeltaC;
        state->environment.heatingStartDeltaC = settings.heatingStartDeltaC;
        state->settings.targetIndoorTempC = settings.targetTempC;
    }

    if (autosave) {
        markSettingsDirty();
    }
}

void ClimateAlgorithm::setTargetTemp(float targetTempC) {
    AutoControlSettings updated = settings;
    updated.targetTempC = targetTempC;
    setSettings(updated);
}

AutoControlSettings ClimateAlgorithm::getSettings() const {
    return settings;
}

AutoControlSettings ClimateAlgorithm::defaults() const {
    return AutoControlSettings{};
}

void ClimateAlgorithm::resetToDefaults() {
    setSettings(defaults());
    hasDecision = false;
    currentActivity = ControllerActivity::Idle;
    stateEnteredMs = millis();
    resetVentCoolingCheck();
}

bool ClimateAlgorithm::loadSettings() {
    if (!preferences.begin(AUTO_NVS_NAMESPACE, true)) {
        Logger::warning(TAG_AUTO, "Failed to open auto control settings for reading");
        return false;
    }

    AutoControlSettings loaded = settings;
    loaded.autoEnabled = preferences.getBool("autoEnabled", loaded.autoEnabled);
    loaded.dryRun = preferences.getBool("dryRun", loaded.dryRun);
    loaded.targetTempC = preferences.getFloat("targetTempC", preferences.getFloat("target", loaded.targetTempC));
    loaded.coolingStartDeltaC = preferences.getFloat("coolStart", preferences.getFloat("coolDelta", loaded.coolingStartDeltaC));
    loaded.heatingStartDeltaC = preferences.getFloat("heatStart", preferences.getFloat("heatDelta", loaded.heatingStartDeltaC));
    loaded.allowVentCooling = preferences.getBool("allowVentCool", preferences.getBool("ventCool", loaded.allowVentCooling));
    loaded.allowAcCooling = preferences.getBool("allowAcCool", preferences.getBool("acCool", loaded.allowAcCooling));
    loaded.allowAcHeating = preferences.getBool("allowAcHeat", preferences.getBool("acHeat", loaded.allowAcHeating));
    loaded.outdoorCoolingMinDeltaC = preferences.getFloat("outCoolMinD", preferences.getFloat("outMargin", loaded.outdoorCoolingMinDeltaC));
    loaded.outdoorCoolingMaxTempC = preferences.getFloat("outCoolMaxT", preferences.getFloat("outMax", loaded.outdoorCoolingMaxTempC));
    loaded.autoVentAlwaysOn = preferences.getBool("ventAlways", loaded.autoVentAlwaysOn);
    loaded.autoVentDefaultStep = preferences.getUChar("ventDefault", preferences.getUChar("normStep", loaded.autoVentDefaultStep));
    loaded.autoVentMinStep = preferences.getUChar("ventMin", preferences.getUChar("minStep", loaded.autoVentMinStep));
    loaded.autoVentCoolingStep = preferences.getUChar("ventCoolStep", preferences.getUChar("coolStep", loaded.autoVentCoolingStep));
    loaded.autoVentMaxStep = preferences.getUChar("ventMax", preferences.getUChar("maxStep", loaded.autoVentMaxStep));
    loaded.bathExhaustCompStep = preferences.getUChar("bathComp", preferences.getUChar("exhBoost", loaded.bathExhaustCompStep));
    loaded.hoodCompStep1 = preferences.getUChar("hoodComp1", loaded.hoodCompStep1);
    loaded.hoodCompStep2 = preferences.getUChar("hoodComp2", loaded.hoodCompStep2);
    loaded.hoodCompStep3 = preferences.getUChar("hoodComp3", preferences.getUChar("hoodBoost", loaded.hoodCompStep3));
    loaded.additiveVentCompensation = preferences.getBool("ventAdditive", loaded.additiveVentCompensation);
    loaded.coldOutdoorTempLimitC = preferences.getFloat("coldLimit", loaded.coldOutdoorTempLimitC);
    loaded.coldOutdoorMaxVentStep = preferences.getUChar("coldMaxStep", loaded.coldOutdoorMaxVentStep);
    loaded.keepAcFanOnInAuto = preferences.getBool("fanInAuto", loaded.keepAcFanOnInAuto);
    loaded.keepAcFanOnWithVent = preferences.getBool("fanWithVent", loaded.keepAcFanOnWithVent);
    loaded.acFanOnlyMode = preferences.getUChar("fanOnlyMode", loaded.acFanOnlyMode);
    loaded.acFanMinSpeed = preferences.getUChar("fanMin", loaded.acFanMinSpeed);
    loaded.acFanNormalSpeed = preferences.getUChar("fanNormal", loaded.acFanNormalSpeed);
    loaded.acFanBoostSpeed = preferences.getUChar("fanBoost", loaded.acFanBoostSpeed);
    loaded.acFanMaxSpeed = preferences.getUChar("fanMax", loaded.acFanMaxSpeed);
    loaded.acFanAutoAllowed = preferences.getBool("fanAuto", loaded.acFanAutoAllowed);
    loaded.acDynamicControlEnabled = preferences.getBool("acDynamic", loaded.acDynamicControlEnabled);
    loaded.acCoolingFullPowerDeltaC = preferences.getFloat("acCoolFullD", loaded.acCoolingFullPowerDeltaC);
    loaded.acCoolingMinTempOffsetC = preferences.getFloat("acCoolMinOff", loaded.acCoolingMinTempOffsetC);
    loaded.acCoolingMaxTempOffsetC = preferences.getFloat("acCoolMaxOff", loaded.acCoolingMaxTempOffsetC);
    loaded.acCoolingMinSetpointC = preferences.getFloat("acCoolMinT", loaded.acCoolingMinSetpointC);
    loaded.acCoolingMinFanSpeed = preferences.getUChar("acCoolFanMin", preferences.getUChar("coolFanSpeed", loaded.acCoolingMinFanSpeed));
    loaded.acCoolingMaxFanSpeed = preferences.getUChar("acCoolFanMax", loaded.acCoolingMaxFanSpeed);
    loaded.acHeatingFullPowerDeltaC = preferences.getFloat("acHeatFullD", loaded.acHeatingFullPowerDeltaC);
    loaded.acHeatingMinTempOffsetC = preferences.getFloat("acHeatMinOff", loaded.acHeatingMinTempOffsetC);
    loaded.acHeatingMaxTempOffsetC = preferences.getFloat("acHeatMaxOff", loaded.acHeatingMaxTempOffsetC);
    loaded.acHeatingMaxSetpointC = preferences.getFloat("acHeatMaxT", loaded.acHeatingMaxSetpointC);
    loaded.acHeatingMinFanSpeed = preferences.getUChar("acHeatFanMin", preferences.getUChar("heatFanSpeed", loaded.acHeatingMinFanSpeed));
    loaded.acHeatingMaxFanSpeed = preferences.getUChar("acHeatFanMax", loaded.acHeatingMaxFanSpeed);
    loaded.decisionIntervalMs = preferences.getULong("decisionMs", preferences.getULong("interval", loaded.decisionIntervalMs));
    loaded.minStateHoldMs = preferences.getULong("holdMs", preferences.getULong("hold", loaded.minStateHoldMs));
    loaded.ventCoolingCheckIntervalMs = preferences.getULong("ventChkMs", preferences.getULong("ventTimeout", loaded.ventCoolingCheckIntervalMs));
    loaded.ventCoolingMinDropC = preferences.getFloat("ventMinDrop", preferences.getFloat("ventDrop", loaded.ventCoolingMinDropC));
    loaded.ventCoolingStepUpOnFail = preferences.getBool("ventStepFail", loaded.ventCoolingStepUpOnFail);
    loaded.ventCoolingFallbackToAc = preferences.getBool("ventFbAc", loaded.ventCoolingFallbackToAc);
    loaded.safeOnIndoorSensorMissing = preferences.getBool("safeNoIndoor", loaded.safeOnIndoorSensorMissing);
    loaded.safeOnCriticalEquipmentError = preferences.getBool("safeEquip", loaded.safeOnCriticalEquipmentError);
    loaded.diagnosticVerbose = preferences.getBool("diagVerbose", preferences.getBool("autoLog", loaded.diagnosticVerbose));
    loaded.ventCompensationUpdateIntervalMs = preferences.getULong("ventCompMs", loaded.ventCompensationUpdateIntervalMs);
    loaded.ventCompensationOffDelayMs = preferences.getULong("ventOffDelay", loaded.ventCompensationOffDelayMs);
    loaded.ventCompensationImmediateUp = preferences.getBool("ventImmUp", loaded.ventCompensationImmediateUp);
    loaded.ventCompensationImmediateDown = preferences.getBool("ventImmDown", loaded.ventCompensationImmediateDown);
    preferences.end();

    setSettings(loaded, false);
    autoSettingsDirty = false;
    Logger::info(TAG_AUTO, "Auto control settings loaded");
    return true;
}

bool ClimateAlgorithm::saveSettings() {
    if (!preferences.begin(AUTO_NVS_NAMESPACE, false)) {
        Logger::warning(TAG_AUTO, "Failed to open auto control settings for writing");
        return false;
    }

    uint16_t changedCount = 0;
    auto putBoolChanged = [&](const char* key, bool value) {
        if (!preferences.isKey(key) || preferences.getBool(key, !value) != value) {
            preferences.putBool(key, value);
            changedCount++;
        }
    };
    auto putUCharChanged = [&](const char* key, uint8_t value) {
        if (!preferences.isKey(key) || preferences.getUChar(key, value == 0 ? 1 : 0) != value) {
            preferences.putUChar(key, value);
            changedCount++;
        }
    };
    auto putULongChanged = [&](const char* key, unsigned long value) {
        if (!preferences.isKey(key) || preferences.getULong(key, value + 1UL) != value) {
            preferences.putULong(key, value);
            changedCount++;
        }
    };
    auto putFloatChanged = [&](const char* key, float value) {
        if (!preferences.isKey(key) || fabsf(preferences.getFloat(key, value + 1.0f) - value) > 0.001f) {
            preferences.putFloat(key, value);
            changedCount++;
        }
    };

    putBoolChanged("autoEnabled", settings.autoEnabled);
    putBoolChanged("dryRun", settings.dryRun);
    putFloatChanged("targetTempC", settings.targetTempC);
    putFloatChanged("coolStart", settings.coolingStartDeltaC);
    putFloatChanged("heatStart", settings.heatingStartDeltaC);
    putBoolChanged("allowVentCool", settings.allowVentCooling);
    putBoolChanged("allowAcCool", settings.allowAcCooling);
    putBoolChanged("allowAcHeat", settings.allowAcHeating);
    putFloatChanged("outCoolMinD", settings.outdoorCoolingMinDeltaC);
    putFloatChanged("outCoolMaxT", settings.outdoorCoolingMaxTempC);
    putBoolChanged("ventAlways", settings.autoVentAlwaysOn);
    putUCharChanged("ventDefault", settings.autoVentDefaultStep);
    putUCharChanged("ventMin", settings.autoVentMinStep);
    putUCharChanged("ventCoolStep", settings.autoVentCoolingStep);
    putUCharChanged("ventMax", settings.autoVentMaxStep);
    putUCharChanged("bathComp", settings.bathExhaustCompStep);
    putUCharChanged("hoodComp1", settings.hoodCompStep1);
    putUCharChanged("hoodComp2", settings.hoodCompStep2);
    putUCharChanged("hoodComp3", settings.hoodCompStep3);
    putBoolChanged("ventAdditive", settings.additiveVentCompensation);
    putFloatChanged("coldLimit", settings.coldOutdoorTempLimitC);
    putUCharChanged("coldMaxStep", settings.coldOutdoorMaxVentStep);
    putBoolChanged("fanInAuto", settings.keepAcFanOnInAuto);
    putBoolChanged("fanWithVent", settings.keepAcFanOnWithVent);
    putUCharChanged("fanOnlyMode", settings.acFanOnlyMode);
    putUCharChanged("fanMin", settings.acFanMinSpeed);
    putUCharChanged("fanNormal", settings.acFanNormalSpeed);
    putUCharChanged("fanBoost", settings.acFanBoostSpeed);
    putUCharChanged("fanMax", settings.acFanMaxSpeed);
    putBoolChanged("fanAuto", settings.acFanAutoAllowed);
    putBoolChanged("acDynamic", settings.acDynamicControlEnabled);
    putFloatChanged("acCoolFullD", settings.acCoolingFullPowerDeltaC);
    putFloatChanged("acCoolMinOff", settings.acCoolingMinTempOffsetC);
    putFloatChanged("acCoolMaxOff", settings.acCoolingMaxTempOffsetC);
    putFloatChanged("acCoolMinT", settings.acCoolingMinSetpointC);
    putUCharChanged("acCoolFanMin", settings.acCoolingMinFanSpeed);
    putUCharChanged("acCoolFanMax", settings.acCoolingMaxFanSpeed);
    putFloatChanged("acHeatFullD", settings.acHeatingFullPowerDeltaC);
    putFloatChanged("acHeatMinOff", settings.acHeatingMinTempOffsetC);
    putFloatChanged("acHeatMaxOff", settings.acHeatingMaxTempOffsetC);
    putFloatChanged("acHeatMaxT", settings.acHeatingMaxSetpointC);
    putUCharChanged("acHeatFanMin", settings.acHeatingMinFanSpeed);
    putUCharChanged("acHeatFanMax", settings.acHeatingMaxFanSpeed);
    putULongChanged("decisionMs", settings.decisionIntervalMs);
    putULongChanged("holdMs", settings.minStateHoldMs);
    putULongChanged("ventChkMs", settings.ventCoolingCheckIntervalMs);
    putFloatChanged("ventMinDrop", settings.ventCoolingMinDropC);
    putBoolChanged("ventStepFail", settings.ventCoolingStepUpOnFail);
    putBoolChanged("ventFbAc", settings.ventCoolingFallbackToAc);
    putBoolChanged("safeNoIndoor", settings.safeOnIndoorSensorMissing);
    putBoolChanged("safeEquip", settings.safeOnCriticalEquipmentError);
    putBoolChanged("diagVerbose", settings.diagnosticVerbose);
    putULongChanged("ventCompMs", settings.ventCompensationUpdateIntervalMs);
    putULongChanged("ventOffDelay", settings.ventCompensationOffDelayMs);
    putBoolChanged("ventImmUp", settings.ventCompensationImmediateUp);
    putBoolChanged("ventImmDown", settings.ventCompensationImmediateDown);
    preferences.end();

    autoSettingsDirty = false;
    Logger::infof(TAG_AUTO, "Auto control settings saved, changed keys=%u", changedCount);
    return true;
}

void ClimateAlgorithm::markSettingsDirty() {
    autoSettingsDirty = true;
    lastSettingsChangeMs = millis();
    Logger::trace(TAG_AUTO, "Auto control settings autosave scheduled");
}

void ClimateAlgorithm::processAutosave() {
    if (!autoSettingsDirty) return;
    if (millis() - lastSettingsChangeMs < AppConfig::AUTO_SETTINGS_SAVE_DELAY_MS) return;
    saveSettings();
}

AutoControlStatus ClimateAlgorithm::getStatus() const {
    return status;
}

const char* ClimateAlgorithm::activityName(ControllerActivity activity) const {
    switch (activity) {
        case ControllerActivity::Start: return "Start";
        case ControllerActivity::Normal: return "Normal";
        case ControllerActivity::VentCool: return "VentCool";
        case ControllerActivity::AcCool: return "AcCool";
        case ControllerActivity::Heat: return "Heat";
        case ControllerActivity::Vent: return "Vent";
        case ControllerActivity::Error: return "Error";
        case ControllerActivity::Hold: return "StandBy";
        case ControllerActivity::Idle: return "Monitor";
    }
    return "Unknown";
}

void ClimateAlgorithm::refreshInputs() {
    status.indoorTempC = state->environment.indoorTempC;
    status.indoorTempValid = indoorTemperatureValid();
    status.outdoorTempC = state->environment.outdoorTempC;
    status.outdoorTempValid = outdoorTemperatureValid();
    status.targetTempC = settings.targetTempC;
    status.deltaTempC = status.indoorTempValid ? status.indoorTempC - settings.targetTempC : 0.0f;
    status.needCooling = status.indoorTempValid && status.deltaTempC > settings.coolingStartDeltaC;
    status.needHeating = status.indoorTempValid && status.deltaTempC < -settings.heatingStartDeltaC;
    status.bathExhaustOn = state->environment.exhaustVentEnabled;
    status.hoodLevel = state->environment.kitchenHoodLevel;
    status.vfdOnline = state->vfd.initialized && !state->vfd.communicationError;
    status.acAvailable = state->ac.bound;
    status.inputValid = inputDataValid();
    status.ventCoolingAllowedNow = canUseVentCooling();
    status.acCoolingAllowedNow = canUseAcCooling();
    status.acHeatingAllowedNow = canUseAcHeating();
}

ControllerActivity ClimateAlgorithm::calculateActivity() {
    refreshInputs();

    status.desiredVfdPower = false;
    status.desiredVfdStep = 0;
    status.desiredVfdHz = 0.0f;
    status.desiredAcPower = false;
    status.desiredAcMode = 0;
    status.desiredAcTargetTemp = 0;
    status.desiredAcFanSpeed = 0;
    status.acCoolingRatio = 0.0f;
    status.acHeatingRatio = 0.0f;
    status.acCoolingRatio = 0.0f;
    status.acHeatingRatio = 0.0f;

    if (!status.inputValid) {
        if (settings.safeOnIndoorSensorMissing) {
            enterAutoSafeMode("Indoor temperature is missing or invalid");
        } else {
            setReason("Indoor temperature is missing or invalid, auto decision skipped");
        }
        return ControllerActivity::Error;
    }

    if (status.needCooling) {
        if (canUseVentCooling()) {
            if (currentActivity == ControllerActivity::VentCool && ventCoolingCheckStartMs > 0) {
                const bool checkDue = millis() - ventCoolingCheckStartMs >= settings.ventCoolingCheckIntervalMs;
                if (checkDue) {
                    const float tempDrop = ventCoolingStartIndoorTempC - state->environment.indoorTempC;
                    if (tempDrop >= settings.ventCoolingMinDropC) {
                        ventCoolingCheckStartMs = millis();
                        ventCoolingStartIndoorTempC = state->environment.indoorTempC;
                        setReason("Outdoor cooling is effective, VentCool continues");
                        return ControllerActivity::VentCool;
                    }
                    if (settings.ventCoolingStepUpOnFail && ventCoolingCurrentStep < settings.autoVentMaxStep) {
                        ventCoolingCurrentStep++;
                        ventCoolingCheckStartMs = millis();
                        ventCoolingStartIndoorTempC = state->environment.indoorTempC;
                        setReason("Outdoor cooling is weak, increasing ventilation step");
                        return ControllerActivity::VentCool;
                    }
                    if (settings.ventCoolingFallbackToAc && canUseAcCooling()) {
                        setReason("Outdoor cooling is weak, fallback to AC cooling");
                        return ControllerActivity::AcCool;
                    }
                }
            }

            setReason("Outdoor air suitable for free cooling, AC fan distributes supply air");
            return ControllerActivity::VentCool;
        }

        if (canUseAcCooling()) {
            setReason("Cooling required, outdoor air is not suitable, using AC cooling");
            return ControllerActivity::AcCool;
        }

        setReason("Cooling required but no cooling equipment available");
        return ControllerActivity::Error;
    }

    if (status.needHeating) {
        if (canUseAcHeating()) {
            setReason("Heating required, using AC heat mode");
            return ControllerActivity::Heat;
        }

        setReason("Heating required but AC heating is unavailable");
        return ControllerActivity::Error;
    }

    if (status.bathExhaustOn || status.hoodLevel > 0) {
        setReason("Exhaust compensation requested");
        return ControllerActivity::Vent;
    }

    setReason(settings.keepAcFanOnInAuto ? "Temperature is inside neutral zone, AC fan keeps air circulation" : "Temperature is inside neutral zone");
    return ControllerActivity::Normal;
}

ControllerActivity ClimateAlgorithm::applyStateHold(ControllerActivity requested) {
    const unsigned long now = millis();
    if (!hasDecision || requested == currentActivity || currentActivity == ControllerActivity::Error || requested == ControllerActivity::Error) {
        return requested;
    }
    if (now - stateEnteredMs < settings.minStateHoldMs) {
        setReason("hold time active");
        return currentActivity;
    }
    return requested;
}

void ClimateAlgorithm::applyActivity(ControllerActivity activity) {
    const unsigned long now = millis();
    if (!hasDecision || activity != currentActivity) {
        currentActivity = activity;
        stateEnteredMs = now;
        if (activity == ControllerActivity::VentCool) {
            ventCoolingCheckStartMs = now;
            ventCoolingStartIndoorTempC = state->environment.indoorTempC;
            ventCoolingCurrentStep = settings.autoVentCoolingStep;
        } else {
            resetVentCoolingCheck();
        }
    }

    hasDecision = true;
    status.activity = activity;
    status.lastDecisionMs = now;
    status.stateEnteredMs = stateEnteredMs;
    state->controllerState.activity = activity;
    updateDesiredStateForActivity(activity);
    applyDesiredState();
}

void ClimateAlgorithm::updateFastVentCompensation() {
    const unsigned long now = millis();
    if (now - lastVentCompensationMs < settings.ventCompensationUpdateIntervalMs) return;
    lastVentCompensationMs = now;
    status.lastVentCompensationMs = now;
    if (!hasDecision) return;
    updateDesiredStateForActivity(currentActivity);
    applyDesiredState();
}

void ClimateAlgorithm::updateVentRequirements(ControllerActivity activity) {
    status.baseVentRequirementStep = settings.autoVentAlwaysOn ? settings.autoVentDefaultStep : 0;
    status.bathCompStep = status.bathExhaustOn ? settings.bathExhaustCompStep : 0;
    status.hoodCompStep = hoodCompensationStep(status.hoodLevel);
    const uint8_t rawExhaustCompStep = clampStep(status.bathCompStep + status.hoodCompStep);
    const unsigned long now = millis();

    if (rawExhaustCompStep >= heldExhaustCompRequirementStep || settings.ventCompensationImmediateDown) {
        heldExhaustCompRequirementStep = rawExhaustCompStep;
        exhaustCompDecreasePending = false;
    } else if (settings.ventCompensationOffDelayMs == 0) {
        heldExhaustCompRequirementStep = rawExhaustCompStep;
        exhaustCompDecreasePending = false;
    } else {
        if (!exhaustCompDecreasePending) {
            exhaustCompDecreasePending = true;
            exhaustCompDecreaseStartedMs = now;
        }

        if (now - exhaustCompDecreaseStartedMs >= settings.ventCompensationOffDelayMs) {
            heldExhaustCompRequirementStep = rawExhaustCompStep;
            exhaustCompDecreasePending = false;
        }
    }

    status.exhaustCompRequirementStep = heldExhaustCompRequirementStep;
    status.coolingVentRequirementStep = activity == ControllerActivity::VentCool
        ? max(settings.autoVentCoolingStep, ventCoolingCurrentStep)
        : 0;

    if (activity == ControllerActivity::Error || activity == ControllerActivity::Idle) {
        status.requestedVentStepBeforeLimit = 0;
    } else if (settings.additiveVentCompensation) {
        status.requestedVentStepBeforeLimit = max(
            clampStep(status.baseVentRequirementStep + status.exhaustCompRequirementStep),
            status.coolingVentRequirementStep
        );
    } else {
        status.requestedVentStepBeforeLimit = max(
            max(status.baseVentRequirementStep, status.exhaustCompRequirementStep),
            status.coolingVentRequirementStep
        );
    }

    if (status.requestedVentStepBeforeLimit > 0) {
        status.requestedVentStepBeforeLimit = max(status.requestedVentStepBeforeLimit, settings.autoVentMinStep);
    }

    uint8_t limited = min(status.requestedVentStepBeforeLimit, settings.autoVentMaxStep);
    status.coldOutdoorLimitActive = status.outdoorTempValid
        && status.outdoorTempC <= settings.coldOutdoorTempLimitC
        && limited > settings.coldOutdoorMaxVentStep;
    if (status.coldOutdoorLimitActive) {
        limited = min(limited, settings.coldOutdoorMaxVentStep);
    }
    status.requestedVentStepAfterLimit = limited;
}

void ClimateAlgorithm::updateDesiredStateForActivity(ControllerActivity activity) {
    refreshInputs();
    updateVentRequirements(activity);

    status.desiredVfdStep = status.requestedVentStepAfterLimit;
    status.desiredVfdPower = status.desiredVfdStep > 0;
    status.desiredVfdHz = vfdStepToHz(status.desiredVfdStep);
    status.desiredAcPower = false;
    status.desiredAcMode = 0;
    status.desiredAcTargetTemp = 0;
    status.desiredAcFanSpeed = 0;

    if (activity == ControllerActivity::Error || activity == ControllerActivity::Idle) {
        return;
    }

    if (activity == ControllerActivity::AcCool) {
        status.desiredAcPower = true;
        status.desiredAcMode = 3;
        calculateDynamicCoolingAc(status.desiredAcTargetTemp, status.desiredAcFanSpeed);
    } else if (activity == ControllerActivity::Heat) {
        status.desiredAcPower = true;
        status.desiredAcMode = 4;
        calculateDynamicHeatingAc(status.desiredAcTargetTemp, status.desiredAcFanSpeed);
    } else if ((settings.keepAcFanOnWithVent && status.desiredVfdPower) || settings.keepAcFanOnInAuto) {
        status.desiredAcPower = true;
        status.desiredAcMode = settings.acFanOnlyMode;
        status.desiredAcTargetTemp = clampAcSetpoint(settings.targetTempC);
        status.desiredAcFanSpeed = calculateAcFanSpeedForVfdStep(status.desiredVfdStep);
    }
}

void ClimateAlgorithm::applyDesiredState() {
    setSkippedReason("none");

    if (settings.dryRun) {
        setSkippedReason("dryRun=1, commands are not sent");
        setApplyResult("dry run");
        return;
    }

    const unsigned long now = millis();
    bool commandSent = false;
    char result[96] = "";

    if (status.desiredAcPower && !state->ac.bound) {
        setSkippedReason("AC desired but AC is not bound");
    } else if (status.desiredAcPower) {
        const bool acCacheMatches = hasAppliedAc
            && lastAppliedAcPower == status.desiredAcPower
            && lastAppliedAcMode == status.desiredAcMode
            && lastAppliedAcTemp == status.desiredAcTargetTemp
            && lastAppliedAcFan == status.desiredAcFanSpeed;
        const bool acActualMatches = state->ac.powerOn
            && state->ac.mode == status.desiredAcMode
            && state->ac.temperature == status.desiredAcTargetTemp
            && state->ac.fanMode == status.desiredAcFanSpeed;
        const bool retryAllowed = now - lastAcCommandMs >= AppConfig::AUTO_COMMAND_RETRY_INTERVAL_MS;

        if (!acCacheMatches || (!acActualMatches && retryAllowed)) {
            if (!state->ac.powerOn) controller->setAcPower(true);
            if (state->ac.mode != status.desiredAcMode) controller->setAcMode(status.desiredAcMode);
            if (state->ac.temperature != status.desiredAcTargetTemp) controller->setAcTemperature(status.desiredAcTargetTemp);
            if (state->ac.fanMode != status.desiredAcFanSpeed) controller->setAcFanMode(status.desiredAcFanSpeed);
            hasAppliedAc = true;
            lastAppliedAcPower = status.desiredAcPower;
            lastAppliedAcMode = status.desiredAcMode;
            lastAppliedAcTemp = status.desiredAcTargetTemp;
            lastAppliedAcFan = status.desiredAcFanSpeed;
            lastAcCommandMs = now;
            commandSent = true;
            strlcat(result, "AC applied; ", sizeof(result));
        } else {
            setSkippedReason("AC desired state already applied");
        }
    } else if (state->ac.powerOn && (status.activity == ControllerActivity::Error || (!settings.keepAcFanOnInAuto && !status.desiredVfdPower))) {
        const bool retryAllowed = now - lastAcCommandMs >= AppConfig::AUTO_COMMAND_RETRY_INTERVAL_MS;
        if (!hasAppliedAc || lastAppliedAcPower || retryAllowed) {
            if (controller->setAcPower(false)) {
                hasAppliedAc = true;
                lastAppliedAcPower = false;
                lastAppliedAcMode = 0;
                lastAppliedAcTemp = 0;
                lastAppliedAcFan = 0;
                lastAcCommandMs = now;
                commandSent = true;
                strlcat(result, "AC off applied; ", sizeof(result));
            }
        }
    }

    if (status.desiredVfdPower && (!state->vfd.initialized || state->vfd.communicationError)) {
        setSkippedReason("VFD desired but VFD is unavailable");
    } else if (status.desiredVfdPower) {
        const bool vfdCacheMatches = hasAppliedVfd
            && lastAppliedVfdPower
            && lastAppliedVfdStep == status.desiredVfdStep
            && fabsf(lastAppliedVfdHz - status.desiredVfdHz) <= 0.5f;
        const bool vfdActualMatches = state->vfd.running
            && state->vfd.hasActualFrequency
            && fabsf(state->vfd.actualFrequencyHz - status.desiredVfdHz) <= 0.75f;
        const bool retryAllowed = now - lastVfdCommandMs >= AppConfig::AUTO_COMMAND_RETRY_INTERVAL_MS;

        if (!vfdCacheMatches || (!vfdActualMatches && retryAllowed)) {
            bool accepted = false;
            if (!state->vfd.running) accepted = controller->vfdForward("auto", false) || accepted;
            if (!state->vfd.hasRequestedFrequency || fabsf(state->vfd.requestedFrequencyHz - status.desiredVfdHz) > 0.5f) {
                accepted = controller->vfdSetFrequency(status.desiredVfdHz, "auto", false) || accepted;
            }
            if (accepted) {
                hasAppliedVfd = true;
                lastAppliedVfdPower = true;
                lastAppliedVfdStep = status.desiredVfdStep;
                lastAppliedVfdHz = status.desiredVfdHz;
                lastVfdCommandMs = now;
                commandSent = true;
                strlcat(result, "VFD applied; ", sizeof(result));
            } else {
                setSkippedReason("VFD command was not accepted, bus may be busy");
            }
        } else {
            setSkippedReason("VFD desired state already applied");
        }
    } else if (state->vfd.running) {
        const bool retryAllowed = now - lastVfdCommandMs >= AppConfig::AUTO_COMMAND_RETRY_INTERVAL_MS;
        if (!hasAppliedVfd || lastAppliedVfdPower || retryAllowed) {
            if (controller->vfdStop("auto", false)) {
                hasAppliedVfd = true;
                lastAppliedVfdPower = false;
                lastAppliedVfdStep = 0;
                lastAppliedVfdHz = 0.0f;
                lastVfdCommandMs = now;
                commandSent = true;
                strlcat(result, "VFD stop applied; ", sizeof(result));
            } else {
                setSkippedReason("VFD stop was not accepted, bus may be busy");
            }
        }
    }

    if (commandSent) {
        status.lastCommandMs = now;
        setApplyResult(result[0] != '\0' ? result : "command sent");
    } else if (status.lastSkippedReason[0] == '\0') {
        setSkippedReason("desired state already matches actual/cache");
    }
}

bool ClimateAlgorithm::inputDataValid() const {
    return indoorTemperatureValid() && settings.targetTempC >= 16.0f && settings.targetTempC <= 30.0f;
}

bool ClimateAlgorithm::indoorTemperatureValid() const {
    return state != nullptr && state->environment.hasIndoorTemp && state->environment.indoorTempC >= 5.0f && state->environment.indoorTempC <= 40.0f;
}

bool ClimateAlgorithm::outdoorTemperatureValid() const {
    return state != nullptr && state->environment.hasOutdoorTemp && state->environment.outdoorTempC >= -40.0f && state->environment.outdoorTempC <= 50.0f;
}

bool ClimateAlgorithm::canUseVentCooling() const {
    if (!settings.allowVentCooling || state == nullptr || !outdoorTemperatureValid()) return false;
    const bool vfdAvailable = state->vfd.initialized && !state->vfd.communicationError;
    return vfdAvailable
        && state->environment.indoorTempC - state->environment.outdoorTempC >= settings.outdoorCoolingMinDeltaC
        && state->environment.outdoorTempC <= settings.outdoorCoolingMaxTempC;
}

bool ClimateAlgorithm::canUseAcCooling() const {
    return settings.allowAcCooling && state != nullptr && state->ac.bound;
}

bool ClimateAlgorithm::canUseAcHeating() const {
    return settings.allowAcHeating && state != nullptr && state->ac.bound;
}

uint8_t ClimateAlgorithm::hoodCompensationStep(uint8_t hoodLevel) const {
    switch (hoodLevel) {
        case 1: return settings.hoodCompStep1;
        case 2: return settings.hoodCompStep2;
        case 3: return settings.hoodCompStep3;
        default: return 0;
    }
}

void ClimateAlgorithm::calculateDynamicCoolingAc(uint8_t& targetTemp, uint8_t& fanSpeed) {
    const float coolingExcessC = max(0.0f, status.deltaTempC - settings.coolingStartDeltaC);
    const float ratio = settings.acDynamicControlEnabled
        ? clampFloat(coolingExcessC / settings.acCoolingFullPowerDeltaC, 0.0f, 1.0f)
        : 0.0f;
    status.acCoolingRatio = ratio;
    status.acHeatingRatio = 0.0f;

    const float fan = settings.acCoolingMinFanSpeed
        + ratio * (settings.acCoolingMaxFanSpeed - settings.acCoolingMinFanSpeed);
    fanSpeed = clampAcFan((uint8_t)lroundf(fan), settings.acFanAutoAllowed);

    const float offset = settings.acCoolingMinTempOffsetC
        + ratio * (settings.acCoolingMaxTempOffsetC - settings.acCoolingMinTempOffsetC);
    targetTemp = clampAcSetpoint(max(settings.targetTempC - offset, settings.acCoolingMinSetpointC));
}

void ClimateAlgorithm::calculateDynamicHeatingAc(uint8_t& targetTemp, uint8_t& fanSpeed) {
    const float heatingErrorC = settings.targetTempC - status.indoorTempC;
    const float heatingExcessC = max(0.0f, heatingErrorC - settings.heatingStartDeltaC);
    const float ratio = settings.acDynamicControlEnabled
        ? clampFloat(heatingExcessC / settings.acHeatingFullPowerDeltaC, 0.0f, 1.0f)
        : 0.0f;
    status.acHeatingRatio = ratio;
    status.acCoolingRatio = 0.0f;

    const float fan = settings.acHeatingMinFanSpeed
        + ratio * (settings.acHeatingMaxFanSpeed - settings.acHeatingMinFanSpeed);
    fanSpeed = clampAcFan((uint8_t)lroundf(fan), settings.acFanAutoAllowed);

    const float offset = settings.acHeatingMinTempOffsetC
        + ratio * (settings.acHeatingMaxTempOffsetC - settings.acHeatingMinTempOffsetC);
    targetTemp = clampAcSetpoint(min(settings.targetTempC + offset, settings.acHeatingMaxSetpointC));
}

uint8_t ClimateAlgorithm::calculateAcFanSpeedForVfdStep(uint8_t vfdStep) const {
    uint8_t fan = settings.acFanMinSpeed;
    if (vfdStep >= 6) {
        fan = settings.acFanMaxSpeed;
    } else if (vfdStep >= 4) {
        fan = settings.acFanBoostSpeed;
    } else if (vfdStep >= 2) {
        fan = settings.acFanNormalSpeed;
    }
    return min(fan, settings.acFanMaxSpeed);
}

float ClimateAlgorithm::vfdStepToHz(uint8_t step) const {
    if (step == 0) return 0.0f;
    if (step >= 6) return 50.0f;
    return 20.0f + (step - 1) * 6.0f;
}

uint8_t ClimateAlgorithm::clampAcSetpoint(float value) const {
    const int valueInt = (int)lroundf(value);
    return constrain(valueInt, 16, 30);
}

void ClimateAlgorithm::enterAutoSafeMode(const char* reason) {
    setReason(reason);
    if (state == nullptr) return;
    if (state->controllerState.mode != DeviceMode::Safe) {
        Logger::errorf(TAG_AUTO, "Entering SAFE mode: %s", reason);
    }
    autoSafeModeActive = true;
    lastSafeRetryMs = millis();
    resetVentCoolingCheck();
    state->controllerState.mode = DeviceMode::Safe;
}

void ClimateAlgorithm::updateSafeRecovery() {
    if (!autoSafeModeActive) {
        setReason("Safe mode active, commands are blocked");
        return;
    }
    const unsigned long now = millis();
    if (now - lastSafeRetryMs < AppConfig::AUTO_SAFE_RETRY_INTERVAL_MS) {
        setReason("Safe mode active, waiting for automatic recovery retry");
        return;
    }
    lastSafeRetryMs = now;
    refreshInputs();
    if (!inputDataValid()) {
        setReason("Safe retry failed: indoor temperature is still missing or invalid");
        Logger::warning(TAG_AUTO, status.reason);
        return;
    }
    autoSafeModeActive = false;
    hasDecision = false;
    currentActivity = ControllerActivity::Idle;
    stateEnteredMs = now;
    state->controllerState.mode = DeviceMode::Auto;
    state->controllerState.activity = ControllerActivity::Normal;
    setReason("Safe retry succeeded: input data recovered, returning to Auto");
    Logger::info(TAG_AUTO, status.reason);
}

void ClimateAlgorithm::resetVentCoolingCheck() {
    ventCoolingCheckStartMs = 0;
    ventCoolingStartIndoorTempC = DEVICE_DISCONNECTED_C;
    ventCoolingCurrentStep = settings.autoVentCoolingStep;
}

void ClimateAlgorithm::setReason(const char* reason) {
    strncpy(status.reason, reason, sizeof(status.reason) - 1);
    status.reason[sizeof(status.reason) - 1] = '\0';
}

void ClimateAlgorithm::setApplyResult(const char* result) {
    strncpy(status.lastApplyResult, result, sizeof(status.lastApplyResult) - 1);
    status.lastApplyResult[sizeof(status.lastApplyResult) - 1] = '\0';
}

void ClimateAlgorithm::setSkippedReason(const char* reason) {
    strncpy(status.lastSkippedReason, reason, sizeof(status.lastSkippedReason) - 1);
    status.lastSkippedReason[sizeof(status.lastSkippedReason) - 1] = '\0';
}

void ClimateAlgorithm::logDecision() const {
    if (!settings.diagnosticVerbose) return;
    Logger::infof(
        TAG_AUTO,
        "mode=AUTO indoor=%.1f outdoor=%.1f target=%.1f delta=%.1f activity=%s vfd=%u step=%u ac=%u mode=%u fan=%u acTarget=%u coolRatio=%.2f heatRatio=%.2f dryRun=%u",
        status.indoorTempC,
        status.outdoorTempC,
        settings.targetTempC,
        status.deltaTempC,
        activityName(status.activity),
        status.desiredVfdPower ? 1 : 0,
        status.desiredVfdStep,
        status.desiredAcPower ? 1 : 0,
        status.desiredAcMode,
        status.desiredAcFanSpeed,
        status.desiredAcTargetTemp,
        status.acCoolingRatio,
        status.acHeatingRatio,
        settings.dryRun ? 1 : 0
    );
    Logger::infof(TAG_AUTO, "reason=%s", status.reason);
}
