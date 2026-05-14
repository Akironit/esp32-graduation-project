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

    if (state->controllerState.mode == DeviceMode::Manual || state->controllerState.mode == DeviceMode::Disabled) {
        status.activity = state->controllerState.mode == DeviceMode::Disabled ? ControllerActivity::Idle : state->controllerState.activity;
        return;
    }

    if (state->controllerState.mode == DeviceMode::Safe) {
        state->controllerState.activity = ControllerActivity::Error;
        status.activity = ControllerActivity::Error;
        status.lastDecisionMs = millis();
        setReason("Safe mode active");
        return;
    }

    const unsigned long now = millis();
    if (now - lastDecisionMs < settings.decisionIntervalMs) {
        return;
    }

    lastDecisionMs = now;
    settings.targetTempC = clampFloat(state->environment.targetIndoorTempC, 16.0f, 30.0f);
    status.targetTempC = settings.targetTempC;
    status.indoorTempC = state->environment.indoorTempC;
    status.outdoorTempC = state->environment.outdoorTempC;
    status.deltaC = state->environment.hasIndoorTemp ? state->environment.indoorTempC - settings.targetTempC : 0.0f;

    ControllerActivity requested = calculateActivity();
    requested = applyStateHold(requested);
    applyActivity(requested);
    logDecision();
}

void ClimateAlgorithm::setSettings(const AutoControlSettings& newSettings) {
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
    settings.decisionIntervalMs = max(settings.decisionIntervalMs, 500UL);
    settings.minStateHoldMs = max(settings.minStateHoldMs, 1000UL);
    settings.ventCoolTimeoutMs = max(settings.ventCoolTimeoutMs, 10000UL);

    if (state != nullptr) {
        state->environment.targetIndoorTempC = settings.targetTempC;
        state->settings.targetIndoorTempC = settings.targetTempC;
    }
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
    loaded.outdoorCoolingMarginC = preferences.getFloat("outMargin", loaded.outdoorCoolingMarginC);
    loaded.outdoorCoolingMaxC = preferences.getFloat("outMax", loaded.outdoorCoolingMaxC);
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
    loaded.dryRun = preferences.getBool("dryRun", loaded.dryRun);
    preferences.end();

    setSettings(loaded);
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
    preferences.putFloat("outMargin", settings.outdoorCoolingMarginC);
    preferences.putFloat("outMax", settings.outdoorCoolingMaxC);
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
    preferences.putBool("dryRun", settings.dryRun);
    preferences.end();

    Logger::info(TAG_AUTO, "Auto control settings saved");
    return true;
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
            return "Idle";
    }

    return "Unknown";
}

ControllerActivity ClimateAlgorithm::calculateActivity() {
    status.vfdDesiredStep = 0;
    status.vfdDesiredPower = false;
    status.acDesiredPower = false;
    status.acDesiredMode = 0;
    status.acDesiredTemp = 0;
    status.acDesiredFan = 0;
    status.inputValid = inputDataValid();

    if (!status.inputValid) {
        setReason("Indoor temperature is missing or invalid");
        state->controllerState.mode = DeviceMode::Safe;
        return ControllerActivity::Error;
    }

    const float delta = status.deltaC;
    const bool exhaustActive = state->environment.exhaustVentEnabled || state->environment.kitchenHoodLevel > 0;

    if (fabsf(delta) <= settings.hysteresisC) {
        if (exhaustActive) {
            setReason("Ventilation boost requested by exhaust");
            return ControllerActivity::Vent;
        }

        setReason("Temperature is within hysteresis");
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

            setReason("Outdoor air suitable for cooling");
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
        setReason("Ventilation boost requested by exhaust");
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

    state->controllerState.activity = activity;

    if (activity == ControllerActivity::Error) {
        state->controllerState.errorCount = max<uint8_t>(state->controllerState.errorCount, 1);
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

    if (settings.dryRun) {
        return;
    }

    if (status.acDesiredPower) {
        if (!state->ac.powerOn) controller->setAcPower(true);
        if (state->ac.mode != status.acDesiredMode) controller->setAcMode(status.acDesiredMode);
        if (state->ac.temperature != status.acDesiredTemp) controller->setAcTemperature(status.acDesiredTemp);
        if (state->ac.fanMode != status.acDesiredFan) controller->setAcFanMode(status.acDesiredFan);
    } else if (state->ac.powerOn && !settings.keepAcFanOnInAuto && !status.vfdDesiredPower) {
        controller->setAcPower(false);
    }

    if (status.vfdDesiredStep > 0 && state->vfd.initialized && !state->vfd.communicationError) {
        const float hz = vfdStepToHz(status.vfdDesiredStep);
        if (!state->vfd.running) {
            controller->vfdForward("auto", false);
        }
        if (!state->vfd.hasRequestedFrequency || fabsf(state->vfd.requestedFrequencyHz - hz) > 0.5f) {
            controller->vfdSetFrequency(hz, "auto", false);
        }
    } else if (state->vfd.running) {
        controller->vfdStop("auto", false);
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

void ClimateAlgorithm::setReason(const char* reason) {
    strncpy(status.reason, reason, sizeof(status.reason) - 1);
    status.reason[sizeof(status.reason) - 1] = '\0';
}

void ClimateAlgorithm::logDecision() const {
    Logger::infof(
        TAG_AUTO,
        "mode=AUTO indoor=%.1f outdoor=%.1f target=%.1f delta=%.1f decision=%s vfdPower=%u vfdStep=%u ac=%s acMode=%u acFan=%u dryRun=%u reason=%s",
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
        settings.dryRun ? 1 : 0,
        status.reason
    );
}
