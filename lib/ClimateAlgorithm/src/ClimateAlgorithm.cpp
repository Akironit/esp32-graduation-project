#include "ClimateAlgorithm.h"

#include <math.h>

#include "Logger.h"

namespace {
constexpr const char* TAG_AUTO = "AUTO";
constexpr const char* AUTO_NVS_NAMESPACE = "autoctrl";

uint8_t clampStep(uint8_t value) {
    return value > 6 ? 6 : value;
}

uint8_t clampAcFan(uint8_t value) {
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

    if (state->controllerState.mode == DeviceMode::Manual || state->controllerState.mode == DeviceMode::Disabled) {
        status.activity = state->controllerState.mode == DeviceMode::Disabled ? ControllerActivity::Idle : state->controllerState.activity;
        if (state->controllerState.mode == DeviceMode::Disabled) {
            state->controllerState.activity = ControllerActivity::Idle;
        }
        status.lastDecisionMs = millis();
        setReason(state->controllerState.mode == DeviceMode::Disabled ? "Disabled monitoring mode active" : "Manual mode active");
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

    const unsigned long now = millis();
    if (now - lastDecisionMs < settings.decisionIntervalMs) {
        return;
    }

    lastDecisionMs = now;
    status.targetTempC = settings.targetTempC;
    status.indoorTempC = state->environment.indoorTempC;
    status.outdoorTempC = state->environment.outdoorTempC;
    status.deltaC = state->environment.hasIndoorTemp ? state->environment.indoorTempC - settings.targetTempC : 0.0f;

    ControllerActivity requested = calculateActivity();
    requested = applyStateHold(requested);
    applyActivity(requested);
    logDecision();
}

void ClimateAlgorithm::setSettings(const AutoControlSettings& newSettings, bool autosave) {
    settings = newSettings;
    settings.targetTempC = clampFloat(settings.targetTempC, 16.0f, 30.0f);
    settings.hysteresisC = clampFloat(settings.hysteresisC, 0.1f, 5.0f);
    settings.coolingStartDeltaC = clampFloat(settings.coolingStartDeltaC, 0.1f, 10.0f);
    settings.heatingStartDeltaC = clampFloat(settings.heatingStartDeltaC, 0.1f, 10.0f);
    settings.outdoorCoolingMarginC = clampFloat(settings.outdoorCoolingMarginC, 0.0f, 10.0f);
    settings.outdoorCoolingMaxC = clampFloat(settings.outdoorCoolingMaxC, -20.0f, 40.0f);
    settings.ventCoolMinDropC = clampFloat(settings.ventCoolMinDropC, 0.0f, 5.0f);
    settings.minVentStep = clampStep(settings.minVentStep);
    settings.normalVentStep = clampStep(settings.normalVentStep);
    settings.coolingVentStep = clampStep(settings.coolingVentStep);
    settings.maxVentStep = clampStep(settings.maxVentStep);
    settings.exhaustBoostStep = clampStep(settings.exhaustBoostStep);
    settings.kitchenHoodBoostStep = clampStep(settings.kitchenHoodBoostStep);
    settings.acFanMinSpeed = clampAcFan(settings.acFanMinSpeed);
    settings.acFanNormalSpeed = clampAcFan(settings.acFanNormalSpeed);
    settings.acFanBoostSpeed = clampAcFan(settings.acFanBoostSpeed);
    settings.acCoolFanMode = clampAcFan(settings.acCoolFanMode);
    settings.acHeatFanMode = clampAcFan(settings.acHeatFanMode);
    if (settings.acFanOnlyMode < 1 || settings.acFanOnlyMode > 5) {
        settings.acFanOnlyMode = 1;
    }
    settings.decisionIntervalMs = max(settings.decisionIntervalMs, 500UL);
    settings.minStateHoldMs = max(settings.minStateHoldMs, 1000UL);
    settings.ventCoolTimeoutMs = max(settings.ventCoolTimeoutMs, 10000UL);

    if (state != nullptr) {
        state->environment.targetIndoorTempC = settings.targetTempC;
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
}

bool ClimateAlgorithm::loadSettings() {
    if (!preferences.begin(AUTO_NVS_NAMESPACE, true)) {
        Logger::warning(TAG_AUTO, "Failed to open auto control settings for reading");
        return false;
    }

    AutoControlSettings loaded = settings;
    loaded.targetTempC = preferences.getFloat("target", loaded.targetTempC);
    loaded.hysteresisC = preferences.getFloat("hyst", loaded.hysteresisC);
    loaded.coolingStartDeltaC = preferences.getFloat("coolDelta", loaded.coolingStartDeltaC);
    loaded.heatingStartDeltaC = preferences.getFloat("heatDelta", loaded.heatingStartDeltaC);
    loaded.outdoorCoolingMarginC = preferences.getFloat("outMargin", loaded.outdoorCoolingMarginC);
    loaded.outdoorCoolingMaxC = preferences.getFloat("outMax", loaded.outdoorCoolingMaxC);
    loaded.ventCoolMinDropC = preferences.getFloat("ventDrop", loaded.ventCoolMinDropC);
    loaded.minVentStep = preferences.getUChar("minStep", loaded.minVentStep);
    loaded.normalVentStep = preferences.getUChar("normStep", loaded.normalVentStep);
    loaded.coolingVentStep = preferences.getUChar("coolStep", loaded.coolingVentStep);
    loaded.maxVentStep = preferences.getUChar("maxStep", loaded.maxVentStep);
    loaded.exhaustBoostStep = preferences.getUChar("exhBoost", loaded.exhaustBoostStep);
    loaded.kitchenHoodBoostStep = preferences.getUChar("hoodBoost", loaded.kitchenHoodBoostStep);
    loaded.decisionIntervalMs = preferences.getULong("interval", loaded.decisionIntervalMs);
    loaded.minStateHoldMs = preferences.getULong("hold", loaded.minStateHoldMs);
    loaded.ventCoolTimeoutMs = preferences.getULong("ventTimeout", loaded.ventCoolTimeoutMs);
    loaded.allowAcCooling = preferences.getBool("acCool", loaded.allowAcCooling);
    loaded.allowAcHeating = preferences.getBool("acHeat", loaded.allowAcHeating);
    loaded.allowVentCooling = preferences.getBool("ventCool", loaded.allowVentCooling);
    loaded.keepAcFanOnWithVent = preferences.getBool("fanWithVent", loaded.keepAcFanOnWithVent);
    loaded.keepAcFanOnInAuto = preferences.getBool("fanInAuto", loaded.keepAcFanOnInAuto);
    loaded.acFanOnlyMode = preferences.getUChar("fanOnlyMode", loaded.acFanOnlyMode);
    loaded.acFanMinSpeed = preferences.getUChar("fanMin", loaded.acFanMinSpeed);
    loaded.acFanNormalSpeed = preferences.getUChar("fanNormal", loaded.acFanNormalSpeed);
    loaded.acFanBoostSpeed = preferences.getUChar("fanBoost", loaded.acFanBoostSpeed);
    loaded.acCoolTempOffsetC = preferences.getUChar("coolOffset", loaded.acCoolTempOffsetC);
    loaded.acHeatTempOffsetC = preferences.getUChar("heatOffset", loaded.acHeatTempOffsetC);
    loaded.acCoolFanMode = preferences.getUChar("coolFan", loaded.acCoolFanMode);
    loaded.acHeatFanMode = preferences.getUChar("heatFan", loaded.acHeatFanMode);
    loaded.dryRun = preferences.getBool("dryRun", loaded.dryRun);
    loaded.autoLogEnabled = preferences.getBool("autoLog", loaded.autoLogEnabled);
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

    preferences.putFloat("target", settings.targetTempC);
    preferences.putFloat("hyst", settings.hysteresisC);
    preferences.putFloat("coolDelta", settings.coolingStartDeltaC);
    preferences.putFloat("heatDelta", settings.heatingStartDeltaC);
    preferences.putFloat("outMargin", settings.outdoorCoolingMarginC);
    preferences.putFloat("outMax", settings.outdoorCoolingMaxC);
    preferences.putFloat("ventDrop", settings.ventCoolMinDropC);
    preferences.putUChar("minStep", settings.minVentStep);
    preferences.putUChar("normStep", settings.normalVentStep);
    preferences.putUChar("coolStep", settings.coolingVentStep);
    preferences.putUChar("maxStep", settings.maxVentStep);
    preferences.putUChar("exhBoost", settings.exhaustBoostStep);
    preferences.putUChar("hoodBoost", settings.kitchenHoodBoostStep);
    preferences.putULong("interval", settings.decisionIntervalMs);
    preferences.putULong("hold", settings.minStateHoldMs);
    preferences.putULong("ventTimeout", settings.ventCoolTimeoutMs);
    preferences.putBool("acCool", settings.allowAcCooling);
    preferences.putBool("acHeat", settings.allowAcHeating);
    preferences.putBool("ventCool", settings.allowVentCooling);
    preferences.putBool("fanWithVent", settings.keepAcFanOnWithVent);
    preferences.putBool("fanInAuto", settings.keepAcFanOnInAuto);
    preferences.putUChar("fanOnlyMode", settings.acFanOnlyMode);
    preferences.putUChar("fanMin", settings.acFanMinSpeed);
    preferences.putUChar("fanNormal", settings.acFanNormalSpeed);
    preferences.putUChar("fanBoost", settings.acFanBoostSpeed);
    preferences.putUChar("coolOffset", settings.acCoolTempOffsetC);
    preferences.putUChar("heatOffset", settings.acHeatTempOffsetC);
    preferences.putUChar("coolFan", settings.acCoolFanMode);
    preferences.putUChar("heatFan", settings.acHeatFanMode);
    preferences.putBool("dryRun", settings.dryRun);
    preferences.putBool("autoLog", settings.autoLogEnabled);
    preferences.end();

    autoSettingsDirty = false;
    Logger::info(TAG_AUTO, "Auto control settings saved");
    return true;
}

void ClimateAlgorithm::markSettingsDirty() {
    autoSettingsDirty = true;
    lastSettingsChangeMs = millis();
    Logger::trace(TAG_AUTO, "Auto control settings autosave scheduled");
}

void ClimateAlgorithm::processAutosave() {
    if (!autoSettingsDirty) {
        return;
    }

    if (millis() - lastSettingsChangeMs < AppConfig::AUTO_SETTINGS_SAVE_DELAY_MS) {
        return;
    }

    saveSettings();
}

AutoControlStatus ClimateAlgorithm::getStatus() const {
    return status;
}

const char* ClimateAlgorithm::activityName(ControllerActivity activity) const {
    switch (activity) {
        case ControllerActivity::Start:
            return "Start";
        case ControllerActivity::Normal:
            return "Normal";
        case ControllerActivity::VentCool:
            return "VentCool";
        case ControllerActivity::AcCool:
            return "AcCool";
        case ControllerActivity::Heat:
            return "Heat";
        case ControllerActivity::Vent:
            return "Vent";
        case ControllerActivity::Error:
            return "Error";
        case ControllerActivity::Hold:
            return "Hold";
        case ControllerActivity::Idle:
            return "Monitor";
    }

    return "Unknown";
}

ControllerActivity ClimateAlgorithm::calculateActivity() {
    status.vfdDesiredStep = 0;
    status.vfdDesiredPower = false;
    status.vfdDesiredHz = 0.0f;
    status.acDesiredPower = false;
    status.acDesiredMode = 0;
    status.acDesiredTemp = 0;
    status.acDesiredFan = 0;
    status.inputValid = inputDataValid();

    if (!status.inputValid) {
        enterAutoSafeMode("Indoor temperature is missing or invalid");
        return ControllerActivity::Error;
    }

    const float delta = status.deltaC;
    const bool exhaustActive = state->environment.exhaustVentEnabled || state->environment.kitchenHoodLevel > 0;

    if (fabsf(delta) <= settings.hysteresisC) {
        if (exhaustActive) {
            setReason("Ventilation boost requested by exhaust, AC fan distributes supply air");
            return ControllerActivity::Vent;
        }

        setReason(settings.keepAcFanOnInAuto ? "Temperature is within hysteresis, AC fan keeps air circulation" : "Temperature is within hysteresis");
        return hasDecision && currentActivity != ControllerActivity::Idle ? ControllerActivity::Hold : ControllerActivity::Normal;
    }

    if (delta > settings.coolingStartDeltaC) {
        if (canUseVentCooling()) {
            if (currentActivity == ControllerActivity::VentCool && ventCoolStartMs > 0) {
                const bool timeout = millis() - ventCoolStartMs >= settings.ventCoolTimeoutMs;
                const bool noDrop = ventCoolStartIndoorTempC - state->environment.indoorTempC < settings.ventCoolMinDropC;
                if (timeout && noDrop && canUseAcCooling()) {
                    setReason("Vent cooling timeout, fallback to AC cooling");
                    return ControllerActivity::AcCool;
                }
            }

            setReason("Outdoor air suitable for cooling, AC fan distributes supply air");
            return ControllerActivity::VentCool;
        }

        if (canUseAcCooling()) {
            setReason("AC cooling required");
            return ControllerActivity::AcCool;
        }

        setReason("Cooling required but no cooling equipment available");
        return ControllerActivity::Error;
    }

    if (delta < -settings.heatingStartDeltaC) {
        if (canUseAcHeating()) {
            setReason("AC heating required");
            return ControllerActivity::Heat;
        }

        setReason("Heating required but AC heating is unavailable");
        return ControllerActivity::Error;
    }

    if (exhaustActive) {
        setReason("Ventilation boost requested by exhaust, AC fan distributes supply air");
        return ControllerActivity::Vent;
    }

    setReason("Temperature delta is below start threshold");
    return ControllerActivity::Hold;
}

ControllerActivity ClimateAlgorithm::applyStateHold(ControllerActivity requested) {
    const unsigned long now = millis();
    if (!hasDecision || requested == currentActivity || currentActivity == ControllerActivity::Error || requested == ControllerActivity::Error) {
        return requested;
    }

    if (now - stateEnteredMs < settings.minStateHoldMs) {
        setReason("Minimum state hold time is active");
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
            ventCoolStartMs = now;
            ventCoolStartIndoorTempC = state->environment.indoorTempC;
        } else {
            ventCoolStartMs = 0;
            ventCoolStartIndoorTempC = DEVICE_DISCONNECTED_C;
        }
    }

    hasDecision = true;
    status.activity = activity;
    status.lastDecisionMs = now;
    status.stateEnteredMs = stateEnteredMs;
    status.vfdDesiredStep = calculateVentStep(activity);
    status.vfdDesiredPower = status.vfdDesiredStep > 0;
    status.vfdDesiredHz = vfdStepToHz(status.vfdDesiredStep);

    state->controllerState.activity = activity;

    if (activity == ControllerActivity::Error) {
        state->controllerState.errorCount = max<uint8_t>(state->controllerState.errorCount, 1);
        applyDesiredState();
        return;
    }

    if (activity == ControllerActivity::AcCool) {
        status.acDesiredPower = true;
        status.acDesiredMode = 3;
        status.acDesiredTemp = acTargetTemperature(settings.acCoolTempOffsetC);
        status.acDesiredFan = settings.acCoolFanMode;
    } else if (activity == ControllerActivity::Heat) {
        status.acDesiredPower = true;
        status.acDesiredMode = 4;
        status.acDesiredTemp = acTargetTemperature(settings.acHeatTempOffsetC);
        status.acDesiredFan = settings.acHeatFanMode;
    } else if ((settings.keepAcFanOnWithVent && status.vfdDesiredPower) || settings.keepAcFanOnInAuto) {
        status.acDesiredPower = true;
        status.acDesiredMode = settings.acFanOnlyMode;
        status.acDesiredTemp = acTargetTemperature(0);
        status.acDesiredFan = calculateAcFanSpeedForVfdStep(status.vfdDesiredStep);
    }

    applyDesiredState();
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

    if (status.acDesiredPower && !state->ac.bound) {
        setSkippedReason("AC desired but AC is not bound");
    } else if (status.acDesiredPower) {
        const bool acCacheMatches = hasAppliedAc
            && lastAppliedAcPower == status.acDesiredPower
            && lastAppliedAcMode == status.acDesiredMode
            && lastAppliedAcTemp == status.acDesiredTemp
            && lastAppliedAcFan == status.acDesiredFan;
        const bool acActualMatches = state->ac.powerOn
            && state->ac.mode == status.acDesiredMode
            && state->ac.temperature == status.acDesiredTemp
            && state->ac.fanMode == status.acDesiredFan;
        const bool retryAllowed = now - lastAcCommandMs >= AppConfig::AUTO_COMMAND_RETRY_INTERVAL_MS;

        if (!acCacheMatches || (!acActualMatches && retryAllowed)) {
            if (!state->ac.powerOn) controller->setAcPower(true);
            if (state->ac.mode != status.acDesiredMode) controller->setAcMode(status.acDesiredMode);
            if (state->ac.temperature != status.acDesiredTemp) controller->setAcTemperature(status.acDesiredTemp);
            if (state->ac.fanMode != status.acDesiredFan) controller->setAcFanMode(status.acDesiredFan);
            hasAppliedAc = true;
            lastAppliedAcPower = status.acDesiredPower;
            lastAppliedAcMode = status.acDesiredMode;
            lastAppliedAcTemp = status.acDesiredTemp;
            lastAppliedAcFan = status.acDesiredFan;
            lastAcCommandMs = now;
            commandSent = true;
            strlcat(result, "AC applied; ", sizeof(result));
        } else {
            setSkippedReason("AC desired state already applied");
        }
    } else if (state->ac.powerOn && (status.activity == ControllerActivity::Error || (!settings.keepAcFanOnInAuto && !status.vfdDesiredPower))) {
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

    if (status.vfdDesiredPower && (!state->vfd.initialized || state->vfd.communicationError)) {
        setSkippedReason("VFD desired but VFD is unavailable");
    } else if (status.vfdDesiredPower) {
        const bool vfdCacheMatches = hasAppliedVfd
            && lastAppliedVfdPower
            && lastAppliedVfdStep == status.vfdDesiredStep
            && fabsf(lastAppliedVfdHz - status.vfdDesiredHz) <= 0.5f;
        const bool vfdActualMatches = state->vfd.running
            && state->vfd.hasActualFrequency
            && fabsf(state->vfd.actualFrequencyHz - status.vfdDesiredHz) <= 0.75f;
        const bool retryAllowed = now - lastVfdCommandMs >= AppConfig::AUTO_COMMAND_RETRY_INTERVAL_MS;

        if (!vfdCacheMatches || (!vfdActualMatches && retryAllowed)) {
            bool accepted = false;
            if (!state->vfd.running) {
                accepted = controller->vfdForward("auto", false) || accepted;
            }
            if (!state->vfd.hasRequestedFrequency || fabsf(state->vfd.requestedFrequencyHz - status.vfdDesiredHz) > 0.5f) {
                accepted = controller->vfdSetFrequency(status.vfdDesiredHz, "auto", false) || accepted;
            }
            if (accepted) {
                hasAppliedVfd = true;
                lastAppliedVfdPower = true;
                lastAppliedVfdStep = status.vfdDesiredStep;
                lastAppliedVfdHz = status.vfdDesiredHz;
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
    return state != nullptr
        && state->environment.hasIndoorTemp
        && state->environment.indoorTempC >= 5.0f
        && state->environment.indoorTempC <= 40.0f;
}

bool ClimateAlgorithm::outdoorTemperatureValid() const {
    return state != nullptr
        && state->environment.hasOutdoorTemp
        && state->environment.outdoorTempC >= -40.0f
        && state->environment.outdoorTempC <= 50.0f;
}

bool ClimateAlgorithm::canUseVentCooling() const {
    if (!settings.allowVentCooling || state == nullptr) {
        return false;
    }

    if (!outdoorTemperatureValid()) {
        return false;
    }

    const bool vfdAvailable = state->vfd.initialized && !state->vfd.communicationError;
    return vfdAvailable
        && state->environment.outdoorTempC + settings.outdoorCoolingMarginC < state->environment.indoorTempC
        && state->environment.outdoorTempC <= settings.outdoorCoolingMaxC;
}

bool ClimateAlgorithm::canUseAcCooling() const {
    return settings.allowAcCooling && state != nullptr && state->ac.bound;
}

bool ClimateAlgorithm::canUseAcHeating() const {
    return settings.allowAcHeating && state != nullptr && state->ac.bound;
}

uint8_t ClimateAlgorithm::calculateVentStep(ControllerActivity activity) const {
    if (activity == ControllerActivity::Error || activity == ControllerActivity::Idle) {
        return 0;
    }

    uint8_t step = 0;

    if (activity == ControllerActivity::VentCool) {
        step = settings.coolingVentStep;
    } else if (activity == ControllerActivity::Vent) {
        step = settings.normalVentStep;
    } else if (activity == ControllerActivity::Normal) {
        step = settings.minVentStep;
    } else if (activity == ControllerActivity::AcCool) {
        step = settings.normalVentStep;
    }

    uint8_t boost = 0;
    if (state != nullptr && state->environment.exhaustVentEnabled) {
        boost += settings.exhaustBoostStep;
    }
    if (state != nullptr && state->environment.kitchenHoodLevel > 0) {
        boost += state->environment.kitchenHoodLevel * settings.kitchenHoodBoostStep;
    }

    if (boost > 0) {
        step = max(step, settings.normalVentStep);
        step += boost;
    }

    return min(step, settings.maxVentStep);
}

uint8_t ClimateAlgorithm::calculateAcFanSpeedForVfdStep(uint8_t vfdStep) const {
    if (vfdStep <= 1) {
        return settings.acFanMinSpeed;
    }
    if (vfdStep <= 3) {
        return settings.acFanNormalSpeed;
    }
    return settings.acFanBoostSpeed;
}

float ClimateAlgorithm::vfdStepToHz(uint8_t step) const {
    if (step == 0) {
        return 0.0f;
    }
    if (step >= 6) {
        return 50.0f;
    }
    return 20.0f + (step - 1) * 6.0f;
}

uint8_t ClimateAlgorithm::acTargetTemperature(int8_t offset) const {
    const int value = (int)lroundf(settings.targetTempC) + offset;
    return constrain(value, 16, 30);
}

void ClimateAlgorithm::enterAutoSafeMode(const char* reason) {
    setReason(reason);
    if (state == nullptr) {
        return;
    }

    if (state->controllerState.mode != DeviceMode::Safe) {
        Logger::errorf(TAG_AUTO, "Entering SAFE mode: %s", reason);
    }

    autoSafeModeActive = true;
    lastSafeRetryMs = millis();
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
    status.targetTempC = settings.targetTempC;
    status.indoorTempC = state->environment.indoorTempC;
    status.outdoorTempC = state->environment.outdoorTempC;
    status.deltaC = state->environment.hasIndoorTemp ? state->environment.indoorTempC - settings.targetTempC : 0.0f;
    status.inputValid = inputDataValid();

    if (!status.inputValid) {
        setReason("Safe retry failed: indoor temperature is still missing or invalid");
        Logger::warning(TAG_AUTO, status.reason);
        return;
    }

    autoSafeModeActive = false;
    hasDecision = false;
    currentActivity = ControllerActivity::Idle;
    stateEnteredMs = now;
    state->controllerState.mode = DeviceMode::Auto;
    state->controllerState.activity = ControllerActivity::Idle;
    setReason("Safe retry succeeded: input data recovered, returning to Auto");
    Logger::info(TAG_AUTO, status.reason);
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
    if (!settings.autoLogEnabled) {
        return;
    }

    Logger::infof(
        TAG_AUTO,
        "mode=AUTO indoor=%.1f outdoor=%.1f target=%.1f delta=%.1f decision=%s vfdPower=%u vfdStep=%u ac=%s acMode=%u acFan=%u dryRun=%u",
        status.indoorTempC,
        status.outdoorTempC,
        settings.targetTempC,
        status.deltaC,
        activityName(status.activity),
        status.vfdDesiredPower ? 1 : 0,
        status.vfdDesiredStep,
        status.acDesiredPower ? "on" : "off",
        status.acDesiredMode,
        status.acDesiredFan,
        settings.dryRun ? 1 : 0
    );
    Logger::infof(TAG_AUTO, "reason=%s", status.reason);
}
