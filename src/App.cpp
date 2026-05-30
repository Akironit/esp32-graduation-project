// App.cpp
#include "App.h"

#include "Logger.h"

namespace {
constexpr const char* TAG_INPUT = "INPUT";
constexpr const char* TAG_SETTINGS = "SETTINGS";
constexpr const char* TAG_VFD_UI = "VFD_UI";

volatile bool mcpInterruptA = false;
volatile bool mcpInterruptB = false;

void IRAM_ATTR handleMcpInterruptA() {
    mcpInterruptA = true;
}

void IRAM_ATTR handleMcpInterruptB() {
    mcpInterruptB = true;
}

uint32_t keepFirstSeen(
    const DiagnosticCode* previousCodes,
    const uint32_t* previousFirstSeen,
    uint8_t previousCount,
    DiagnosticCode code,
    uint32_t now
) {
    for (uint8_t i = 0; i < previousCount; i++) {
        if (previousCodes[i] == code) {
            return previousFirstSeen[i];
        }
    }

    return now;
}

void addDiagnostic(
    DiagnosticsSnapshot& diagnostics,
    const DiagnosticCode* previousCodes,
    const uint32_t* previousFirstSeen,
    uint8_t previousCount,
    DiagnosticSeverity severity,
    DiagnosticCode code,
    const char* title,
    const char* details,
    const char* recommendation,
    uint32_t now
) {
    if (diagnostics.itemCount >= MAX_DIAGNOSTIC_ITEMS) {
        return;
    }

    DiagnosticItem& item = diagnostics.items[diagnostics.itemCount++];
    item.severity = severity;
    item.code = code;
    item.active = true;
    strncpy(item.title, title, sizeof(item.title) - 1);
    item.title[sizeof(item.title) - 1] = '\0';
    strncpy(item.details, details, sizeof(item.details) - 1);
    item.details[sizeof(item.details) - 1] = '\0';
    strncpy(item.recommendation, recommendation, sizeof(item.recommendation) - 1);
    item.recommendation[sizeof(item.recommendation) - 1] = '\0';
    item.firstSeenMs = keepFirstSeen(previousCodes, previousFirstSeen, previousCount, code, now);
    item.lastSeenMs = now;

    if (severity == DiagnosticSeverity::Error) {
        diagnostics.errorCount++;
    } else if (severity == DiagnosticSeverity::Warning) {
        diagnostics.warningCount++;
    }
}
}



void App::begin() {
    loadUserSettings();

    controller.begin(&hp, &vfd, &display, &tempSensors);
    climateAlgorithm.begin(&state, &controller);
    console.begin(&hp, &vfd, &tempSensors, &display, &state, &controller, &climateAlgorithm);
    display.begin();

    network.begin(state.settings.wifiEnabled);

    if (network.isConnected()) {
        console.startTelnet();
    }

    homeAssistant.begin(
        state.settings.wifiEnabled && state.settings.mqttEnabled,
        MQTT_HOST,
        MQTT_PORT,
        MQTT_USER,
        MQTT_PASSWORD,
        MQTT_CLIENT_ID,
        MQTT_BASE_TOPIC,
        &state,
        &controller
    );

    hp.connect(&Serial1, IS_SECONDARY_CONTROLLER, AC_LIN_RX1_PIN, AC_LIN_TX1_PIN);
    hp.setDebug(AC_DEBUG);

    vfd.begin(Serial2, RS485_RX2_PIN, RS485_TX2_PIN, RS485_BAUD, SERIAL_8E1);

    tempSensors.begin(TEMP_ONE_WIRE_PIN);

    configureIoExpanderInputs();
}


void App::update() {
    vfd.update();
    updateHeatPump();
    updateIoExpanderInputs();

    network.update();
    vfd.update();
    updateHeatPump();
    updateIoExpanderInputs();

    if (network.isConnected()) {
        console.startTelnet();
    }
    updateHeatPump();
    updateIoExpanderInputs();

    homeAssistant.update(state.settings.wifiEnabled && network.isConnected());
    vfd.update();
    updateHeatPump();
    updateIoExpanderInputs();

    console.update();
    vfd.update();
    updateHeatPump();

    if (state.settings.mqttEnabled != lastMqttEnabled) {
        lastMqttEnabled = state.settings.mqttEnabled;
        homeAssistant.setEnabled(state.settings.wifiEnabled && state.settings.mqttEnabled);
        scheduleUserSettingsSave();
    }

    updateIoExpanderInputs();
    updateHeatPump();

    tempSensors.update();
    updateHeatPump();
    updateIoExpanderInputs();

    const bool vfdCommandSent = updateVfdCommandSync();
    if (!vfdCommandSent) {
        updateVfdStatus();
    }
    vfd.update();
    updateHeatPump();
    updateIoExpanderInputs();

    updateDeviceState();
    updateModeTransition();
    climateAlgorithm.update();
    vfd.update();
    updateDeviceState();
    display.update(state, climateAlgorithm.getSettings());
    updateDeferredSettingsSave();
    vfd.update();
    updateHeatPump();
    updateIoExpanderInputs();

    updateDeviceState();
}


void App::updateHeatPump() {
    hp.waitForFrame();
    hp.sendPendingFrame();
}


void App::updateVfdStatus() {
    if (!state.settings.vfdPollingEnabled) {
        return;
    }

    if (vfdSyncState == VfdSyncState::WaitingWriteAck) {
        return;
    }

    const unsigned long now = millis();
    const unsigned long lastWriteAckMs = vfd.getLastWriteAckMs();
    if (lastWriteAckMs != 0 && lastWriteAckMs != lastObservedVfdWriteAckMs) {
        lastObservedVfdWriteAckMs = lastWriteAckMs;
        startVfdFastVerify();
    }

    if (vfdFastVerifyActive && now - vfdFastVerifyStartedMs > AppConfig::VFD_FAST_VERIFY_DURATION_MS) {
        vfdFastVerifyActive = false;
    }

    if (vfdFastVerifyActive) {
        if (now - lastVfdFastVerifyPollMs < AppConfig::VFD_FAST_VERIFY_POLL_INTERVAL_MS) {
            return;
        }
        lastVfdFastVerifyPollMs = now;
        lastVfdStatusPollMs = now;
        vfd.pollStatus();
        vfd.update();
        return;
    }

    const bool vfdLinkUnstable = vfd.getConsecutiveErrorCount() >= 5 || vfd.hasCommunicationError();
    const unsigned long pollInterval = vfdLinkUnstable
        ? AppConfig::VFD_ERROR_POLL_BACKOFF_MS
        : AppConfig::VFD_STATUS_POLL_INTERVAL_MS;
    if (now - lastVfdStatusPollMs < pollInterval) {
        return;
    }

    if (now - lastVfdCommandSyncMs < AppConfig::VFD_POLL_AFTER_COMMAND_GUARD_MS) {
        return;
    }

    lastVfdStatusPollMs = now;
    vfd.pollStatus();
    vfd.update();
}


bool App::updateVfdCommandSync() {
    if (vfdSyncState == VfdSyncState::Idle) {
        return false;
    }

    if (state.controllerState.mode != DeviceMode::Manual) {
        vfdSyncState = VfdSyncState::Idle;
        return false;
    }

    const unsigned long now = millis();

    if (vfdSyncState == VfdSyncState::WaitingWriteAck) {
        if (vfd.hasRecentWriteAck(pendingVfdAddress, pendingVfdValue, pendingVfdStartedMs)) {
            Logger::debugf(
                TAG_VFD_UI,
                "VFD command accepted address=0x%04X value=0x%04X desiredPower=%u desiredStep=%u",
                pendingVfdAddress,
                pendingVfdValue,
                pendingVfdDesiredPower ? 1 : 0,
                pendingVfdDesiredStep
            );
            startVfdFastVerify();
            vfdSyncState = VfdSyncState::WaitingStatusVerify;
            return false;
        }

        if (now - pendingVfdLastSendMs < AppConfig::VFD_COMMAND_ACK_TIMEOUT_MS) {
            return false;
        }

        if (pendingVfdRetryCount < AppConfig::VFD_COMMAND_RETRY_LIMIT) {
            pendingVfdRetryCount++;
            Logger::warningf(
                TAG_VFD_UI,
                "VFD command ack timeout, retry=%u address=0x%04X value=0x%04X",
                pendingVfdRetryCount,
                pendingVfdAddress,
                pendingVfdValue
            );
            return sendPendingVfdCommand("display-sync-retry");
        }

        Logger::warningf(
            TAG_VFD_UI,
            "VFD command failed: no write ack address=0x%04X value=0x%04X",
            pendingVfdAddress,
            pendingVfdValue
        );
        vfdSyncState = VfdSyncState::Failed;
        return false;
    }

    if (vfdSyncState == VfdSyncState::WaitingStatusVerify) {
        if (isPendingVfdStatusVerified()) {
            Logger::debugf(
                TAG_VFD_UI,
                "VFD command verified address=0x%04X desiredPower=%u desiredStep=%u actualRunning=%u actualStep=%u actualHz=%.1f requestedHz=%.1f",
                pendingVfdAddress,
                pendingVfdDesiredPower ? 1 : 0,
                pendingVfdDesiredStep,
                vfd.isRunning() ? 1 : 0,
                vfd.getActualStep(),
                vfd.hasActualFrequency() ? vfd.getActualFrequencyHz() : -1.0f,
                vfd.hasRequestedFrequency() ? vfd.getRequestedFrequencyHz() : -1.0f
            );
            vfdSyncState = VfdSyncState::Idle;
            vfdFastVerifyActive = false;
            if (!isVfdDesiredStateReached()) {
                requestVfdCommandSync("display-sync next");
            }
        } else if (now - pendingVfdStartedMs >= AppConfig::VFD_COMMAND_VERIFY_TIMEOUT_MS) {
            Logger::warningf(
                TAG_VFD_UI,
                "VFD command accepted but status not verified desiredPower=%u desiredStep=%u actualRunning=%u actualStep=%u actualHz=%.1f",
                pendingVfdDesiredPower ? 1 : 0,
                pendingVfdDesiredStep,
                vfd.isRunning() ? 1 : 0,
                vfd.getActualStep(),
                vfd.hasActualFrequency() ? vfd.getActualFrequencyHz() : -1.0f
            );
            vfdSyncState = VfdSyncState::Idle;
            vfdFastVerifyActive = false;
            if (!isVfdDesiredStateReached()) {
                requestVfdCommandSync("display-sync next after verify timeout");
            }
        }
        return false;
    }

    if (vfdSyncState == VfdSyncState::Failed) {
        vfdSyncState = VfdSyncState::Idle;
    }

    return false;
}


void App::updateDeviceState() {
    state.uptimeMs = millis();
    const uint32_t uptimeSeconds = state.uptimeMs / 1000UL;

    if (uptimeSeconds != lastUptimeSecond) {
        lastUptimeSecond = uptimeSeconds;
        state.uptimeSeconds = uptimeSeconds;
        state.uptimeHours = uptimeSeconds / 3600UL;
        state.uptimeMinutes = (uptimeSeconds / 60UL) % 60UL;
        state.uptimeSecondPart = uptimeSeconds % 60UL;
        snprintf(
            state.uptimeText,
            sizeof(state.uptimeText),
            "%02u:%02u:%02u",
            state.uptimeHours,
            state.uptimeMinutes,
            state.uptimeSecondPart
        );
    }

    state.wifiConnected = network.isConnected();
    state.ip = network.getIp();

    state.ac.bound = hp.isBound();
    state.ac.powerOn = hp.getOnOff();
    state.ac.temperature = hp.getTemp();
    state.ac.controllerTemp = hp.getControllerTemp();
    state.ac.mode = hp.getMode();
    state.ac.fanMode = hp.getFanMode();
    state.ac.primaryController = hp.isPrimaryController();
    state.ac.controllerAddress = hp.getControllerAddress();
    state.ac.seenPrimaryController = hp.hasSeenPrimaryController();
    state.ac.seenSecondaryController = hp.hasSeenSecondaryController();
    state.ac.updatePending = hp.updatePending();
    state.ac.framePending = hp.hasPendingFrame();
    state.ac.debugEnabled = hp.debugPrint;
    state.ac.hasAnyFrame = hp.hasAnyFrame();
    state.ac.lastAnyFrameAgeMs = hp.hasAnyFrame() ? hp.getLastAnyFrameAgeMs() : 0;
    state.ac.lastFrameSourceAddress = hp.getLastFrameSourceAddress();
    state.ac.lastFrameDestinationAddress = hp.getLastFrameDestinationAddress();
    state.ac.lastFrameType = hp.getLastFrameMessageType();
    const bool acHasFrame = hp.hasReceivedFrame();
    state.ac.hasReceivedFrame = acHasFrame;
    state.ac.lastFrameAgeMs = acHasFrame ? hp.getLastFrameAgeMs() : 0;
    state.ac.communicationError = acHasFrame && hp.hasCommunicationError();
    state.ac.consecutiveErrorCount = acHasFrame ? hp.getConsecutiveErrorCount() : 0;
    state.ac.errorCount = hp.getErrorCount();
    state.ac.updateFields = hp.getUpdateFields();

    state.temperatures.sensorCount = tempSensors.getSensorCount();
    for (uint8_t i = 0; i < TEMP_MAX_SENSORS; i++) {
        state.temperatures.values[i] = tempSensors.getTemperatureC(i);
        const TempSensorEntry* entry = tempSensors.getEntry(i);
        if (entry != nullptr) {
            memcpy(state.temperatures.sensors[i].address, entry->address, sizeof(DeviceAddress));
            state.temperatures.sensors[i].role = entry->role;
            state.temperatures.sensors[i].enabled = entry->enabled;
            state.temperatures.sensors[i].connected = entry->connected;
            state.temperatures.sensors[i].hasTemperature = entry->hasTemperature;
            state.temperatures.sensors[i].temperatureC = entry->temperatureC;
            state.temperatures.sensors[i].missedScanCount = entry->missedScanCount;
            state.temperatures.sensors[i].failedReadCount = entry->failedReadCount;
        } else {
            state.temperatures.sensors[i] = {};
        }
    }
    state.environment.hasIndoorTemp = tempSensors.getTemperatureByRole(TempSensorRole::Indoor, state.environment.indoorTempC);
    state.environment.hasOutdoorTemp = tempSensors.getTemperatureByRole(TempSensorRole::Outdoor, state.environment.outdoorTempC);
    if (state.environment.hasIndoorTemp) {
        const uint8_t indoorControllerTemp = (uint8_t)constrain((int)(state.environment.indoorTempC + 0.5f), 0, 63);
        hp.setControllerTempOverride(true, indoorControllerTemp);
        state.ac.controllerTemp = indoorControllerTemp;
    } else {
        hp.setControllerTempOverride(false, 0);
        state.ac.controllerTemp = hp.getControllerTemp();
    }

    state.vfd.initialized = vfd.isInitialized();
    state.vfd.online = vfd.isOnline();
    state.vfd.everOnline = vfd.hasEverBeenOnline();
    state.vfd.communicationError = vfd.hasCommunicationError();
    state.vfd.hasStatusWord = vfd.hasStatusWord();
    state.vfd.statusWord = vfd.getStatusWord();
    state.vfd.running = vfd.isRunning();
    state.vfd.commandedRunning = vfd.isCommandedRunning();
    state.vfd.lastAction = vfd.getLastAction();
    state.vfd.hasRequestedFrequency = vfd.hasRequestedFrequency();
    state.vfd.requestedFrequencyHz = vfd.getRequestedFrequencyHz();
    state.vfd.hasActualFrequency = vfd.hasActualFrequency();
    state.vfd.actualFrequencyHz = vfd.getActualFrequencyHz();
    state.vfd.actualStep = vfd.getActualStep();
    state.vfd.requestCount = vfd.getRequestCount();
    state.vfd.okCount = vfd.getOkCount();
    state.vfd.errorCount = vfd.getErrorCount();
    state.vfd.consecutiveErrorCount = vfd.getConsecutiveErrorCount();
    state.vfd.lastToken = vfd.getLastToken();
    state.vfd.lastErrorCode = vfd.getLastErrorCode();
    state.vfd.hasActivity = vfd.hasActivity();
    state.vfd.lastActivityAgeMs = vfd.getLastActivityAgeMs();

    const AutoControlStatus autoStatus = climateAlgorithm.getStatus();
    const AutoControlSettings autoSettings = climateAlgorithm.getSettings();
    state.ventilation.baseRequirementStep = autoStatus.baseVentRequirementStep;
    state.ventilation.bathCompStep = autoStatus.bathCompStep;
    state.ventilation.hoodCompStep = autoStatus.hoodCompStep;
    state.ventilation.exhaustCompRequirementStep = autoStatus.exhaustCompRequirementStep;
    state.ventilation.coolingRequirementStep = autoStatus.coolingVentRequirementStep;
    state.ventilation.requestedStepBeforeLimit = autoStatus.requestedVentStepBeforeLimit;
    state.ventilation.requestedStepAfterLimit = autoStatus.requestedVentStepAfterLimit;
    state.ventilation.desiredVfdPower = autoStatus.desiredVfdPower;
    state.ventilation.desiredVfdStep = autoStatus.desiredVfdStep;
    state.ventilation.coldOutdoorLimitActive = autoStatus.coldOutdoorLimitActive;
    state.ventilation.compensationUpdateIntervalSec = autoSettings.ventCompensationUpdateIntervalSec;
    state.ventilation.compensationUpdateRemainingSec = autoStatus.ventCompensationUpdateRemainingSec;
    state.ventilation.compensationOffDelaySec = autoSettings.ventCompensationOffDelaySec;
    state.ventilation.compensationOffDelayRemainingSec = autoStatus.ventCompensationOffDelayRemainingSec;
    state.ventilation.compensationOffDelayActive = autoStatus.ventCompensationOffDelayActive;
    state.ventilation.additiveCompensation = autoSettings.additiveVentCompensation;
    strncpy(state.ventilation.reason, autoStatus.reason, sizeof(state.ventilation.reason) - 1);
    state.ventilation.reason[sizeof(state.ventilation.reason) - 1] = '\0';

    state.input.ioExpanderReady = ioExpanderReady;
    state.input.buttonBackPressed = buttonBack.isPressed();
    state.input.buttonLeftPressed = buttonLeft.isPressed();
    state.input.buttonRightPressed = buttonRight.isPressed();
    state.input.buttonOkPressed = buttonOk.isPressed();

    state.display.ready = display.isReady();
    state.display.pageIndex = display.getPageIndex();
    state.display.pageName = display.getPageName();

    state.homeAssistant.enabled = homeAssistant.isEnabled();
    state.homeAssistant.connected = homeAssistant.isConnected();
    state.homeAssistant.reconnectCount = homeAssistant.getReconnectCount();
    state.homeAssistant.publishCount = homeAssistant.getPublishCount();
    state.homeAssistant.commandCount = homeAssistant.getCommandCount();
    state.homeAssistant.hasPublished = homeAssistant.hasPublished();
    state.homeAssistant.lastPublishAgeMs = homeAssistant.getLastPublishAgeMs();

    updateDiagnostics(autoStatus, autoSettings);
    logVfdStateChanges();
}


void App::updateDiagnostics(const AutoControlStatus& autoStatus, const AutoControlSettings& autoSettings) {
    DiagnosticCode previousCodes[MAX_DIAGNOSTIC_ITEMS];
    uint32_t previousFirstSeen[MAX_DIAGNOSTIC_ITEMS];
    uint8_t previousCount = 0;
    for (uint8_t i = 0; i < state.diagnostics.itemCount && i < MAX_DIAGNOSTIC_ITEMS; i++) {
        if (!state.diagnostics.items[i].active) {
            continue;
        }
        previousCodes[previousCount] = state.diagnostics.items[i].code;
        previousFirstSeen[previousCount] = state.diagnostics.items[i].firstSeenMs;
        previousCount++;
    }

    static DiagnosticsSnapshot diagnostics;
    diagnostics = DiagnosticsSnapshot();
    diagnostics.updatedAtMs = state.uptimeMs;

    auto add = [&](DiagnosticSeverity severity, DiagnosticCode code, const char* title, const char* details, const char* recommendation) {
        addDiagnostic(diagnostics, previousCodes, previousFirstSeen, previousCount, severity, code, title, details, recommendation, state.uptimeMs);
    };

    char details[64] = {};
    const bool acExpected = state.controllerState.mode == DeviceMode::Auto
        ? (autoSettings.allowAcCooling || autoSettings.allowAcHeating || autoSettings.keepAcFanOnInAuto || autoSettings.keepAcFanOnWithVent)
        : state.settings.manualAcPower;
    if (acExpected && state.ac.hasReceivedFrame && (!state.ac.bound || state.ac.communicationError)) {
        snprintf(details, sizeof(details), "bound=%u err=%u last=%lus", state.ac.bound ? 1 : 0, state.ac.communicationError ? 1 : 0, state.ac.hasReceivedFrame ? state.ac.lastFrameAgeMs / 1000UL : 0UL);
        add(DiagnosticSeverity::Error, DiagnosticCode::AcLinkLost, "AC LINK LOST", details, "Check AC bus wiring / restart AC");
    }

    if (state.ac.hasAnyFrame && state.ac.lastAnyFrameAgeMs < 10000UL && (!state.ac.hasReceivedFrame || state.ac.lastFrameAgeMs > 10000UL)) {
        snprintf(details, sizeof(details), "bus active dst=0x%02X local=0x%02X", state.ac.lastFrameDestinationAddress, state.ac.controllerAddress);
        add(DiagnosticSeverity::Warning, DiagnosticCode::AcBusActiveButDeviceNotAddressed, "AC BUS NO ESP", details, "Restart AC with ESP connected");
    }

    if (state.controllerState.mode == DeviceMode::Auto && autoStatus.needCooling && !autoStatus.ventCoolingAllowedNow && !autoStatus.acCoolingAllowedNow) {
        snprintf(details, sizeof(details), "dT=%.1f AC=%u VCool=%u", autoStatus.deltaTempC, autoStatus.acCoolingAllowedNow ? 1 : 0, autoStatus.ventCoolingAllowedNow ? 1 : 0);
        add(DiagnosticSeverity::Error, DiagnosticCode::CoolingRequiredButUnavailable, "COOLING UNAVAILABLE", details, "Check AC/VFD or auto settings");
    }

    if (state.controllerState.mode == DeviceMode::Auto && autoStatus.needHeating && !autoStatus.acHeatingAllowedNow) {
        snprintf(details, sizeof(details), "dT=%.1f AC heat=%u", autoStatus.deltaTempC, autoStatus.acHeatingAllowedNow ? 1 : 0);
        add(DiagnosticSeverity::Error, DiagnosticCode::HeatingRequiredButUnavailable, "HEATING UNAVAILABLE", details, "Check AC link / heat setting");
    }

    bool indoorAssigned = false;
    bool outdoorAssigned = false;
    uint8_t unassignedConnected = 0;
    for (uint8_t i = 0; i < state.temperatures.sensorCount && i < TEMP_MAX_SENSORS; i++) {
        const auto& sensor = state.temperatures.sensors[i];
        if (!sensor.enabled) {
            continue;
        }
        if (sensor.role == TempSensorRole::Indoor) {
            indoorAssigned = true;
        } else if (sensor.role == TempSensorRole::Outdoor) {
            outdoorAssigned = true;
        } else if (sensor.connected && sensor.role == TempSensorRole::Unknown) {
            unassignedConnected++;
        }
    }

    if (!indoorAssigned || !state.environment.hasIndoorTemp) {
        snprintf(details, sizeof(details), "assigned=%u valid=%u", indoorAssigned ? 1 : 0, state.environment.hasIndoorTemp ? 1 : 0);
        add(DiagnosticSeverity::Error, DiagnosticCode::IndoorSensorMissing, "INDOOR TEMP MISSING", details, "Assign/check indoor DS18B20");
    }

    if (autoSettings.allowVentCooling && (!outdoorAssigned || !state.environment.hasOutdoorTemp)) {
        snprintf(details, sizeof(details), "assigned=%u valid=%u", outdoorAssigned ? 1 : 0, state.environment.hasOutdoorTemp ? 1 : 0);
        add(DiagnosticSeverity::Error, DiagnosticCode::OutdoorSensorMissing, "OUTDOOR TEMP MISSING", details, "Assign/check outdoor DS18B20");
    }

    if (state.vfd.consecutiveErrorCount >= 5 || (state.vfd.communicationError && state.vfd.hasActivity && state.vfd.lastActivityAgeMs > 10000UL)) {
        snprintf(details, sizeof(details), "consec=%u last=0x%02X", state.vfd.consecutiveErrorCount, state.vfd.lastErrorCode);
        add(DiagnosticSeverity::Error, DiagnosticCode::VfdLinkLost, "VFD LINK LOST", details, "Check RS485 / VFD power");
    } else if (state.vfd.consecutiveErrorCount >= 3) {
        snprintf(details, sizeof(details), "consec=%u total=%lu", state.vfd.consecutiveErrorCount, (unsigned long)state.vfd.errorCount);
        add(DiagnosticSeverity::Warning, DiagnosticCode::VfdCommunicationUnstable, "VFD LINK UNSTABLE", details, "Check RS485 timing/wiring");
    }

    if (state.controllerState.mode == DeviceMode::Safe) {
        add(DiagnosticSeverity::Error, DiagnosticCode::AutoSafeModeActive, "AUTO SAFE ACTIVE", autoStatus.reason, "Fix critical input data");
    }

    if (!state.wifiConnected) {
        add(DiagnosticSeverity::Warning, DiagnosticCode::WifiDisconnected, "WIFI DISCONNECTED", "local control active", "Check Wi-Fi router/settings");
    }

    if (state.homeAssistant.enabled && !state.homeAssistant.connected) {
        add(DiagnosticSeverity::Warning, DiagnosticCode::HomeAssistantDisconnected, "HA DISCONNECTED", "MQTT bridge offline", "Check MQTT broker/network");
    }

    if (unassignedConnected > 0) {
        snprintf(details, sizeof(details), "unknown connected=%u", unassignedConnected);
        add(DiagnosticSeverity::Warning, DiagnosticCode::UnassignedTemperatureSensorsDetected, "TEMP SENSOR UNKNOWN", details, "Assign sensors on Temp page");
    }

    if (autoSettings.targetTempC < 16.0f || autoSettings.targetTempC > 30.0f) {
        snprintf(details, sizeof(details), "target=%.1f", autoSettings.targetTempC);
        add(DiagnosticSeverity::Error, DiagnosticCode::SettingsInvalid, "AUTO SETTINGS INVALID", details, "Reset or fix auto settings");
    }

    state.diagnostics = diagnostics;
    state.controllerState.warningCount = diagnostics.warningCount;
    state.controllerState.errorCount = diagnostics.errorCount;
}


void App::configureIoExpanderInputs() {
    ioExpanderReady = ioExpander.begin(Wire, MCP23017_ADDRESS, I2C_SDA_PIN, I2C_SCL_PIN);

    if (!ioExpanderReady) {
        return;
    }

    ioExpander.pinMode(MCP_PIN_GPA5, INPUT_PULLUP);
    ioExpander.pinMode(MCP_PIN_GPA6, INPUT_PULLUP);
    ioExpander.pinMode(MCP_PIN_GPA7, INPUT_PULLUP);
    ioExpander.pinMode(MCP_PIN_EXHAUST_VENT, INPUT_PULLUP);
    ioExpander.pinMode(MCP_BUTTON_BACK_PIN, INPUT_PULLUP);
    ioExpander.pinMode(MCP_BUTTON_LEFT_PIN, INPUT_PULLUP);
    ioExpander.pinMode(MCP_BUTTON_RIGHT_PIN, INPUT_PULLUP);
    ioExpander.pinMode(MCP_BUTTON_OK_PIN, INPUT_PULLUP);
    ioExpander.configureInterruptOutputs(false);
    ioExpander.enableInterruptOnChange(MCP_PIN_GPA5);
    ioExpander.enableInterruptOnChange(MCP_PIN_GPA6);
    ioExpander.enableInterruptOnChange(MCP_PIN_GPA7);
    ioExpander.enableInterruptOnChange(MCP_PIN_EXHAUST_VENT);
    ioExpander.enableInterruptOnChange(MCP_BUTTON_BACK_PIN);
    ioExpander.enableInterruptOnChange(MCP_BUTTON_LEFT_PIN);
    ioExpander.enableInterruptOnChange(MCP_BUTTON_RIGHT_PIN);
    ioExpander.enableInterruptOnChange(MCP_BUTTON_OK_PIN);
    ioExpander.clearInterrupts();

    lastGpa5State = ioExpander.digitalRead(MCP_PIN_GPA5);
    lastGpa6State = ioExpander.digitalRead(MCP_PIN_GPA6);
    lastGpa7State = ioExpander.digitalRead(MCP_PIN_GPA7);
    lastExhaustVentState = ioExpander.digitalRead(MCP_PIN_EXHAUST_VENT);
    rawExhaustVentState = lastExhaustVentState;
    rawExhaustVentChangedMs = millis();
    exhaustDebounceActive = false;
    updateVentilationInputs(lastGpa5State, lastGpa6State, lastGpa7State, lastExhaustVentState);
    buttonBack.begin(true, 50, 500);
    buttonLeft.begin(true, 50, 500);
    buttonRight.begin(true, 50, 500);
    buttonOk.begin(true, 50, 500);

    pinMode(MCP_INT_A_PIN, INPUT);
    pinMode(MCP_INT_B_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(MCP_INT_A_PIN), handleMcpInterruptA, FALLING);
    attachInterrupt(digitalPinToInterrupt(MCP_INT_B_PIN), handleMcpInterruptB, FALLING);

    Logger::info(TAG_INPUT, "GPA0-GPA3 buttons and GPA4-GPA7 ventilation inputs started");
}


void App::updateIoExpanderInputs() {
    if (!ioExpanderReady) {
        return;
    }

    const unsigned long now = millis();
    const bool pollButtons = (buttonsActive || exhaustDebounceActive)
        && now - lastButtonPollMs >= AppConfig::BUTTON_POLL_INTERVAL_MS;

    if (!mcpInterruptA && !mcpInterruptB && !pollButtons) {
        return;
    }

    if (pollButtons) {
        lastButtonPollMs = now;
    }

    noInterrupts();
    const bool interruptA = mcpInterruptA;
    const bool interruptB = mcpInterruptB;
    mcpInterruptA = false;
    mcpInterruptB = false;
    interrupts();

    if (interruptA || interruptB) {
        buttonsActive = true;
        lastButtonPollMs = now;
    }

    if (interruptA || interruptB || pollButtons) {
        processIoExpanderPort();
    }
}


void App::processIoExpanderPort() {
    uint16_t portState = 0;

    if (!ioExpander.readPort(portState)) {
        Logger::warning(TAG_INPUT, "Failed to read MCP23017 port after interrupt");
        return;
    }

    const int gpa5State = (portState & (1 << MCP_PIN_GPA5)) ? HIGH : LOW;
    const int gpa6State = (portState & (1 << MCP_PIN_GPA6)) ? HIGH : LOW;
    const int gpa7State = (portState & (1 << MCP_PIN_GPA7)) ? HIGH : LOW;
    const int exhaustVentState = (portState & (1 << MCP_PIN_EXHAUST_VENT)) ? HIGH : LOW;
    const unsigned long now = millis();

    if (gpa5State != lastGpa5State) {
        lastGpa5State = gpa5State;
        handleIoExpanderInputChange(MCP_PIN_GPA5, gpa5State);
    }

    if (gpa6State != lastGpa6State) {
        lastGpa6State = gpa6State;
        handleIoExpanderInputChange(MCP_PIN_GPA6, gpa6State);
    }

    if (gpa7State != lastGpa7State) {
        lastGpa7State = gpa7State;
        handleIoExpanderInputChange(MCP_PIN_GPA7, gpa7State);
    }

    if (exhaustVentState != rawExhaustVentState) {
        rawExhaustVentState = exhaustVentState;
        rawExhaustVentChangedMs = now;
        exhaustDebounceActive = true;
        Logger::debugf(TAG_INPUT, "Raw bathroom exhaust input: %s", exhaustVentState == LOW ? "ON" : "OFF");
    }

    if (exhaustDebounceActive && now - rawExhaustVentChangedMs >= AppConfig::EXHAUST_INPUT_DEBOUNCE_MS) {
        exhaustDebounceActive = false;
        if (rawExhaustVentState != lastExhaustVentState) {
            lastExhaustVentState = rawExhaustVentState;
            handleIoExpanderInputChange(MCP_PIN_EXHAUST_VENT, lastExhaustVentState);
        }
    }

    updateVentilationInputs(gpa5State, gpa6State, gpa7State, lastExhaustVentState);

    const bool backPressed = (portState & (1 << MCP_BUTTON_BACK_PIN)) == 0;
    const bool leftPressed = (portState & (1 << MCP_BUTTON_LEFT_PIN)) == 0;
    const bool rightPressed = (portState & (1 << MCP_BUTTON_RIGHT_PIN)) == 0;
    const bool okPressed = (portState & (1 << MCP_BUTTON_OK_PIN)) == 0;

    handleButtonEvent("BACK", buttonBack.update(backPressed, now));
    handleButtonEvent("LEFT", buttonLeft.update(leftPressed, now));
    handleButtonEvent("RIGHT", buttonRight.update(rightPressed, now));
    handleButtonEvent("OK", buttonOk.update(okPressed, now));

    buttonsActive = buttonBack.isPressed()
        || buttonLeft.isPressed()
        || buttonRight.isPressed()
        || buttonOk.isPressed()
        || buttonBack.isActive()
        || buttonLeft.isActive()
        || buttonRight.isActive()
        || buttonOk.isActive();
}


void App::handleIoExpanderInputChange(uint8_t pin, int currentState) {
    if (pin == MCP_PIN_EXHAUST_VENT) {
        Logger::infof(TAG_INPUT, "Bathroom exhaust: %s", currentState == LOW ? "ON" : "OFF");
        return;
    }

    Logger::debugf(
        TAG_INPUT,
        "GPA%u changed to %s",
        pin,
        currentState == LOW ? "LOW (grounded)" : "HIGH (pull-up)"
    );
}


void App::handleButtonEvent(const char* name, ButtonInput::Event event) {
    if (event == ButtonInput::Event::None) {
        return;
    }

    DisplayUi::Button button = DisplayUi::Button::Ok;
    if (strcmp(name, "BACK") == 0) {
        button = DisplayUi::Button::Back;
    } else if (strcmp(name, "LEFT") == 0) {
        button = DisplayUi::Button::Left;
    } else if (strcmp(name, "RIGHT") == 0) {
        button = DisplayUi::Button::Right;
    } else if (strcmp(name, "OK") == 0) {
        button = DisplayUi::Button::Ok;
    }

    const bool longPress = event == ButtonInput::Event::LongPress;
    Logger::debugf(TAG_INPUT, "Button %s %s press", name, longPress ? "long" : "short");
    const float previousTargetTemp = state.environment.targetIndoorTempC;
    const DisplayUi::Action action = display.handleButton(button, longPress, state, climateAlgorithm.getSettings());

    if (action.settingsChanged) {
        scheduleUserSettingsSave();
        if (fabsf(previousTargetTemp - state.environment.targetIndoorTempC) > 0.01f) {
            climateAlgorithm.setTargetTemp(state.environment.targetIndoorTempC);
        }
    }

    switch (action.type) {
        case DisplayUi::ActionType::AcPower:
            controller.setAcPower(action.boolValue);
            break;
        case DisplayUi::ActionType::AcMode:
            controller.setAcMode(action.uintValue);
            controller.setAcTemperature(state.settings.manualAcTemperature);
            break;
        case DisplayUi::ActionType::AcTemperature:
            controller.setAcTemperature(action.uintValue);
            break;
        case DisplayUi::ActionType::AcFan:
            controller.setAcFanMode(action.uintValue);
            break;
        case DisplayUi::ActionType::VfdStop:
            requestVfdCommandSync("display stop");
            break;
        case DisplayUi::ActionType::VfdForward:
            requestVfdCommandSync("display forward");
            break;
        case DisplayUi::ActionType::VfdSetFrequency:
            requestVfdCommandSync("display frequency");
            break;
        case DisplayUi::ActionType::AutoSettings:
            climateAlgorithm.setSettings(action.autoSettings);
            break;
        case DisplayUi::ActionType::TempAssignRole:
            if (!tempSensors.assignRole(action.uintValue, action.tempRole)) {
                Logger::warningf(TAG_SETTINGS, "Temperature role assign failed: index=%u", action.uintValue);
            }
            break;
        case DisplayUi::ActionType::TempForget:
            if (!tempSensors.forget(action.uintValue)) {
                Logger::warningf(TAG_SETTINGS, "Temperature sensor forget failed: index=%u", action.uintValue);
            }
            break;
        case DisplayUi::ActionType::TempForceRead:
            tempSensors.forceRead();
            Logger::info(TAG_SETTINGS, "Temperature force read requested");
            break;
        case DisplayUi::ActionType::TempScan:
            tempSensors.rescan();
            Logger::info(TAG_SETTINGS, "Temperature bus scan requested");
            break;
        case DisplayUi::ActionType::TempSwap:
            if (!tempSensors.swapRoles()) {
                Logger::warning(TAG_SETTINGS, "Temperature role swap failed");
            }
            break;
        case DisplayUi::ActionType::SystemSettings:
            network.setEnabled(state.settings.wifiEnabled);
            homeAssistant.setEnabled(state.settings.wifiEnabled && state.settings.mqttEnabled);
            lastMqttEnabled = state.settings.mqttEnabled;
            if (action.uintValue == 2) {
                saveUserSettings();
            }
            break;
        case DisplayUi::ActionType::SystemSaveNow:
            saveUserSettings();
            break;
        case DisplayUi::ActionType::SystemReboot:
            saveUserSettings();
            controller.restart(250);
            break;
        case DisplayUi::ActionType::None:
            break;
    }
}


void App::requestVfdCommandSync(const char* reason) {
    const AutoControlSettings autoSettings = climateAlgorithm.getSettings();
    const bool manualVentAssistEnabled = state.controllerState.mode == DeviceMode::Manual
        && autoSettings.manualVentCompensationEnabled;
    const uint8_t userStep = state.settings.manualVfdPower
        ? (state.settings.manualVfdStep > 6 ? 6 : state.settings.manualVfdStep)
        : 0;
    const uint8_t ventAssistStep = manualVentAssistEnabled
        ? state.ventilation.requestedStepAfterLimit
        : 0;
    const uint8_t effectiveStep = max(userStep, ventAssistStep);
    const bool effectivePower = effectiveStep > 0;

    if (!effectivePower) {
        requestVfdCommandSync(reason, 0x2000, 0x0005, false, 0, 0.0f);
        return;
    }

    const float desiredHz = vfdStepToHz(effectiveStep);
    const uint16_t desiredValue = (uint16_t)lroundf(desiredHz * 100.0f);
    const bool requestedFrequencyMatches = vfd.hasRequestedFrequency()
        && fabsf(vfd.getRequestedFrequencyHz() - desiredHz) <= 0.5f;

    if (!requestedFrequencyMatches) {
        requestVfdCommandSync(reason, 0x2001, desiredValue, true, effectiveStep, desiredHz);
        return;
    }

    if (!vfd.isRunning()) {
        requestVfdCommandSync(reason, 0x2000, 0x0001, true, effectiveStep, desiredHz);
        return;
    }

    if (vfd.hasActualFrequency() && fabsf(vfd.getActualFrequencyHz() - desiredHz) > 0.75f) {
        requestVfdCommandSync(reason, 0x2001, desiredValue, true, effectiveStep, desiredHz);
        return;
    }

    Logger::tracef(TAG_VFD_UI, "VFD sync not needed: %s", reason);
}


void App::startVfdFastVerify() {
    vfdFastVerifyActive = true;
    vfdFastVerifyStartedMs = millis();
    lastVfdFastVerifyPollMs = 0;
}


void App::requestVfdCommandSync(const char* reason, uint16_t address, uint16_t value, bool desiredPower, uint8_t desiredStep, float desiredHz) {
    const unsigned long now = millis();
    const unsigned long duplicateSinceMs = now > AppConfig::VFD_DUPLICATE_WRITE_SUPPRESS_MS
        ? now - AppConfig::VFD_DUPLICATE_WRITE_SUPPRESS_MS
        : 0;
    if (vfd.hasRecentWriteAck(address, value, duplicateSinceMs)) {
        Logger::debugf(
            TAG_VFD_UI,
            "VFD duplicate write suppressed: %s address=0x%04X value=0x%04X",
            reason,
            address,
            value
        );
        pendingVfdAddress = address;
        pendingVfdValue = value;
        pendingVfdDesiredPower = desiredPower;
        pendingVfdDesiredStep = desiredStep;
        pendingVfdDesiredHz = desiredHz;
        pendingVfdStartedMs = now;
        pendingVfdLastSendMs = now;
        pendingVfdRetryCount = AppConfig::VFD_COMMAND_RETRY_LIMIT;
        vfdSyncState = VfdSyncState::WaitingStatusVerify;
        startVfdFastVerify();
        return;
    }

    pendingVfdAddress = address;
    pendingVfdValue = value;
    pendingVfdDesiredPower = desiredPower;
    pendingVfdDesiredStep = desiredStep;
    pendingVfdDesiredHz = desiredHz;
    pendingVfdRetryCount = 0;
    pendingVfdStartedMs = now;
    pendingVfdLastSendMs = 0;
    vfdSyncState = VfdSyncState::WaitingWriteAck;

    Logger::debugf(
        TAG_VFD_UI,
        "VFD sync requested: %s address=0x%04X value=0x%04X desiredPower=%u desiredStep=%u desiredHz=%.1f",
        reason,
        pendingVfdAddress,
        pendingVfdValue,
        pendingVfdDesiredPower ? 1 : 0,
        pendingVfdDesiredStep,
        pendingVfdDesiredHz
    );

    if (!sendPendingVfdCommand("display")) {
        vfdSyncState = VfdSyncState::Failed;
    }
}


bool App::sendPendingVfdCommand(const char* source) {
    bool queued = false;
    if (pendingVfdAddress == 0x2000 && pendingVfdValue == 0x0001) {
        queued = controller.vfdForward(source, true);
    } else if (pendingVfdAddress == 0x2000 && pendingVfdValue == 0x0005) {
        queued = controller.vfdStop(source, true);
    } else if (pendingVfdAddress == 0x2001) {
        queued = controller.vfdSetFrequency(pendingVfdValue / 100.0f, source, true);
    } else {
        queued = vfd.writeRegister(pendingVfdAddress, pendingVfdValue);
    }

    if (queued) {
        pendingVfdLastSendMs = millis();
        lastVfdCommandSyncMs = pendingVfdLastSendMs;
    } else {
        Logger::warningf(
            TAG_VFD_UI,
            "VFD command queue failed source=%s address=0x%04X value=0x%04X",
            source,
            pendingVfdAddress,
            pendingVfdValue
        );
    }

    return queued;
}


bool App::isPendingVfdStatusVerified() const {
    if (pendingVfdAddress == 0x2000) {
        if (!pendingVfdDesiredPower) {
            return !vfd.isRunning()
                && (!vfd.hasActualFrequency() || vfd.getActualFrequencyHz() < 1.0f);
        }

        return vfd.isRunning()
            && vfd.hasActualFrequency()
            && fabsf(vfd.getActualFrequencyHz() - pendingVfdDesiredHz) <= 0.75f
            && vfd.getActualStep() == pendingVfdDesiredStep;
    }

    if (pendingVfdAddress == 0x2001) {
        return vfd.hasRequestedFrequency()
            && fabsf(vfd.getRequestedFrequencyHz() - pendingVfdDesiredHz) <= 0.5f;
    }

    return false;
}


bool App::isVfdDesiredStateReached() const {
    const AutoControlSettings autoSettings = climateAlgorithm.getSettings();
    const bool manualVentAssistEnabled = state.controllerState.mode == DeviceMode::Manual
        && autoSettings.manualVentCompensationEnabled;
    const uint8_t userStep = state.settings.manualVfdPower
        ? (state.settings.manualVfdStep > 6 ? 6 : state.settings.manualVfdStep)
        : 0;
    const uint8_t ventAssistStep = manualVentAssistEnabled
        ? state.ventilation.requestedStepAfterLimit
        : 0;
    const uint8_t effectiveStep = max(userStep, ventAssistStep);

    if (effectiveStep == 0) {
        if (!state.settings.manualVfdPower && ventAssistStep == 0) {
            return !vfd.isRunning() && (!vfd.hasActualFrequency() || vfd.getActualFrequencyHz() < 1.0f);
        }

        return vfd.isRunning();
    }

    const float desiredHz = vfdStepToHz(effectiveStep);
    return vfd.isRunning()
        && vfd.hasActualFrequency()
        && fabsf(vfd.getActualFrequencyHz() - desiredHz) <= 0.75f
        && vfd.getActualStep() == effectiveStep;
}


float App::vfdStepToHz(uint8_t step) const {
    if (step == 0) {
        return 0.0f;
    }

    if (step >= 6) {
        return 50.0f;
    }

    return 20.0f + (step - 1) * 6.0f;
}


void App::logVfdStateChanges() {
    const int16_t actualFreq10 = state.vfd.hasActualFrequency
        ? (int16_t)lroundf(state.vfd.actualFrequencyHz * 10.0f)
        : INT16_MIN;

    if (lastLoggedVfdDesiredPower == state.settings.manualVfdPower
        && lastLoggedVfdDesiredStep == state.settings.manualVfdStep
        && lastLoggedVfdRunning == state.vfd.running
        && lastLoggedVfdActualStep == state.vfd.actualStep
        && lastLoggedVfdActualFreq10 == actualFreq10
        && lastLoggedVfdOnline == state.vfd.online
        && lastLoggedVfdError == state.vfd.communicationError) {
        return;
    }

    lastLoggedVfdDesiredPower = state.settings.manualVfdPower;
    lastLoggedVfdDesiredStep = state.settings.manualVfdStep;
    lastLoggedVfdRunning = state.vfd.running;
    lastLoggedVfdActualStep = state.vfd.actualStep;
    lastLoggedVfdActualFreq10 = actualFreq10;
    lastLoggedVfdOnline = state.vfd.online;
    lastLoggedVfdError = state.vfd.communicationError;

    Logger::tracef(
        TAG_VFD_UI,
        "VFD state desiredPower=%u desiredStep=%u actualRunning=%u actualStep=%u actualHz=%.1f online=%u error=%u status=0x%04X",
        state.settings.manualVfdPower ? 1 : 0,
        state.settings.manualVfdStep,
        state.vfd.running ? 1 : 0,
        state.vfd.actualStep,
        state.vfd.hasActualFrequency ? state.vfd.actualFrequencyHz : -1.0f,
        state.vfd.online ? 1 : 0,
        state.vfd.communicationError ? 1 : 0,
        state.vfd.statusWord
    );
}


void App::updateVentilationInputs(int gpa5State, int gpa6State, int gpa7State, int exhaustState) {
    uint8_t hoodLevel = 0;
    if (gpa5State == LOW) {
        hoodLevel = 1;
    }
    if (gpa6State == LOW) {
        hoodLevel = 2;
    }
    if (gpa7State == LOW) {
        hoodLevel = 3;
    }

    const bool exhaustEnabled = exhaustState == LOW;

    if (state.environment.kitchenHoodLevel != hoodLevel) {
        state.environment.kitchenHoodLevel = hoodLevel;
        Logger::infof(TAG_INPUT, "Kitchen hood level updated from MCP23017: %u", hoodLevel);
    }

    if (state.environment.exhaustVentEnabled != exhaustEnabled) {
        state.environment.exhaustVentEnabled = exhaustEnabled;
        Logger::infof(TAG_INPUT, "Bathroom exhaust updated from MCP23017: %s", exhaustEnabled ? "ON" : "OFF");
    }
}


void App::loadUserSettings() {
    if (!preferences.begin(AppConfig::USER_SETTINGS_NAMESPACE, true)) {
        Logger::warning(TAG_SETTINGS, "Failed to open NVS for reading");
        return;
    }

    const uint8_t mode = preferences.getUChar("mode", static_cast<uint8_t>(state.settings.mode));
    const float targetTemp = preferences.getFloat("setTemp", state.settings.targetIndoorTempC);
    state.settings.mode = mode == static_cast<uint8_t>(DeviceMode::Manual)
        ? DeviceMode::Manual
        : (mode == static_cast<uint8_t>(DeviceMode::Disabled) ? DeviceMode::Disabled : DeviceMode::Auto);
    state.settings.wifiEnabled = preferences.getBool("wifiEnabled", true);
    state.settings.mqttEnabled = preferences.getBool("mqttEnabled", MQTT_ENABLED);
    state.settings.autoSaveEnabled = preferences.getBool("autoSave", true);
    state.settings.vfdPollingEnabled = preferences.getBool("vfdPolling", true);
    lastMqttEnabled = state.settings.mqttEnabled;
    state.settings.targetIndoorTempC = constrain(targetTemp, 16.0f, 30.0f);
    state.settings.manualAcPower = preferences.getBool("acPower", state.settings.manualAcPower);
    state.settings.manualAcMode = preferences.getUChar("acMode", state.settings.manualAcMode);
    if (state.settings.manualAcMode != 1 && state.settings.manualAcMode != 3 && state.settings.manualAcMode != 4 && state.settings.manualAcMode != 5) {
        state.settings.manualAcMode = 5;
    }
    state.settings.manualAcTemperature = preferences.getUChar("acTemp", state.settings.manualAcTemperature);
    state.settings.manualAcTemperature = constrain(state.settings.manualAcTemperature, (uint8_t)16, (uint8_t)30);
    state.settings.manualAcModeTemperatures[1] = preferences.getUChar("acTempFan", state.settings.manualAcTemperature);
    state.settings.manualAcModeTemperatures[2] = preferences.getUChar("acTempDry", state.settings.manualAcTemperature);
    state.settings.manualAcModeTemperatures[3] = preferences.getUChar("acTempCool", state.settings.manualAcTemperature);
    state.settings.manualAcModeTemperatures[4] = preferences.getUChar("acTempHeat", state.settings.manualAcTemperature);
    state.settings.manualAcModeTemperatures[5] = preferences.getUChar("acTempAuto", state.settings.manualAcTemperature);
    for (uint8_t i = 1; i <= 5; i++) {
        state.settings.manualAcModeTemperatures[i] = constrain(state.settings.manualAcModeTemperatures[i], (uint8_t)16, (uint8_t)30);
    }
    state.settings.manualAcTemperature = state.settings.manualAcModeTemperatures[state.settings.manualAcMode];
    state.settings.manualAcFanMode = preferences.getUChar("acFan", state.settings.manualAcFanMode);
    if (state.settings.manualAcFanMode > 4) {
        state.settings.manualAcFanMode = 0;
    }
    state.settings.manualVfdPower = preferences.getBool("vfdPower", state.settings.manualVfdPower);
    state.settings.manualVfdStep = preferences.getUChar("vfdStep", state.settings.manualVfdStep);
    if (state.settings.manualVfdStep > 6) {
        state.settings.manualVfdStep = 0;
    }
    preferences.end();

    state.controllerState.mode = state.settings.mode;
    state.environment.targetIndoorTempC = state.settings.targetIndoorTempC;

    Logger::infof(
        TAG_SETTINGS,
        "Loaded settings: mode=%u setTemp=%.1f acPower=%u acMode=%u acTemp=%u acFan=%u vfdPower=%u vfdStep=%u",
        static_cast<unsigned int>(state.settings.mode),
        state.settings.targetIndoorTempC,
        state.settings.manualAcPower ? 1 : 0,
        state.settings.manualAcMode,
        state.settings.manualAcTemperature,
        state.settings.manualAcFanMode,
        state.settings.manualVfdPower ? 1 : 0,
        state.settings.manualVfdStep
    );
}


void App::scheduleUserSettingsSave() {
    if (!state.settings.autoSaveEnabled) {
        Logger::trace(TAG_SETTINGS, "User settings autosave skipped because it is disabled");
        return;
    }

    settingsDirty = true;
    lastSettingsChangeMs = millis();
    Logger::trace(TAG_SETTINGS, "User settings save scheduled");
}


void App::updateDeferredSettingsSave() {
    if (!settingsDirty) {
        return;
    }

    if (millis() - lastSettingsChangeMs < AppConfig::USER_SETTINGS_SAVE_DELAY_MS) {
        return;
    }

    settingsDirty = false;
    saveUserSettings();
}


void App::saveUserSettings() {
    if (!preferences.begin(AppConfig::USER_SETTINGS_NAMESPACE, false)) {
        Logger::warning(TAG_SETTINGS, "Failed to open NVS for writing");
        return;
    }

    preferences.putUChar("mode", static_cast<uint8_t>(state.settings.mode));
    preferences.putBool("wifiEnabled", state.settings.wifiEnabled);
    preferences.putBool("mqttEnabled", state.settings.mqttEnabled);
    preferences.putBool("autoSave", state.settings.autoSaveEnabled);
    preferences.putBool("vfdPolling", state.settings.vfdPollingEnabled);
    preferences.putFloat("setTemp", state.settings.targetIndoorTempC);
    preferences.putBool("acPower", state.settings.manualAcPower);
    preferences.putUChar("acMode", state.settings.manualAcMode);
    preferences.putUChar("acTemp", state.settings.manualAcTemperature);
    preferences.putUChar("acTempFan", state.settings.manualAcModeTemperatures[1]);
    preferences.putUChar("acTempDry", state.settings.manualAcModeTemperatures[2]);
    preferences.putUChar("acTempCool", state.settings.manualAcModeTemperatures[3]);
    preferences.putUChar("acTempHeat", state.settings.manualAcModeTemperatures[4]);
    preferences.putUChar("acTempAuto", state.settings.manualAcModeTemperatures[5]);
    preferences.putUChar("acFan", state.settings.manualAcFanMode);
    preferences.putBool("vfdPower", state.settings.manualVfdPower);
    preferences.putUChar("vfdStep", state.settings.manualVfdStep);
    preferences.end();

    Logger::info(TAG_SETTINGS, "User settings saved to NVS");
}


void App::updateModeTransition() {
    const DeviceMode currentMode = state.controllerState.mode;

    if (!modeTransitionInitialized) {
        modeTransitionInitialized = true;
        lastControllerMode = currentMode;

        if (currentMode == DeviceMode::Manual) {
            applyManualSettingsProfile("startup manual mode");
        }
        return;
    }

    if (currentMode == lastControllerMode) {
        return;
    }

    const DeviceMode previousMode = lastControllerMode;

    Logger::infof(
        TAG_SETTINGS,
        "Device mode changed: %u -> %u",
        static_cast<unsigned int>(previousMode),
        static_cast<unsigned int>(currentMode)
    );

    lastControllerMode = currentMode;

    const bool autoRecoveredFromSafe = previousMode == DeviceMode::Safe && currentMode == DeviceMode::Auto;
    if (!autoRecoveredFromSafe && (currentMode == DeviceMode::Auto || currentMode == DeviceMode::Manual || currentMode == DeviceMode::Disabled)) {
        state.settings.mode = currentMode;
        scheduleUserSettingsSave();
    }

    if (currentMode == DeviceMode::Auto) {
        state.controllerState.activity = ControllerActivity::Normal;
    } else if (currentMode == DeviceMode::Manual) {
        state.controllerState.activity = ControllerActivity::Hold;
        applyManualSettingsProfile("entered manual mode");
    } else if (currentMode == DeviceMode::Disabled) {
        state.controllerState.activity = ControllerActivity::Idle;
    }
}


void App::applyManualSettingsProfile(const char* reason) {
    Logger::infof(
        TAG_SETTINGS,
        "Applying manual profile: %s acPower=%u acMode=%u acTemp=%u acFan=%u vfdPower=%u vfdStep=%u",
        reason,
        state.settings.manualAcPower ? 1 : 0,
        state.settings.manualAcMode,
        state.settings.manualAcTemperature,
        state.settings.manualAcFanMode,
        state.settings.manualVfdPower ? 1 : 0,
        state.settings.manualVfdStep
    );

    if (state.settings.manualAcPower) {
        controller.setAcPower(true);
        controller.setAcMode(state.settings.manualAcMode);
        controller.setAcTemperature(state.settings.manualAcTemperature);
        controller.setAcFanMode(state.settings.manualAcFanMode);
    } else {
        controller.setAcPower(false);
    }

    requestVfdCommandSync(reason);
}
