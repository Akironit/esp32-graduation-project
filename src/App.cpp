// App.cpp
#include "App.h"

#include "Logger.h"

namespace {
constexpr const char* TAG_INPUT = "INPUT";
constexpr unsigned long BUTTON_POLL_INTERVAL_MS = 10;

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
    controller.begin(&hp, &vfd, &display, &tempSensors);
    console.begin(&hp, &vfd, &tempSensors, &display, &state, &controller);
    display.begin();

    network.begin();

    if (network.isConnected()) {
        console.startTelnet();
    }

    homeAssistant.begin(
        MQTT_ENABLED,
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

    updateIoExpanderInputs();
    updateHeatPump();

    tempSensors.update();
    updateHeatPump();
    updateIoExpanderInputs();

    updateDeviceState();
    display.update(state);
    updateHeatPump();
    updateIoExpanderInputs();

    updateDeviceState();
}


void App::updateHeatPump() {
    hp.waitForFrame();
    hp.sendPendingFrame();
}


void App::updateDeviceState() {
    state.uptimeMs = millis();
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

    state.vfd.initialized = vfd.isInitialized();
    state.vfd.lastAction = vfd.getLastAction();
    state.vfd.hasRequestedFrequency = vfd.hasRequestedFrequency();
    state.vfd.requestedFrequencyHz = vfd.getRequestedFrequencyHz();
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
}


void App::configureIoExpanderInputs() {
    ioExpanderReady = ioExpander.begin(Wire, MCP23017_ADDRESS, I2C_SDA_PIN, I2C_SCL_PIN);

    if (!ioExpanderReady) {
        return;
    }

    ioExpander.pinMode(MCP_PIN_GPA5, INPUT_PULLUP);
    ioExpander.pinMode(MCP_PIN_GPA6, INPUT_PULLUP);
    ioExpander.pinMode(MCP_PIN_GPA7, INPUT_PULLUP);
    ioExpander.pinMode(MCP_BUTTON_BACK_PIN, INPUT_PULLUP);
    ioExpander.pinMode(MCP_BUTTON_LEFT_PIN, INPUT_PULLUP);
    ioExpander.pinMode(MCP_BUTTON_RIGHT_PIN, INPUT_PULLUP);
    ioExpander.pinMode(MCP_BUTTON_OK_PIN, INPUT_PULLUP);
    ioExpander.configureInterruptOutputs(false);
    ioExpander.enableInterruptOnChange(MCP_PIN_GPA5);
    ioExpander.enableInterruptOnChange(MCP_PIN_GPA6);
    ioExpander.enableInterruptOnChange(MCP_PIN_GPA7);
    ioExpander.enableInterruptOnChange(MCP_BUTTON_BACK_PIN);
    ioExpander.enableInterruptOnChange(MCP_BUTTON_LEFT_PIN);
    ioExpander.enableInterruptOnChange(MCP_BUTTON_RIGHT_PIN);
    ioExpander.enableInterruptOnChange(MCP_BUTTON_OK_PIN);
    ioExpander.clearInterrupts();

    lastGpa5State = ioExpander.digitalRead(MCP_PIN_GPA5);
    lastGpa6State = ioExpander.digitalRead(MCP_PIN_GPA6);
    lastGpa7State = ioExpander.digitalRead(MCP_PIN_GPA7);
    buttonBack.begin(true, 50, 500);
    buttonLeft.begin(true, 50, 500);
    buttonRight.begin(true, 50, 500);
    buttonOk.begin(true, 50, 500);

    pinMode(MCP_INT_A_PIN, INPUT);
    pinMode(MCP_INT_B_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(MCP_INT_A_PIN), handleMcpInterruptA, FALLING);
    attachInterrupt(digitalPinToInterrupt(MCP_INT_B_PIN), handleMcpInterruptB, FALLING);

    Logger::info(TAG_INPUT, "GPA0-GPA3 buttons and GPA5/GPA6/GPA7 interrupt monitor started");
}


void App::updateIoExpanderInputs() {
    if (!ioExpanderReady) {
        return;
    }

    const unsigned long now = millis();
    const bool pollButtons = buttonsActive && now - lastButtonPollMs >= BUTTON_POLL_INTERVAL_MS;

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

    if (event == ButtonInput::Event::LongPress) {
        Logger::debugf(TAG_INPUT, "Button %s long press", name);

        if (strcmp(name, "BACK") == 0) {
            controller.displaySetPage(DisplayUi::Page::Overview);
        } else if (strcmp(name, "OK") == 0) {
            Logger::debug(TAG_INPUT, "OK long press action is reserved for future menu selection");
        }

        return;
    }

    Logger::debugf(TAG_INPUT, "Button %s short press", name);

    if (strcmp(name, "LEFT") == 0) {
        controller.displayPreviousPage();
    } else if (strcmp(name, "RIGHT") == 0) {
        controller.displayNextPage();
    } else if (strcmp(name, "BACK") == 0) {
        controller.displaySetPage(DisplayUi::Page::Overview);
    } else if (strcmp(name, "OK") == 0) {
        Logger::debug(TAG_INPUT, "OK button action is reserved for future menu selection");
    }
}
