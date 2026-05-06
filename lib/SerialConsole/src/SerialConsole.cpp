// SerialConsole.cpp
#include "SerialConsole.h"

#include "Logger.h"

namespace {
constexpr const char* TAG_TELNET = "TELNET";

constexpr uint8_t TELNET_IAC = 255;
constexpr uint8_t TELNET_DO = 253;
constexpr uint8_t TELNET_WILL = 251;
constexpr uint8_t TELNET_ECHO = 1;
constexpr uint8_t TELNET_SUPPRESS_GO_AHEAD = 3;
}


SerialConsole::ConsoleOutput::ConsoleOutput(SerialConsole& console)
    : console(console) {
}


size_t SerialConsole::ConsoleOutput::write(uint8_t value) {
    Serial.write(value);

    if (console.telnetClient && console.telnetClient.connected()) {
        if (value == '\n' && !console.telnetLastOutputWasCarriageReturn) {
            console.telnetClient.write('\r');
        }

        console.telnetClient.write(value);
        console.telnetLastOutputWasCarriageReturn = (value == '\r');
    }

    return 1;
}


size_t SerialConsole::ConsoleOutput::write(const uint8_t* buffer, size_t size) {
    for (size_t i = 0; i < size; i++) {
        write(buffer[i]);
    }

    return size;
}


void SerialConsole::begin(
    FujiHeatPump* hp,
    VfdController* vfd,
    TemperatureSensors* temp,
    DisplayUi* display,
    DeviceState* state,
    DeviceController* controller
) {
    this->hp = hp;
    this->vfd = vfd;
    this->temp = temp;
    this->display = display;
    this->state = state;
    this->controller = controller;

    if (this->hp != nullptr) {
        this->hp->setDebugOutput(&Serial);
    }

    Serial.begin(115200);
    Logger::begin(consoleOutput);

    println();
    println("-=-=-=-=- ESP32: Start -=-=-=-=-");
    println("Type 'help' for available commands");
    println();
}

void SerialConsole::startTelnet() {
    if (telnetStarted) {
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Logger::warning(TAG_TELNET, "Wi-Fi is not connected, Telnet server not started");
        return;
    }

    telnetServer.begin();
    telnetServer.setNoDelay(true);
    telnetStarted = true;

    Logger::infof(TAG_TELNET, "Server started at %s:23", WiFi.localIP().toString().c_str());
}


void SerialConsole::update() {
    updateSerialInput();
    updateTelnet();
}


void SerialConsole::updateSerialInput() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        handleInputChar(c, serialBuffer, false, serialLastInputWasCarriageReturn);
    }
}


void SerialConsole::print(const String& value) {
    consoleOutput.print(value);
}

void SerialConsole::print(const char* value) {
    consoleOutput.print(value);
}

void SerialConsole::print(int value) {
    consoleOutput.print(value);
}

void SerialConsole::print(uint16_t value) {
    consoleOutput.print(value);
}

void SerialConsole::print(float value, int digits) {
    consoleOutput.print(value, digits);
}


void SerialConsole::println() {
    consoleOutput.println();
}

void SerialConsole::println(const String& value) {
    consoleOutput.println(value);
}

void SerialConsole::println(const char* value) {
    consoleOutput.println(value);
}

void SerialConsole::println(int value) {
    consoleOutput.println(value);
}

void SerialConsole::println(uint16_t value) {
    consoleOutput.println(value);
}

void SerialConsole::println(float value, int digits) {
    consoleOutput.println(value, digits);
}


void SerialConsole::updateTelnet() {
    if (!telnetClient || !telnetClient.connected()) {
        WiFiClient newClient = telnetServer.available();

        if (newClient) {
            if (telnetClient) {
                telnetClient.stop();
            }

            telnetClient = newClient;
            telnetClient.setNoDelay(true);
            telnetNegotiationBytesToSkip = 0;
            telnetLastOutputWasCarriageReturn = false;

            sendTelnetNegotiation();

            println();
            println("[TELNET] Client connected");
            println("-=-=-=-=- ESP32 Telnet Console -=-=-=-=-");
            println("Type 'help' for available commands");
            println();
            telnetClient.println("--- Recent log history ---");
            Logger::replay(telnetClient);
            telnetClient.println("--- End log history ---");
            telnetClient.println();
            printPrompt();
        }

        return;
    }

    while (telnetClient.available()) {
        uint8_t value = (uint8_t)telnetClient.read();

        if (isTelnetCommandByte(value)) {
            continue;
        }

        char c = (char)value;
        handleInputChar(c, telnetBuffer, true, telnetLastInputWasCarriageReturn);
    }
}


void SerialConsole::sendTelnetNegotiation() {
    const uint8_t negotiation[] = {
        TELNET_IAC, TELNET_WILL, TELNET_ECHO,
        TELNET_IAC, TELNET_WILL, TELNET_SUPPRESS_GO_AHEAD,
        TELNET_IAC, TELNET_DO, TELNET_SUPPRESS_GO_AHEAD
    };

    telnetClient.write(negotiation, sizeof(negotiation));
}


bool SerialConsole::isTelnetCommandByte(uint8_t value) {
    if (value == TELNET_IAC) {
        telnetNegotiationBytesToSkip = 2;
        return true;
    }

    if (telnetNegotiationBytesToSkip > 0) {
        telnetNegotiationBytesToSkip--;
        return true;
    }

    return false;
}


void SerialConsole::handleInputChar(
    char c,
    String& buffer,
    bool echo,
    bool& lastInputWasCarriageReturn
) {
    if (c == '\r') {
        handleInputLine(buffer, echo);
        lastInputWasCarriageReturn = true;
        return;
    }

    if (c == '\n') {
        if (lastInputWasCarriageReturn) {
            lastInputWasCarriageReturn = false;
            return;
        }

        handleInputLine(buffer, echo);
        return;
    }

    lastInputWasCarriageReturn = false;

    if (c == '\b' || c == 127) {
        if (buffer.length() > 0) {
            buffer.remove(buffer.length() - 1);

            if (echo) {
                print("\b \b");
            }
        }

        return;
    }

    if (buffer.length() < 128) {
        buffer += c;

        if (echo) {
            print(String(c));
        }
    }
}


void SerialConsole::handleInputLine(String& buffer, bool echo) {
    if (echo) {
        println();
    }

    buffer.trim();
    buffer.toLowerCase();

    if (buffer.length() > 0) {
        processCommand(buffer);
    }

    buffer = "";

    if (echo) {
        printPrompt();
    }
}


void SerialConsole::printPrompt() {
    print("esp32> ");
}


void SerialConsole::printLogHistory(Print& output) {
    output.println();
    output.println("--- Recent log history ---");
    Logger::replay(output);
    output.println("--- End log history ---");
    output.println();
}


void SerialConsole::processCommand(const String& cmd) {
    if (cmd == "help") {
        printHelp();
        return;
    }

    if (cmd == "log history") {
        printLogHistory(consoleOutput);
        return;
    }

    if (cmd == "log help") {
        printLogHelp();
        return;
    }

    if (cmd == "state" || cmd == "state status") {
        printStateStatus();
        return;
    }

    if (cmd == "state help") {
        printStateHelp();
        return;
    }

    if (cmd.startsWith("state ")) {
        processStateCommand(cmd.substring(6));
        return;
    }

    if (cmd.startsWith("log ")) {
        processLogCommand(cmd.substring(4));
        return;
    }

    if (cmd == "reboot" || cmd == "restart") {
        if (controller == nullptr) {
            println("Error: controller action layer is not available");
            return;
        }

        println("Rebooting ESP32...");
        controller->restart();
        return;
    }

    if (cmd == "ac help") {
        printAcHelp();
        return;
    }

    if (cmd == "vfd help") {
        printVfdHelp();
        return;
    }

    if (cmd == "display help") {
        printDisplayHelp();
        return;
    }

    if (cmd.startsWith("ac ")) {
        processAcCommand(cmd.substring(3));
        return;
    }

    if (cmd.startsWith("vfd ")) {
        processVfdCommand(cmd.substring(4));
        return;
    }

    if (cmd.startsWith("temp ")) {
        processTempCommand(cmd.substring(5));
        return;
    }

    if (cmd.startsWith("display ")) {
        processDisplayCommand(cmd.substring(8));
        return;
    }

    println("Unknown command. Use: ac <cmd>, vfd <cmd>, temp <cmd>, display <cmd>, state <cmd>");
    println("Type 'help' for available commands");
}


void SerialConsole::processAcCommand(const String& cmd) {
    String args = cmd;
    args.trim();

    if (hp == nullptr) {
        println("Error: heat pump module is not connected to console");
        return;
    }

    if (args == "debug on") {
        if (controller == nullptr || !controller->setAcDebug(true)) {
            println("Error: AC controller action layer is not available");
            return;
        }

        println("=== AC debug ENABLED ===");
        return;
    }

    if (args == "debug off") {
        if (controller == nullptr || !controller->setAcDebug(false)) {
            println("Error: AC controller action layer is not available");
            return;
        }

        println("=== AC debug DISABLED ===");
        return;
    }

    if (args == "role") {
        print("Current AC controller role: ");
        if (state != nullptr) {
            println(state->ac.primaryController ? "PRIMARY" : "SECONDARY");
        } else {
            println(hp->isPrimaryController() ? "PRIMARY" : "SECONDARY");
        }
        return;
    }

    if (args.startsWith("role ")) {
        String role = args.substring(5);

        if (role == "primary") {
            if (controller == nullptr || !controller->setAcPrimaryRole(true)) {
                println("Error: AC controller action layer is not available");
                return;
            }

            println(">>> AC: Controller role set to PRIMARY");
            println(">>> AC: Handshake state reset, waiting for new frames");
        } else if (role == "secondary") {
            if (controller == nullptr || !controller->setAcPrimaryRole(false)) {
                println("Error: AC controller action layer is not available");
                return;
            }

            println(">>> AC: Controller role set to SECONDARY");
            println(">>> AC: Handshake state reset, waiting for new frames");
        } else {
            println("Error: role must be primary or secondary");
        }

        return;
    }

    if (args == "on") {
        if (controller == nullptr || !controller->setAcPower(true)) {
            println("Error: AC controller action layer is not available");
            return;
        }

        println(">>> AC: Power ON");
        return;
    }

    if (args == "off") {
        if (controller == nullptr || !controller->setAcPower(false)) {
            println("Error: AC controller action layer is not available");
            return;
        }

        println(">>> AC: Power OFF");
        return;
    }

    if (args.startsWith("temp ")) {
        int temp = args.substring(5).toInt();

        if (temp >= 16 && temp <= 30) {
            if (controller == nullptr || !controller->setAcTemperature((uint8_t)temp)) {
                println("Error: AC controller action layer is not available");
                return;
            }

            print(">>> AC: Set temperature ");
            println(temp);
        } else {
            println("Error: temp must be 16-30");
        }

        return;
    }

    if (args.startsWith("mode ")) {
        String modeStr = args.substring(5);
        byte modeVal = 0;

        if (modeStr == "fan") {
            modeVal = 1;
        } else if (modeStr == "dry") {
            modeVal = 2;
        } else if (modeStr == "cool") {
            modeVal = 3;
        } else if (modeStr == "heat") {
            modeVal = 4;
        } else if (modeStr == "auto") {
            modeVal = 5;
        } else {
            println("Error: mode fan/dry/cool/heat/auto");
            return;
        }

        if (controller == nullptr || !controller->setAcMode(modeVal)) {
            println("Error: AC controller action layer is not available");
            return;
        }

        print(">>> AC: Set mode ");
        println(modeStr);
        return;
    }

    if (args.startsWith("fan ")) {
        byte speed = (byte)args.substring(4).toInt();

        if (speed <= 4) {
            if (controller == nullptr || !controller->setAcFanMode(speed)) {
                println("Error: AC controller action layer is not available");
                return;
            }

            print(">>> AC: Set fan mode ");
            println(speed);
        } else {
            println("Error: fan mode: 0-4");
        }

        return;
    }

    if (args == "status") {
        printAcStatus();
        return;
    }

    println("Unknown AC command. Type 'ac help'");
}


void SerialConsole::processVfdCommand(const String& cmd) {
    if (vfd == nullptr) {
        println("Error: VFD module is not connected to console");
        return;
    }

    if (cmd == "fwd") {
        if (controller == nullptr || !controller->vfdForward()) {
            println("Error: VFD controller action layer is not available");
            return;
        }

        println(">>> VFD: Forward");
        return;
    }

    if (cmd == "rev") {
        if (controller == nullptr || !controller->vfdReverse()) {
            println("Error: VFD controller action layer is not available");
            return;
        }

        println(">>> VFD: Reverse");
        return;
    }

    if (cmd == "stop") {
        if (controller == nullptr || !controller->vfdStop()) {
            println("Error: VFD controller action layer is not available");
            return;
        }

        println(">>> VFD: Stop");
        return;
    }

    if (cmd.startsWith("hz ")) {
        float hz = cmd.substring(3).toFloat();

        if (hz < 0.0f) {
            println("Error: frequency must be >= 0");
            return;
        }

        if (controller == nullptr || !controller->vfdSetFrequency(hz)) {
            println("Error: VFD controller action layer is not available");
            return;
        }

        print(">>> VFD: Set frequency ");
        print(hz, 2);
        println(" Hz");
        return;
    }

    if (cmd.startsWith("read ")) {
        int separator = cmd.indexOf(' ', 5);

        if (separator < 0) {
            println("Format: vfd read <hexAddr> <count>");
            return;
        }

        String addressStr = cmd.substring(5, separator);
        String countStr = cmd.substring(separator + 1);

        bool okAddress = false;
        uint16_t address = parseHexU16(addressStr, okAddress);
        uint16_t count = (uint16_t)countStr.toInt();

        if (!okAddress || count == 0) {
            println("Error: bad read arguments");
            return;
        }

        if (controller == nullptr || !controller->vfdReadRegister(address, count)) {
            println("Error: VFD controller action layer is not available");
            return;
        }

        println(">>> VFD: Read request queued");
        return;
    }

    if (cmd.startsWith("write ")) {
        int separator = cmd.indexOf(' ', 6);

        if (separator < 0) {
            println("Format: vfd write <hexAddr> <hexVal>");
            return;
        }

        String addressStr = cmd.substring(6, separator);
        String valueStr = cmd.substring(separator + 1);

        bool okAddress = false;
        bool okValue = false;

        uint16_t address = parseHexU16(addressStr, okAddress);
        uint16_t value = parseHexU16(valueStr, okValue);

        if (!okAddress || !okValue) {
            println("Error: bad write arguments");
            return;
        }

        if (controller == nullptr || !controller->vfdWriteRegister(address, value)) {
            println("Error: VFD controller action layer is not available");
            return;
        }

        println(">>> VFD: Write request queued");
        return;
    }

    println("Unknown VFD command. Type 'vfd help'");
}


void SerialConsole::processDisplayCommand(const String& cmd) {
    String args = cmd;
    args.trim();

    if (display == nullptr) {
        println("[DISPLAY] DisplayUi module is not connected to console");
        return;
    }

    if (args == "help") {
        printDisplayHelp();
        return;
    }

    if (args == "next") {
        if (controller == nullptr || !controller->displayNextPage()) {
            println("[DISPLAY] Controller action layer is not available");
            return;
        }

        print("[DISPLAY] Page: ");
        println(display->getPageName());
        return;
    }

    if (args == "prev" || args == "previous") {
        if (controller == nullptr || !controller->displayPreviousPage()) {
            println("[DISPLAY] Controller action layer is not available");
            return;
        }

        print("[DISPLAY] Page: ");
        println(display->getPageName());
        return;
    }

    if (args == "status") {
        printDisplayStateStatus();
        return;
    }

    if (args == "overview") {
        if (controller == nullptr || !controller->displaySetPage(DisplayUi::Page::Overview)) {
            println("[DISPLAY] Controller action layer is not available");
            return;
        }
    } else if (args == "temp" || args == "temperatures") {
        if (controller == nullptr || !controller->displaySetPage(DisplayUi::Page::Temperatures)) {
            println("[DISPLAY] Controller action layer is not available");
            return;
        }
    } else if (args == "ac") {
        if (controller == nullptr || !controller->displaySetPage(DisplayUi::Page::AirConditioner)) {
            println("[DISPLAY] Controller action layer is not available");
            return;
        }
    } else if (args == "net" || args == "network") {
        if (controller == nullptr || !controller->displaySetPage(DisplayUi::Page::Network)) {
            println("[DISPLAY] Controller action layer is not available");
            return;
        }
    } else {
        println("[DISPLAY] Unknown display command. Use: display help");
        return;
    }

    print("[DISPLAY] Page: ");
    println(display->getPageName());
}


void SerialConsole::processLogCommand(const String& cmd) {
    String args = cmd;
    args.trim();

    if (args == "level") {
        print("[LOG] Current level: ");
        println(Logger::levelName(Logger::getLevel()));
        return;
    }

    if (args.startsWith("level ")) {
        String levelName = args.substring(6);
        LogLevel level = LogLevel::Info;

        if (!parseLogLevel(levelName, level)) {
            println("[LOG] Unknown level. Use: error/warning/info/debug/trace");
            return;
        }

        Logger::setLevel(level);
        print("[LOG] Level set to ");
        println(Logger::levelName(level));
        return;
    }

    if (args == "history") {
        printLogHistory(consoleOutput);
        return;
    }

    if (args == "help") {
        printLogHelp();
        return;
    }

    println("[LOG] Unknown log command. Use: log help");
}


void SerialConsole::processStateCommand(const String& cmd) {
    String args = cmd;
    args.trim();

    if (args == "status") {
        printStateStatus();
        return;
    }

    if (args == "help") {
        printStateHelp();
        return;
    }

    println("[STATE] Unknown state command. Use: state help");
}


void SerialConsole::printStateStatus() {
    if (state == nullptr) {
        println("[STATE] DeviceState is not connected to console");
        return;
    }

    char updateMask[8];
    snprintf(updateMask, sizeof(updateMask), "0x%02X", state->ac.updateFields);

    char lastErrorCode[8];
    snprintf(lastErrorCode, sizeof(lastErrorCode), "0x%02X", state->vfd.lastErrorCode);

    println();
    println("--- DEVICE STATE ---");
    print("Uptime: ");
    print(String(state->uptimeMs));
    println(" ms");
    print("Wi-Fi: ");
    println(state->wifiConnected ? "CONNECTED" : "DISCONNECTED");
    print("IP: ");
    println(state->ip.toString());
    println();

    println("[AC]");
    print("Power: ");
    println(state->ac.powerOn ? "ON" : "OFF");
    print("Temp: ");
    println((int)state->ac.temperature);
    print("Mode: ");
    println((int)state->ac.mode);
    print("Fan: ");
    println((int)state->ac.fanMode);
    print("Bound: ");
    println(state->ac.bound ? "YES" : "NO");
    print("Last frame age: ");
    if (state->ac.hasReceivedFrame) {
        print(String(state->ac.lastFrameAgeMs));
        println(" ms");
    } else {
        println("never");
    }
    print("Role: ");
    println(state->ac.primaryController ? "PRIMARY" : "SECONDARY");
    print("Address: ");
    println((int)state->ac.controllerAddress);
    print("Seen primary: ");
    println(state->ac.seenPrimaryController ? "YES" : "NO");
    print("Seen secondary: ");
    println(state->ac.seenSecondaryController ? "YES" : "NO");
    print("Update pending: ");
    println(state->ac.updatePending ? "YES" : "NO");
    print("Frame pending: ");
    println(state->ac.framePending ? "YES" : "NO");
    print("Update mask: ");
    println(updateMask);
    print("Debug: ");
    println(state->ac.debugEnabled ? "YES" : "NO");
    println();

    println("[VFD]");
    print("Initialized: ");
    println(state->vfd.initialized ? "YES" : "NO");
    print("Last action: ");
    println(state->vfd.lastAction);
    print("Requested frequency: ");
    if (state->vfd.hasRequestedFrequency) {
        print(state->vfd.requestedFrequencyHz, 2);
        println(" Hz");
    } else {
        println("none");
    }
    print("Requests: ");
    println(String(state->vfd.requestCount));
    print("OK responses: ");
    println(String(state->vfd.okCount));
    print("Errors: ");
    println(String(state->vfd.errorCount));
    print("Last token: ");
    println(String(state->vfd.lastToken));
    print("Last error: ");
    println(lastErrorCode);
    print("Last activity age: ");
    if (state->vfd.hasActivity) {
        print(String(state->vfd.lastActivityAgeMs));
        println(" ms");
    } else {
        println("never");
    }
    println();

    println("[TEMP]");
    print("Sensors: ");
    println((int)state->temperatures.sensorCount);
    for (uint8_t i = 0; i < state->temperatures.sensorCount && i < TEMP_MAX_SENSORS; i++) {
        print("Sensor ");
        print((int)i);
        print(": ");
        print(state->temperatures.values[i], 2);
        println(" C");
    }
    println();

    println("[INPUT]");
    print("MCP23017: ");
    println(state->input.ioExpanderReady ? "READY" : "NOT READY");
    print("BACK: ");
    println(state->input.buttonBackPressed ? "PRESSED" : "released");
    print("LEFT: ");
    println(state->input.buttonLeftPressed ? "PRESSED" : "released");
    print("RIGHT: ");
    println(state->input.buttonRightPressed ? "PRESSED" : "released");
    print("OK: ");
    println(state->input.buttonOkPressed ? "PRESSED" : "released");
    println();

    println("[DISPLAY]");
    print("Ready: ");
    println(state->display.ready ? "YES" : "NO");
    print("Page index: ");
    println((int)state->display.pageIndex);
    print("Page name: ");
    println(state->display.pageName);
    println();

    println("[HOME ASSISTANT]");
    print("MQTT enabled: ");
    println(state->homeAssistant.enabled ? "YES" : "NO");
    print("MQTT connected: ");
    println(state->homeAssistant.connected ? "YES" : "NO");
    print("Reconnects: ");
    println(String(state->homeAssistant.reconnectCount));
    print("Publishes: ");
    println(String(state->homeAssistant.publishCount));
    print("Commands: ");
    println(String(state->homeAssistant.commandCount));
    print("Last publish age: ");
    if (state->homeAssistant.hasPublished) {
        print(String(state->homeAssistant.lastPublishAgeMs));
        println(" ms");
    } else {
        println("never");
    }
    println("--------------------");
    println();
}


void SerialConsole::printTemperatureStateStatus() {
    if (state == nullptr) {
        println("[TEMP] DeviceState is not connected to console");
        return;
    }

    println();
    println("[TEMP] Temperature sensors status");
    print("[TEMP] Sensors: ");
    println((int)state->temperatures.sensorCount);

    for (uint8_t i = 0; i < state->temperatures.sensorCount && i < TEMP_MAX_SENSORS; i++) {
        print("[TEMP] Sensor ");
        print((int)i);
        print(" = ");
        print(state->temperatures.values[i], 2);
        println(" C");
    }
}


void SerialConsole::printDisplayStateStatus() {
    if (state == nullptr) {
        println("[DISPLAY] DeviceState is not connected to console");
        return;
    }

    print("[DISPLAY] Ready: ");
    println(state->display.ready ? "YES" : "NO");
    print("[DISPLAY] Page: ");
    println(state->display.pageName);
}


void SerialConsole::printAcStatus() {
    if (state == nullptr) {
        println("Error: DeviceState is not connected to console");
        return;
    }

    char updateMask[8];
    snprintf(updateMask, sizeof(updateMask), "0x%02X", state->ac.updateFields);

    println();
    println("--- AC STATUS ---");
    print("Power: ");
    println(state->ac.powerOn ? "ON" : "OFF");
    print("Temp: ");
    println((int)state->ac.temperature);
    print("Mode: ");
    println((int)state->ac.mode);
    print("Fan: ");
    println((int)state->ac.fanMode);
    print("Bound: ");
    println(state->ac.bound ? "YES" : "NO");
    print("Last frame age: ");
    if (state->ac.hasReceivedFrame) {
        print(String(state->ac.lastFrameAgeMs));
        println(" ms");
    } else {
        println("never");
    }
    print("Controller role: ");
    println(state->ac.primaryController ? "PRIMARY" : "SECONDARY");
    print("Controller address: ");
    println((int)state->ac.controllerAddress);
    print("Seen primary: ");
    println(state->ac.seenPrimaryController ? "YES" : "NO");
    print("Seen secondary: ");
    println(state->ac.seenSecondaryController ? "YES" : "NO");
    print("Update pending: ");
    println(state->ac.updatePending ? "YES" : "NO");
    print("Frame pending: ");
    println(state->ac.framePending ? "YES" : "NO");
    print("Update mask: ");
    println(updateMask);
    print("Debug: ");
    println(state->ac.debugEnabled ? "YES" : "NO");
    println("-----------------");
    println();
}


void SerialConsole::printHelp() {
    println();
    println("--- Available command groups ---");
    println("ac <command>   - Air conditioner control");
    println("vfd <command>  - Frequency drive control");
    println("temp <command> - Temperature sensors control");
    println("display <cmd>  - LCD display pages");
    println("state <cmd>    - Device state snapshot");
    println();
    println("Examples:");
    println("ac on");
    println("ac temp 23");
    println("ac status");
    println("vfd fwd");
    println("vfd hz 10.0");
    println("vfd stop");
    println("temp status");
    println("temp read");
    println("display next");
    println("state status");
    println("log level debug");
    println("log history");
    println("reboot");
    println();
    println("Type 'ac help', 'vfd help', 'temp help', 'display help', 'state help' or 'log help'");
    println("Other commands: reboot/restart");
    println("------------------------------");
    println();
}


void SerialConsole::printAcHelp() {
    println();
    println("--- AC commands ---");
    println("ac debug on/off");
    println("ac role");
    println("ac role primary/secondary");
    println("ac on/off");
    println("ac temp 16-30");
    println("ac mode fan/dry/cool/heat/auto");
    println("ac fan 0-4");
    println("ac status");
    println("-------------------");
    println();
}


void SerialConsole::printVfdHelp() {
    println();
    println("--- VFD commands ---");
    println("vfd fwd");
    println("vfd rev");
    println("vfd stop");
    println("vfd hz <frequency>");
    println("vfd read <hexAddr> <count>");
    println("vfd write <hexAddr> <hexVal>");
    println();
    println("Examples:");
    println("vfd hz 10.0");
    println("vfd read 2100 1");
    println("vfd write 2000 0001");
    println("--------------------");
    println();
}


void SerialConsole::printDisplayHelp() {
    println();
    println("--- DISPLAY commands ---");
    println("display status");
    println("display next");
    println("display prev");
    println("display overview");
    println("display temp");
    println("display ac");
    println("display network");
    println("------------------------");
    println();
}


void SerialConsole::printLogHelp() {
    println();
    println("--- LOG commands ---");
    println("log level");
    println("log level error");
    println("log level warning");
    println("log level info");
    println("log level debug");
    println("log level trace");
    println("log history");
    println("--------------------");
    println();
}


void SerialConsole::printStateHelp() {
    println();
    println("--- STATE commands ---");
    println("state");
    println("state status");
    println("state help");
    println("----------------------");
    println();
}


uint16_t SerialConsole::parseHexU16(const String& value, bool& ok) {
    ok = false;

    char* end = nullptr;
    long parsed = strtol(value.c_str(), &end, 16);

    if (end == value.c_str() || *end != '\0') {
        return 0;
    }

    if (parsed < 0 || parsed > 0xFFFF) {
        return 0;
    }

    ok = true;
    return (uint16_t)parsed;
}


bool SerialConsole::parseLogLevel(const String& value, LogLevel& level) {
    if (value == "error") {
        level = LogLevel::Error;
    } else if (value == "warn" || value == "warning") {
        level = LogLevel::Warning;
    } else if (value == "info") {
        level = LogLevel::Info;
    } else if (value == "debug") {
        level = LogLevel::Debug;
    } else if (value == "trace") {
        level = LogLevel::Trace;
    } else {
        return false;
    }

    return true;
}


void SerialConsole::processTempCommand(const String& cmd) {
    String args = cmd;
    args.trim();

    if (temp == nullptr) {
        println("[TEMP] TemperatureSensors module is not connected to console");
        return;
    }

    if (args.length() == 0 || args == "status") {
        printTemperatureStateStatus();
        return;
    }

    if (args == "read") {
        if (controller == nullptr || !controller->temperatureForceRead()) {
            println("[TEMP] Controller action layer is not available");
            return;
        }

        temp->printStatus(consoleOutput);
        return;
    }

    if (args == "scan") {
        if (controller == nullptr || !controller->temperatureRescan()) {
            println("[TEMP] Controller action layer is not available");
            return;
        }

        temp->printAddresses(consoleOutput);
        return;
    }

    if (args == "help") {
        println();
        println("[TEMP] Commands:");
        println("  temp status   - show last temperature readings");
        println("  temp read     - force temperature reading and show result");
        println("  temp scan     - rescan OneWire bus and show addresses");
        println("  temp help     - show this help");
        return;
    }

    println("[TEMP] Unknown temp command. Use: temp help");
}
