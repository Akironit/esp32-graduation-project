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
}



void App::begin() {
    loadUserSettings();

    controller.begin(&hp, &vfd, &display, &tempSensors);
    climateAlgorithm.begin(&state, &controller);
    console.begin(&hp, &vfd, &tempSensors, &display, &state, &controller, &climateAlgorithm);
    display.begin();

    network.begin();

    if (network.isConnected()) {
        console.startTelnet();
    }

    homeAssistant.begin(
        state.settings.mqttEnabled,
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
    updateHeatPump();
    updateIoExpanderInputs();

    network.update();
    updateHeatPump();
    updateIoExpanderInputs();

    if (network.isConnected()) {
        console.startTelnet();
    }
    updateHeatPump();
    updateIoExpanderInputs();

    homeAssistant.update(network.isConnected());
    updateHeatPump();
    updateIoExpanderInputs();

    console.update();
    updateHeatPump();

    if (state.settings.mqttEnabled != lastMqttEnabled) {
        lastMqttEnabled = state.settings.mqttEnabled;
        homeAssistant.setEnabled(state.settings.mqttEnabled);
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
    updateHeatPump();
    updateIoExpanderInputs();

    updateDeviceState();
    updateModeTransition();
    climateAlgorithm.update();
    updateDeviceState();
    display.update(state);
    updateDeferredSettingsSave();
    updateHeatPump();
    updateIoExpanderInputs();

    updateDeviceState();
}


void App::updateHeatPump() {
    hp.waitForFrame();
    hp.sendPendingFrame();
}


void App::updateVfdStatus() {
    if (vfd.isBusy()) {
        return;
    }

    if (vfdCommandSyncActive) {
        return;
    }

    const unsigned long now = millis();
    if (now - lastVfdStatusPollMs < AppConfig::VFD_STATUS_POLL_INTERVAL_MS) {
        return;
    }

    if (now - lastVfdCommandSyncMs < AppConfig::VFD_POLL_AFTER_COMMAND_GUARD_MS) {
        return;
    }

    lastVfdStatusPollMs = now;
    vfd.pollStatus();
}


bool App::updateVfdCommandSync() {
    if (!vfdCommandSyncActive || state.controllerState.mode != DeviceMode::Manual) {
        return false;
    }

    if (vfd.isBusy()) {
        return false;
    }

    if (isVfdDesiredStateReached()) {
        Logger::tracef(
            TAG_VFD_UI,
            "VFD desired state reached after %u sync attempts",
            vfdCommandSyncAttempts
        );
        vfdCommandSyncActive = false;
        return false;
    }

    const unsigned long now = millis();
    if (now - lastVfdCommandSyncMs < AppConfig::VFD_COMMAND_SYNC_INTERVAL_MS) {
        return false;
    }

    if (vfdCommandSyncAttempts >= AppConfig::VFD_COMMAND_SYNC_MAX_ATTEMPTS) {
        Logger::warningf(
            TAG_VFD_UI,
            "VFD sync stopped after %u attempts desiredPower=%u desiredStep=%u actualRunning=%u actualStep=%u actualHz=%.1f",
            vfdCommandSyncAttempts,
            state.settings.manualVfdPower ? 1 : 0,
            state.settings.manualVfdStep,
            vfd.isRunning() ? 1 : 0,
            vfd.getActualStep(),
            vfd.hasActualFrequency() ? vfd.getActualFrequencyHz() : -1.0f
        );
        vfdCommandSyncActive = false;
        return false;
    }

    lastVfdCommandSyncMs = now;
    vfdCommandSyncAttempts++;

    Logger::tracef(
        TAG_VFD_UI,
        "VFD sync attempt=%u desiredPower=%u desiredStep=%u actualRunning=%u actualStep=%u actualHz=%.1f",
        vfdCommandSyncAttempts,
        state.settings.manualVfdPower ? 1 : 0,
        state.settings.manualVfdStep,
        vfd.isRunning() ? 1 : 0,
        vfd.getActualStep(),
        vfd.hasActualFrequency() ? vfd.getActualFrequencyHz() : -1.0f
    );

    if (!state.settings.manualVfdPower) {
        controller.vfdStop("auto", vfdCommandSyncActive);
        return true;
    }

    if (!vfd.isRunning()) {
        controller.vfdForward("auto", vfdCommandSyncActive);
        return true;
    }

    if (state.settings.manualVfdStep == 0) {
        vfdCommandSyncActive = false;
        return false;
    }

    const float desiredHz = vfdStepToHz(state.settings.manualVfdStep);
    const bool frequencyMatches = vfd.hasActualFrequency()
        && fabsf(vfd.getActualFrequencyHz() - desiredHz) <= 0.75f;

    if (!frequencyMatches) {
        controller.vfdSetFrequency(desiredHz, "auto", vfdCommandSyncActive);
        return true;
    }

    controller.vfdForward("auto", vfdCommandSyncActive);
    return true;
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
    state.ac.mode = hp.getMode();
    state.ac.fanMode = hp.getFanMode();
    state.ac.primaryController = hp.isPrimaryController();
    state.ac.controllerAddress = hp.getControllerAddress();
    state.ac.seenPrimaryController = hp.hasSeenPrimaryController();
    state.ac.seenSecondaryController = hp.hasSeenSecondaryController();
    state.ac.updatePending = hp.updatePending();
    state.ac.framePending = hp.hasPendingFrame();
    state.ac.debugEnabled = hp.debugPrint;
    state.ac.hasReceivedFrame = hp.hasReceivedFrame();
    state.ac.lastFrameAgeMs = hp.hasReceivedFrame() ? hp.getLastFrameAgeMs() : 0;
    state.ac.updateFields = hp.getUpdateFields();

    state.temperatures.sensorCount = tempSensors.getSensorCount();
    for (uint8_t i = 0; i < TEMP_MAX_SENSORS; i++) {
        state.temperatures.values[i] = tempSensors.getTemperatureC(i);
    }
    state.environment.hasIndoorTemp = tempSensors.getTemperatureByRole(TempSensorRole::Indoor, state.environment.indoorTempC);
    state.environment.hasOutdoorTemp = tempSensors.getTemperatureByRole(TempSensorRole::Outdoor, state.environment.outdoorTempC);

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
    state.vfd.lastToken = vfd.getLastToken();
    state.vfd.lastErrorCode = vfd.getLastErrorCode();
    state.vfd.hasActivity = vfd.hasActivity();
    state.vfd.lastActivityAgeMs = vfd.getLastActivityAgeMs();

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

    logVfdStateChanges();
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
    const bool pollButtons = buttonsActive && now - lastButtonPollMs >= AppConfig::BUTTON_POLL_INTERVAL_MS;

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

    if (exhaustVentState != lastExhaustVentState) {
        lastExhaustVentState = exhaustVentState;
        handleIoExpanderInputChange(MCP_PIN_EXHAUST_VENT, exhaustVentState);
    }

    updateVentilationInputs(gpa5State, gpa6State, gpa7State, exhaustVentState);

    const unsigned long now = millis();
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
    Logger::infof(
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

    if (event == ButtonInput::Event::LongPress) {
        Logger::debugf(TAG_INPUT, "Button %s long press", name);
        display.handleButton(button, true, state);
        return;
    }

    Logger::debugf(TAG_INPUT, "Button %s short press", name);
    const float previousTargetTemp = state.environment.targetIndoorTempC;
    const DisplayUi::Action action = display.handleButton(button, false, state);

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
            if (!vfd.isBusy()) {
                controller.vfdStop("display", vfdCommandSyncActive);
            }
            requestVfdCommandSync("display stop confirm");
            break;
        case DisplayUi::ActionType::VfdForward:
            if (!vfd.isBusy()) {
                controller.vfdForward("display", vfdCommandSyncActive);
            }
            requestVfdCommandSync("display forward confirm");
            break;
        case DisplayUi::ActionType::VfdSetFrequency:
            if (!vfd.isBusy()) {
                controller.vfdSetFrequency(vfdStepToHz(state.settings.manualVfdStep), "display", vfdCommandSyncActive);
            }
            requestVfdCommandSync("display frequency confirm");
            break;
        case DisplayUi::ActionType::None:
            break;
    }
}


void App::requestVfdCommandSync(const char* reason) {
    vfdCommandSyncActive = true;
    vfdCommandSyncAttempts = 0;
    lastVfdCommandSyncMs = 0;
    Logger::tracef(
        TAG_VFD_UI,
        "VFD sync requested: %s desiredPower=%u desiredStep=%u",
        reason,
        state.settings.manualVfdPower ? 1 : 0,
        state.settings.manualVfdStep
    );
}


bool App::isVfdDesiredStateReached() const {
    if (!state.settings.manualVfdPower || state.settings.manualVfdStep == 0) {
        if (!state.settings.manualVfdPower) {
            return !vfd.isRunning() && (!vfd.hasActualFrequency() || vfd.getActualFrequencyHz() < 1.0f);
        }

        return vfd.isRunning();
    }

    const float desiredHz = vfdStepToHz(state.settings.manualVfdStep);
    return vfd.isRunning()
        && vfd.hasActualFrequency()
        && fabsf(vfd.getActualFrequencyHz() - desiredHz) <= 0.75f;
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
    state.settings.mqttEnabled = preferences.getBool("mqttEnabled", MQTT_ENABLED);
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
    preferences.putBool("mqttEnabled", state.settings.mqttEnabled);
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
