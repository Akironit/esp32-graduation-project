// App.cpp
#include "App.h"

#include "Logger.h"

namespace {
constexpr const char* TAG_INPUT = "INPUT";

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
    console.begin(&hp, &vfd, &tempSensors, &display);
    display.begin();

    network.begin();

    if (network.isConnected()) {
        console.startTelnet();
    }

    hp.connect(&Serial1, IS_SECONDARY_CONTROLLER, AC_LIN_RX1_PIN, AC_LIN_TX1_PIN);
    hp.setDebug(AC_DEBUG);

    vfd.begin(Serial2, RS485_RX2_PIN, RS485_TX2_PIN, RS485_BAUD, SERIAL_8E1);

    tempSensors.begin(TEMP_ONE_WIRE_PIN);

    configureIoExpanderInputs();
}


void App::update() {
    updateHeatPump();

    network.update();
    updateHeatPump();

    if (network.isConnected()) {
        console.startTelnet();
    }
    updateHeatPump();

    console.update();
    updateHeatPump();

    updateIoExpanderInputs();
    updateHeatPump();

    tempSensors.update();
    updateHeatPump();

    display.update(network.isConnected(), network.getIp(), hp, tempSensors);
    updateHeatPump();
}


void App::updateHeatPump() {
    hp.waitForFrame();
    hp.sendPendingFrame();
}


void App::configureIoExpanderInputs() {
    ioExpanderReady = ioExpander.begin(Wire, MCP23017_ADDRESS, I2C_SDA_PIN, I2C_SCL_PIN);

    if (!ioExpanderReady) {
        return;
    }

    ioExpander.pinMode(MCP_PIN_GPA5, INPUT_PULLUP);
    ioExpander.pinMode(MCP_PIN_GPA6, INPUT_PULLUP);
    ioExpander.pinMode(MCP_PIN_GPA7, INPUT_PULLUP);
    ioExpander.configureInterruptOutputs(false);
    ioExpander.enableInterruptOnChange(MCP_PIN_GPA5);
    ioExpander.enableInterruptOnChange(MCP_PIN_GPA6);
    ioExpander.enableInterruptOnChange(MCP_PIN_GPA7);
    ioExpander.clearInterrupts();

    lastGpa5State = ioExpander.digitalRead(MCP_PIN_GPA5);
    lastGpa6State = ioExpander.digitalRead(MCP_PIN_GPA6);
    lastGpa7State = ioExpander.digitalRead(MCP_PIN_GPA7);

    pinMode(MCP_INT_A_PIN, INPUT);
    pinMode(MCP_INT_B_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(MCP_INT_A_PIN), handleMcpInterruptA, FALLING);
    attachInterrupt(digitalPinToInterrupt(MCP_INT_B_PIN), handleMcpInterruptB, FALLING);

    Logger::info(TAG_INPUT, "GPA5/GPA6/GPA7 interrupt monitor started");
}


void App::updateIoExpanderInputs() {
    if (!ioExpanderReady) {
        return;
    }

    if (!mcpInterruptA && !mcpInterruptB) {
        return;
    }

    noInterrupts();
    const bool interruptA = mcpInterruptA;
    const bool interruptB = mcpInterruptB;
    mcpInterruptA = false;
    mcpInterruptB = false;
    interrupts();

    if (interruptA) {
        processIoExpanderPort();
    }

    if (interruptB) {
        Logger::info(TAG_INPUT, "MCP23017 INTB triggered");
        ioExpander.clearInterrupts();
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
}


void App::handleIoExpanderInputChange(uint8_t pin, int currentState) {
    Logger::infof(
        TAG_INPUT,
        "GPA%u changed to %s",
        pin,
        currentState == LOW ? "LOW (grounded)" : "HIGH (pull-up)"
    );
}
