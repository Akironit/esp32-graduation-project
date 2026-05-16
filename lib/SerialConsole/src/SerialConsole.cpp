// SerialConsole.cpp
#include "SerialConsole.h"

#include "ClimateAlgorithm.h"
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
    DeviceController* controller,
    ClimateAlgorithm* climateAlgorithm
) {
    this->hp = hp;
    this->vfd = vfd;
    this->temp = temp;
    this->display = display;
    this->state = state;
    this->controller = controller;
    this->climateAlgorithm = climateAlgorithm;

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

#if ENABLE_STATE_DEBUG_COMMANDS
    if (cmd == "debug help" || cmd == "dbg help") {
        printDebugHelp();
        return;
    }

    if (cmd == "debug" || cmd == "debug status" || cmd == "dbg" || cmd == "dbg status") {
        printDebugStatus();
        return;
    }

    if (cmd.startsWith("debug ")) {
        processDebugCommand(cmd.substring(6));
        return;
    }

    if (cmd.startsWith("dbg ")) {
        processDebugCommand(cmd.substring(4));
        return;
    }
#endif

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

    if (cmd == "auto help") {
        printAutoHelp();
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

    if (cmd.startsWith("auto ")) {
        processAutoCommand(cmd.substring(5));
        return;
    }

    if (cmd.startsWith("display ")) {
        processDisplayCommand(cmd.substring(8));
        return;
    }

    println("Unknown command. Use: ac <cmd>, vfd <cmd>, temp <cmd>, auto <cmd>, display <cmd>, state <cmd>");
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
        if (controller == nullptr || !controller->vfdForward("console")) {
            println("Error: VFD controller action layer is not available");
            return;
        }

        println(">>> VFD: Forward");
        return;
    }

    if (cmd == "rev") {
        if (controller == nullptr || !controller->vfdReverse("console")) {
            println("Error: VFD controller action layer is not available");
            return;
        }

        println(">>> VFD: Reverse");
        return;
    }

    if (cmd == "stop") {
        if (controller == nullptr || !controller->vfdStop("console")) {
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

        if (controller == nullptr || !controller->vfdSetFrequency(hz, "console")) {
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
    } else if (args == "ac") {
        if (controller == nullptr || !controller->displaySetPage(DisplayUi::Page::AirConditioner)) {
            println("[DISPLAY] Controller action layer is not available");
            return;
        }
    } else if (args == "vent" || args == "ventilation") {
        if (controller == nullptr || !controller->displaySetPage(DisplayUi::Page::Ventilation)) {
            println("[DISPLAY] Controller action layer is not available");
            return;
        }
    } else if (args == "temp" || args == "temperatures") {
        if (controller == nullptr || !controller->displaySetPage(DisplayUi::Page::Temperatures)) {
            println("[DISPLAY] Controller action layer is not available");
            return;
        }
    } else if (args == "settings" || args == "net" || args == "network") {
        if (controller == nullptr || !controller->displaySetPage(DisplayUi::Page::Settings)) {
            println("[DISPLAY] Controller action layer is not available");
            return;
        }
    } else if (args == "diag" || args == "diagnostics") {
        if (controller == nullptr || !controller->displaySetPage(DisplayUi::Page::Diagnostics)) {
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

#if ENABLE_STATE_DEBUG_COMMANDS
void SerialConsole::processDebugCommand(const String& cmd) {
    String args = cmd;
    args.trim();

    if (state == nullptr) {
        println("[DEBUG] DeviceState is not connected to console");
        return;
    }

    if (args == "help") {
        printDebugHelp();
        return;
    }

    if (args == "status") {
        printDebugStatus();
        return;
    }

    if (args.startsWith("m ")) {
        args = "mode " + args.substring(2);
    } else if (args.startsWith("a ")) {
        args = "activity " + args.substring(2);
    } else if (args.startsWith("h ")) {
        args = "hood " + args.substring(2);
    } else if (args.startsWith("e ")) {
        args = "exhaust " + args.substring(2);
    } else if (args.startsWith("w ")) {
        args = "warn " + args.substring(2);
    } else if (args.startsWith("err ")) {
        args = "error " + args.substring(4);
    }

    if (args.startsWith("mode ")) {
        DeviceMode mode = DeviceMode::Auto;

        if (!parseDeviceMode(args.substring(5), mode)) {
            println("[DEBUG] Unknown mode. Use: auto/manual/safe/off");
            return;
        }

        state->controllerState.mode = mode;
        print("[DEBUG] Device mode set to ");
        println(deviceModeName(mode));
        return;
    }

    if (args.startsWith("activity ")) {
        ControllerActivity activity = ControllerActivity::Normal;

        if (!parseControllerActivity(args.substring(9), activity)) {
            println("[DEBUG] Unknown activity. Use: start/normal/ventcool/accool/heat/vent/error/hold/idle");
            return;
        }

        state->controllerState.activity = activity;
        print("[DEBUG] Controller activity set to ");
        println(controllerActivityName(activity));
        return;
    }

    if (args.startsWith("hood ")) {
        const int level = args.substring(5).toInt();

        if (level < 0 || level > 3) {
            println("[DEBUG] Hood level must be 0-3");
            return;
        }

        state->environment.kitchenHoodLevel = (uint8_t)level;
        print("[DEBUG] Kitchen hood level set to ");
        println(level);
        return;
    }

    if (args.startsWith("exhaust ")) {
        const String value = args.substring(8);

        if (value == "on" || value == "1") {
            state->environment.exhaustVentEnabled = true;
        } else if (value == "off" || value == "0") {
            state->environment.exhaustVentEnabled = false;
        } else {
            println("[DEBUG] Exhaust value must be on/off");
            return;
        }

        print("[DEBUG] Exhaust ventilation set to ");
        println(state->environment.exhaustVentEnabled ? "ON" : "OFF");
        return;
    }

    if (args.startsWith("room ")) {
        const float value = args.substring(5).toFloat();

        if (value < 5.0f || value > 40.0f) {
            println("[DEBUG] Room target must be 5-40 C");
            return;
        }

        state->environment.targetIndoorTempC = value;
        print("[DEBUG] Room target set to ");
        print(value, 1);
        println(" C");
        return;
    }

    if (args.startsWith("delta ")) {
        const float value = args.substring(6).toFloat();

        if (value < 0.0f || value > 10.0f) {
            println("[DEBUG] Delta must be 0-10 C");
            return;
        }

        state->environment.targetToleranceC = value;
        print("[DEBUG] Delta set to +/-");
        print(value, 1);
        println(" C");
        return;
    }

    if (args.startsWith("warn ")) {
        const int count = args.substring(5).toInt();

        if (count < 0 || count > 99) {
            println("[DEBUG] Warning count must be 0-99");
            return;
        }

        state->controllerState.warningCount = (uint8_t)count;
        print("[DEBUG] Warning count set to ");
        println(count);
        return;
    }

    if (args.startsWith("error ")) {
        const int count = args.substring(6).toInt();

        if (count < 0 || count > 99) {
            println("[DEBUG] Error count must be 0-99");
            return;
        }

        state->controllerState.errorCount = (uint8_t)count;
        print("[DEBUG] Error count set to ");
        println(count);
        return;
    }

    println("[DEBUG] Unknown debug command. Use: debug help");
}
#endif


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
    print(state->uptimeText);
    print(" (");
    print(String(state->uptimeMs));
    println(" ms)");
    print("Wi-Fi: ");
    println(state->wifiConnected ? "CONNECTED" : "DISCONNECTED");
    print("IP: ");
    println(state->ip.toString());
    println();

    println("[CONTROLLER]");
    print("Mode: ");
    println(deviceModeName(state->controllerState.mode));
    print("Activity: ");
    println(controllerActivityName(state->controllerState.activity));
    print("Warnings: ");
    println((int)state->controllerState.warningCount);
    print("Errors: ");
    println((int)state->controllerState.errorCount);
    println();

    println("[ENVIRONMENT]");
    print("Indoor temp: ");
    if (state->environment.hasIndoorTemp) {
        print(state->environment.indoorTempC, 2);
        println(" C");
    } else {
        println("not available");
    }
    print("Outdoor temp: ");
    if (state->environment.hasOutdoorTemp) {
        print(state->environment.outdoorTempC, 2);
        println(" C");
    } else {
        println("not available");
    }
    print("Kitchen hood: ");
    println((int)state->environment.kitchenHoodLevel);
    print("Exhaust vent: ");
    println(state->environment.exhaustVentEnabled ? "ON" : "OFF");
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
    println("auto <command> - Automatic climate algorithm");
    println("display <cmd>  - LCD display pages");
    println("state <cmd>    - Device state snapshot");
#if ENABLE_STATE_DEBUG_COMMANDS
    println("debug <cmd>    - temporary UI state simulator");
#endif
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
    println("auto status");
    println("auto dry on");
    println("display next");
    println("state status");
#if ENABLE_STATE_DEBUG_COMMANDS
    println("debug mode manual");
    println("debug activity accool");
#endif
    println("log level debug");
    println("log history");
    println("reboot");
    println();
    println("Type 'ac help', 'vfd help', 'temp help', 'auto help', 'display help', 'state help', 'debug help' or 'log help'");
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
    println("display ac");
    println("display ventilation");
    println("display temp");
    println("display settings");
    println("display diagnostics");
    println("------------------------");
    println();
}


void SerialConsole::printAutoHelp() {
    println();
    println("--- AUTO commands ---");
    println("auto status");
    println("auto config");
    println("auto enabled on/off");
    println("auto dry on/off");
    println("auto verbose on/off");
    println("auto target <temp>");
    println("auto cooling-delta <value>");
    println("auto heating-delta <value>");
    println("auto outdoor-min-delta <value>");
    println("auto outdoor-max-temp <value>");
    println("auto vent-always on/off");
    println("auto vent-default-step <0-6>");
    println("auto vent-min-step <0-6>");
    println("auto vent-cooling-step <0-6>");
    println("auto vent-max-step <0-6>");
    println("auto bath-comp-step <0-6>");
    println("auto hood-comp-1 <0-6>");
    println("auto hood-comp-2 <0-6>");
    println("auto hood-comp-3 <0-6>");
    println("auto vent-additive on/off");
    println("auto cold-limit <temp>");
    println("auto cold-max-step <0-6>");
    println("auto interval <ms>");
    println("auto hold <ms>");
    println("auto vent-check <ms>");
    println("auto vent-min-drop <value>");
    println("auto vent-step-up on/off");
    println("auto vent-fallback-ac on/off");
    println("auto ac-cool on/off");
    println("auto ac-heat on/off");
    println("auto vent-cool on/off");
    println("auto ac-fan-with-vent on/off");
    println("auto ac-fan-in-auto on/off");
    println("auto ac-fan-mode <value>");
    println("auto ac-fan-min <0-4>");
    println("auto ac-fan-normal <0-4>");
    println("auto ac-fan-boost <0-4>");
    println("auto ac-fan-max <0-4>");
    println("auto ac-fan-auto on/off");
    println("auto ac-dynamic on/off");
    println("auto ac-cool-full-delta <value>");
    println("auto ac-cool-min-offset <value>");
    println("auto ac-cool-max-offset <value>");
    println("auto ac-cool-min-temp <temp>");
    println("auto ac-cool-fan-min <0-4>");
    println("auto ac-cool-fan-max <0-4>");
    println("auto ac-heat-full-delta <value>");
    println("auto ac-heat-min-offset <value>");
    println("auto ac-heat-max-offset <value>");
    println("auto ac-heat-max-temp <temp>");
    println("auto ac-heat-fan-min <0-4>");
    println("auto ac-heat-fan-max <0-4>");
    println("auto safe-no-indoor on/off");
    println("auto safe-equipment on/off");
    println("auto vent-comp-interval <ms>");
    println("auto vent-comp-off-delay <ms>");
    println("auto vent-comp-up on/off");
    println("auto vent-comp-down on/off");
    println("auto save                         - force save now");
    println("auto load                         - reload from NVS");
    println("auto defaults                     - reset defaults and autosave");
    println("---------------------");
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

#if ENABLE_STATE_DEBUG_COMMANDS
void SerialConsole::printDebugHelp() {
    println();
    println("--- DEBUG commands ---");
    println("debug status");
    println("debug mode auto/manual/safe/off    | dbg m a/man/s/off");
    println("debug activity start/normal/ventcool/accool/heat/vent/error/hold/idle");
    println("Short activity aliases: dbg a st/n/vc/ac/h/v/e/hold/i");
    println("debug hood 0-3                    | dbg h 2");
    println("debug exhaust on/off              | dbg e on");
    println("debug room <tempC>");
    println("debug delta <tempC>");
    println("debug warn 0-99                   | dbg w 1");
    println("debug error 0-99                  | dbg err 0");
    println("----------------------");
    println();
}

void SerialConsole::printDebugStatus() {
    if (state == nullptr) {
        println("[DEBUG] DeviceState is not connected to console");
        return;
    }

    println();
    println("--- DEBUG UI STATE ---");
    print("Mode: ");
    println(deviceModeName(state->controllerState.mode));
    print("Activity: ");
    println(controllerActivityName(state->controllerState.activity));
    print("Kitchen hood: ");
    println((int)state->environment.kitchenHoodLevel);
    print("Exhaust vent: ");
    println(state->environment.exhaustVentEnabled ? "ON" : "OFF");
    print("Room target: ");
    print(state->environment.targetIndoorTempC, 1);
    println(" C");
    print("Target delta: +/-");
    print(state->environment.targetToleranceC, 1);
    println(" C");
    print("Warnings: ");
    println((int)state->controllerState.warningCount);
    print("Errors: ");
    println((int)state->controllerState.errorCount);
    println("----------------------");
    println();
}
#endif


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

#if ENABLE_STATE_DEBUG_COMMANDS
bool SerialConsole::parseDeviceMode(const String& value, DeviceMode& mode) {
    if (value == "auto" || value == "a") {
        mode = DeviceMode::Auto;
    } else if (value == "manual" || value == "man" || value == "m") {
        mode = DeviceMode::Manual;
    } else if (value == "safe" || value == "s") {
        mode = DeviceMode::Safe;
    } else if (value == "off" || value == "disabled" || value == "o") {
        mode = DeviceMode::Disabled;
    } else {
        return false;
    }

    return true;
}

bool SerialConsole::parseControllerActivity(const String& value, ControllerActivity& activity) {
    if (value == "start" || value == "st") {
        activity = ControllerActivity::Start;
    } else if (value == "normal" || value == "n") {
        activity = ControllerActivity::Normal;
    } else if (value == "ventcool" || value == "vent_cool" || value == "vent-cool" || value == "vc") {
        activity = ControllerActivity::VentCool;
    } else if (value == "accool" || value == "ac_cool" || value == "ac-cool" || value == "ac") {
        activity = ControllerActivity::AcCool;
    } else if (value == "heat" || value == "h") {
        activity = ControllerActivity::Heat;
    } else if (value == "vent" || value == "v") {
        activity = ControllerActivity::Vent;
    } else if (value == "error" || value == "err" || value == "e") {
        activity = ControllerActivity::Error;
    } else if (value == "hold") {
        activity = ControllerActivity::Hold;
    } else if (value == "idle" || value == "i") {
        activity = ControllerActivity::Idle;
    } else {
        return false;
    }

    return true;
}
#endif

const char* SerialConsole::deviceModeName(DeviceMode mode) const {
    switch (mode) {
        case DeviceMode::Auto:
            return "AUTO";
        case DeviceMode::Manual:
            return "MANUAL";
        case DeviceMode::Safe:
            return "SAFE";
        case DeviceMode::Disabled:
            return "OFF";
    }

    return "?";
}

const char* SerialConsole::controllerActivityName(ControllerActivity activity) const {
    switch (activity) {
        case ControllerActivity::Start:
            return "start";
        case ControllerActivity::Normal:
            return "normal";
        case ControllerActivity::VentCool:
            return "vent cool";
        case ControllerActivity::AcCool:
            return "AC cool";
        case ControllerActivity::Heat:
            return "heat";
        case ControllerActivity::Vent:
            return "vent";
        case ControllerActivity::Error:
            return "error";
        case ControllerActivity::Hold:
            return "standby";
        case ControllerActivity::Idle:
            return "monitor";
    }

    return "?";
}


void SerialConsole::processTempCommand(const String& cmd) {
    String args = cmd;
    args.trim();

    if (temp == nullptr) {
        println("[TEMP] TemperatureSensors module is not connected to console");
        return;
    }

    if (args.length() == 0 || args == "status" || args == "list") {
        temp->printStatus(consoleOutput);
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

    if (args.startsWith("assign ")) {
        const int roleSeparator = args.lastIndexOf(' ');
        if (roleSeparator <= 7) {
            println("[TEMP] Format: temp assign <index|address> indoor|outdoor|unknown|unused");
            return;
        }

        String selector = args.substring(7, roleSeparator);
        String roleText = args.substring(roleSeparator + 1);
        selector.trim();
        roleText.trim();
        roleText.toLowerCase();

        TempSensorRole role = TempSensorRole::Unknown;
        if (roleText == "indoor" || roleText == "in") {
            role = TempSensorRole::Indoor;
        } else if (roleText == "outdoor" || roleText == "out") {
            role = TempSensorRole::Outdoor;
        } else if (roleText == "unused" || roleText == "off") {
            role = TempSensorRole::Unused;
        } else if (roleText == "unknown" || roleText == "clear") {
            role = TempSensorRole::Unknown;
        } else {
            println("[TEMP] Role must be indoor, outdoor, unknown or unused");
            return;
        }

        const int index = temp->findEntryBySelector(selector);
        if (index < 0 || !temp->assignRole((uint8_t)index, role)) {
            println("[TEMP] Sensor not found");
            return;
        }

        println("[TEMP] Sensor role saved");
        temp->printStatus(consoleOutput);
        return;
    }

    if (args.startsWith("forget ")) {
        String selector = args.substring(7);
        selector.trim();
        const int index = temp->findEntryBySelector(selector);
        if (index < 0 || !temp->forget((uint8_t)index)) {
            println("[TEMP] Sensor not found");
            return;
        }

        println("[TEMP] Sensor forgotten");
        temp->printStatus(consoleOutput);
        return;
    }

    if (args == "swap") {
        if (!temp->swapRoles()) {
            println("[TEMP] Swap failed: Indoor and Outdoor roles must both be assigned");
            return;
        }

        println("[TEMP] Indoor and Outdoor roles swapped");
        temp->printStatus(consoleOutput);
        return;
    }

    if (args == "help") {
        println();
        println("[TEMP] Commands:");
        println("  temp list                         - show known DS18B20 sensors");
        println("  temp status                       - same as temp list");
        println("  temp read                         - force temperature reading and show list");
        println("  temp scan                         - rescan OneWire bus and show addresses");
        println("  temp assign <index|address> indoor|outdoor|unknown|unused");
        println("  temp forget <index|address>       - remove sensor from known registry");
        println("  temp swap                         - swap Indoor and Outdoor roles");
        println("  temp help                         - show this help");
        return;
    }

    println("[TEMP] Unknown temp command. Use: temp help");
}


void SerialConsole::processAutoCommand(const String& cmd) {
    String args = cmd;
    args.trim();

    if (climateAlgorithm == nullptr) {
        println("[AUTO] ClimateAlgorithm module is not connected to console");
        return;
    }

    AutoControlSettings settings = climateAlgorithm->getSettings();

    auto parseOnOff = [](const String& value, bool& result) {
        if (value == "on" || value == "1" || value == "true") {
            result = true;
            return true;
        }
        if (value == "off" || value == "0" || value == "false") {
            result = false;
            return true;
        }
        return false;
    };

    auto parseStep = [](const String& value, uint8_t& result) {
        const int parsed = value.toInt();
        if (parsed < 0 || parsed > 6) {
            return false;
        }
        result = (uint8_t)parsed;
        return true;
    };

    auto parseAcFan = [](const String& value, uint8_t& result) {
        const int parsed = value.toInt();
        if (parsed < 0 || parsed > 4) {
            return false;
        }
        result = (uint8_t)parsed;
        return true;
    };

    if (args.length() == 0 || args == "status") {
        const AutoControlStatus status = climateAlgorithm->getStatus();
        println();
        println("[AUTO] Status");
        print("mode=");
        println(state != nullptr ? deviceModeName(state->controllerState.mode) : "?");
        print("activity=");
        println(climateAlgorithm->activityName(status.activity));
        print("dryRun=");
        println(settings.dryRun ? 1 : 0);
        print("diagnosticVerbose=");
        println(settings.diagnosticVerbose ? 1 : 0);
        println("[INPUTS]");
        print("indoorTempC=");
        if (!status.indoorTempValid) println("N/A"); else println(status.indoorTempC, 1);
        print("indoorTempValid=");
        println(status.indoorTempValid ? 1 : 0);
        print("outdoorTempC=");
        if (!status.outdoorTempValid) println("N/A"); else println(status.outdoorTempC, 1);
        print("outdoorTempValid=");
        println(status.outdoorTempValid ? 1 : 0);
        print("bathExhaustOn=");
        println(status.bathExhaustOn ? 1 : 0);
        print("hoodLevel=");
        println((int)status.hoodLevel);
        print("vfdOnline=");
        println(status.vfdOnline ? 1 : 0);
        print("acAvailable=");
        println(status.acAvailable ? 1 : 0);
        println("[CALC]");
        print("targetTempC=");
        println(settings.targetTempC, 1);
        print("deltaTempC=");
        println(status.deltaTempC, 1);
        print("needCooling=");
        println(status.needCooling ? 1 : 0);
        print("needHeating=");
        println(status.needHeating ? 1 : 0);
        print("ventCoolingAllowedNow=");
        println(status.ventCoolingAllowedNow ? 1 : 0);
        print("acCoolingAllowedNow=");
        println(status.acCoolingAllowedNow ? 1 : 0);
        print("acHeatingAllowedNow=");
        println(status.acHeatingAllowedNow ? 1 : 0);
        print("baseVentRequirementStep=");
        println((int)status.baseVentRequirementStep);
        print("bathCompStep=");
        println((int)status.bathCompStep);
        print("hoodCompStep=");
        println((int)status.hoodCompStep);
        print("exhaustCompRequirementStep=");
        println((int)status.exhaustCompRequirementStep);
        print("coolingVentRequirementStep=");
        println((int)status.coolingVentRequirementStep);
        print("requestedVentStepBeforeLimit=");
        println((int)status.requestedVentStepBeforeLimit);
        print("requestedVentStepAfterLimit=");
        println((int)status.requestedVentStepAfterLimit);
        print("coldOutdoorLimitActive=");
        println(status.coldOutdoorLimitActive ? 1 : 0);
        println("[DESIRED]");
        print("desired.vfdPower=");
        println(status.desiredVfdPower ? 1 : 0);
        print("desired.vfdStep=");
        println((int)status.desiredVfdStep);
        print("desired.vfdHz=");
        println(status.desiredVfdHz, 1);
        print("desired.acPower=");
        println(status.desiredAcPower ? 1 : 0);
        print("desired.acMode=");
        println((int)status.desiredAcMode);
        print("desired.acFanSpeed=");
        println((int)status.desiredAcFanSpeed);
        print("desired.acTargetTemp=");
        println((int)status.desiredAcTargetTemp);
        print("acCoolingRatio=");
        println(status.acCoolingRatio, 2);
        print("acHeatingRatio=");
        println(status.acHeatingRatio, 2);
        println("[ACTUAL]");
        print("actualVfdRunning=");
        println(state != nullptr && state->vfd.running ? 1 : 0);
        print("actualVfdStep=");
        println(state != nullptr ? (int)state->vfd.actualStep : 0);
        print("actualVfdHz=");
        if (state != nullptr && state->vfd.hasActualFrequency) println(state->vfd.actualFrequencyHz, 1); else println("N/A");
        print("actualAcPower=");
        println(state != nullptr && state->ac.powerOn ? 1 : 0);
        print("actualAcMode=");
        println(state != nullptr ? (int)state->ac.mode : 0);
        print("actualAcFan=");
        println(state != nullptr ? (int)state->ac.fanMode : 0);
        print("actualAcTemp=");
        println(state != nullptr ? (int)state->ac.temperature : 0);
        print("actualAcBound=");
        println(state != nullptr && state->ac.bound ? 1 : 0);
        print("reason=");
        println(status.reason);
        print("lastApplyResult=");
        println(status.lastApplyResult);
        print("lastSkippedReason=");
        println(status.lastSkippedReason);
        print("lastCommandAge=");
        println(String(status.lastCommandMs == 0 ? 0 : millis() - status.lastCommandMs) + " ms");
        print("lastDecisionAge=");
        println(String(status.lastDecisionMs == 0 ? 0 : millis() - status.lastDecisionMs) + " ms");
        print("lastVentCompensationAge=");
        println(String(status.lastVentCompensationMs == 0 ? 0 : millis() - status.lastVentCompensationMs) + " ms");
        print("stateHoldAge=");
        println(String(status.stateEnteredMs == 0 ? 0 : millis() - status.stateEnteredMs) + " ms");
        printAutoConfig(settings);
        return;
    }

    if (args == "config" || args == "settings") {
        printAutoConfig(settings);
        return;
    }

    if (args == "save") {
        println(climateAlgorithm->saveSettings() ? "[AUTO] Settings force-saved" : "[AUTO] Save failed");
        return;
    }

    if (args == "load") {
        println(climateAlgorithm->loadSettings() ? "[AUTO] Settings reloaded from NVS" : "[AUTO] Load failed");
        return;
    }

    if (args == "defaults") {
        climateAlgorithm->resetToDefaults();
        println("[AUTO] Defaults restored. Autosave scheduled.");
        return;
    }

    if (args == "help") {
        printAutoHelp();
        return;
    }

    int separator = args.indexOf(' ');
    if (separator < 0) {
        println("[AUTO] Unknown auto command. Use: auto help");
        return;
    }

    String key = args.substring(0, separator);
    String value = args.substring(separator + 1);
    key.trim();
    value.trim();

    bool changed = true;
    if (key == "enabled") {
        changed = parseOnOff(value, settings.autoEnabled);
    } else if (key == "dry") {
        changed = parseOnOff(value, settings.dryRun);
    } else if (key == "verbose" || key == "log") {
        changed = parseOnOff(value, settings.diagnosticVerbose);
    } else if (key == "target") {
        settings.targetTempC = value.toFloat();
    } else if (key == "hyst") {
        println("[AUTO] auto hyst is deprecated. Use auto cooling-delta and auto heating-delta.");
        return;
    } else if (key == "cooling-delta") {
        settings.coolingStartDeltaC = value.toFloat();
    } else if (key == "heating-delta") {
        settings.heatingStartDeltaC = value.toFloat();
    } else if (key == "outdoor-min-delta" || key == "outdoor-margin") {
        settings.outdoorCoolingMinDeltaC = value.toFloat();
    } else if (key == "outdoor-max" || key == "outdoor-max-temp") {
        settings.outdoorCoolingMaxTempC = value.toFloat();
    } else if (key == "vent-always") {
        changed = parseOnOff(value, settings.autoVentAlwaysOn);
    } else if (key == "vent-default-step" || key == "normal-step") {
        changed = parseStep(value, settings.autoVentDefaultStep);
    } else if (key == "vent-min-step" || key == "min-step") {
        changed = parseStep(value, settings.autoVentMinStep);
    } else if (key == "vent-cooling-step" || key == "cool-step") {
        changed = parseStep(value, settings.autoVentCoolingStep);
    } else if (key == "vent-max-step" || key == "max-step") {
        changed = parseStep(value, settings.autoVentMaxStep);
    } else if (key == "bath-comp-step" || key == "exhaust-boost") {
        changed = parseStep(value, settings.bathExhaustCompStep);
    } else if (key == "hood-comp-1") {
        changed = parseStep(value, settings.hoodCompStep1);
    } else if (key == "hood-comp-2") {
        changed = parseStep(value, settings.hoodCompStep2);
    } else if (key == "hood-comp-3" || key == "hood-boost") {
        changed = parseStep(value, settings.hoodCompStep3);
    } else if (key == "vent-additive") {
        changed = parseOnOff(value, settings.additiveVentCompensation);
    } else if (key == "cold-limit") {
        settings.coldOutdoorTempLimitC = value.toFloat();
    } else if (key == "cold-max-step") {
        changed = parseStep(value, settings.coldOutdoorMaxVentStep);
    } else if (key == "interval") {
        settings.decisionIntervalMs = value.toInt();
    } else if (key == "hold") {
        settings.minStateHoldMs = value.toInt();
    } else if (key == "vent-check") {
        settings.ventCoolingCheckIntervalMs = value.toInt();
    } else if (key == "vent-min-drop") {
        settings.ventCoolingMinDropC = value.toFloat();
    } else if (key == "vent-step-up") {
        changed = parseOnOff(value, settings.ventCoolingStepUpOnFail);
    } else if (key == "vent-fallback-ac") {
        changed = parseOnOff(value, settings.ventCoolingFallbackToAc);
    } else if (key == "ac-cool") {
        changed = parseOnOff(value, settings.allowAcCooling);
    } else if (key == "ac-heat") {
        changed = parseOnOff(value, settings.allowAcHeating);
    } else if (key == "vent-cool") {
        changed = parseOnOff(value, settings.allowVentCooling);
    } else if (key == "ac-fan-with-vent") {
        changed = parseOnOff(value, settings.keepAcFanOnWithVent);
    } else if (key == "ac-fan-in-auto") {
        changed = parseOnOff(value, settings.keepAcFanOnInAuto);
    } else if (key == "ac-fan-mode") {
        const int parsed = value.toInt();
        if (parsed < 1 || parsed > 5) {
            changed = false;
        } else {
            settings.acFanOnlyMode = (uint8_t)parsed;
        }
    } else if (key == "ac-fan-min") {
        changed = parseAcFan(value, settings.acFanMinSpeed);
    } else if (key == "ac-fan-normal") {
        changed = parseAcFan(value, settings.acFanNormalSpeed);
    } else if (key == "ac-fan-boost") {
        changed = parseAcFan(value, settings.acFanBoostSpeed);
    } else if (key == "ac-fan-max") {
        changed = parseAcFan(value, settings.acFanMaxSpeed);
    } else if (key == "ac-fan-auto") {
        changed = parseOnOff(value, settings.acFanAutoAllowed);
    } else if (key == "ac-dynamic") {
        changed = parseOnOff(value, settings.acDynamicControlEnabled);
    } else if (key == "ac-cool-full-delta") {
        settings.acCoolingFullPowerDeltaC = value.toFloat();
    } else if (key == "ac-cool-min-offset") {
        settings.acCoolingMinTempOffsetC = value.toFloat();
    } else if (key == "ac-cool-max-offset") {
        settings.acCoolingMaxTempOffsetC = value.toFloat();
    } else if (key == "ac-cool-min-temp") {
        settings.acCoolingMinSetpointC = value.toFloat();
    } else if (key == "ac-cool-fan-min" || key == "ac-cool-fan") {
        changed = parseAcFan(value, settings.acCoolingMinFanSpeed);
        if (key == "ac-cool-fan") {
            println("[AUTO] auto ac-cool-fan is deprecated. Use auto ac-cool-fan-min and auto ac-cool-fan-max.");
        }
    } else if (key == "ac-cool-fan-max") {
        changed = parseAcFan(value, settings.acCoolingMaxFanSpeed);
    } else if (key == "ac-heat-full-delta") {
        settings.acHeatingFullPowerDeltaC = value.toFloat();
    } else if (key == "ac-heat-min-offset") {
        settings.acHeatingMinTempOffsetC = value.toFloat();
    } else if (key == "ac-heat-max-offset") {
        settings.acHeatingMaxTempOffsetC = value.toFloat();
    } else if (key == "ac-heat-max-temp") {
        settings.acHeatingMaxSetpointC = value.toFloat();
    } else if (key == "ac-heat-fan-min" || key == "ac-heat-fan") {
        changed = parseAcFan(value, settings.acHeatingMinFanSpeed);
        if (key == "ac-heat-fan") {
            println("[AUTO] auto ac-heat-fan is deprecated. Use auto ac-heat-fan-min and auto ac-heat-fan-max.");
        }
    } else if (key == "ac-heat-fan-max") {
        changed = parseAcFan(value, settings.acHeatingMaxFanSpeed);
    } else if (key == "safe-no-indoor") {
        changed = parseOnOff(value, settings.safeOnIndoorSensorMissing);
    } else if (key == "safe-equipment") {
        changed = parseOnOff(value, settings.safeOnCriticalEquipmentError);
    } else if (key == "vent-comp-interval") {
        settings.ventCompensationUpdateIntervalMs = value.toInt();
    } else if (key == "vent-comp-off-delay") {
        settings.ventCompensationOffDelayMs = value.toInt();
    } else if (key == "vent-comp-up") {
        changed = parseOnOff(value, settings.ventCompensationImmediateUp);
    } else if (key == "vent-comp-down") {
        changed = parseOnOff(value, settings.ventCompensationImmediateDown);
    } else {
        println("[AUTO] Unknown auto setting. Use: auto help");
        return;
    }

    if (!changed) {
        println("[AUTO] Invalid value");
        return;
    }

    climateAlgorithm->setSettings(settings);
    println("[AUTO] Setting updated. Autosave scheduled.");
}


void SerialConsole::printAutoConfig(const AutoControlSettings& settings) {
    println();
    println("[AUTO] Config");
    println(String("autoEnabled=") + (settings.autoEnabled ? 1 : 0) + "    command: auto enabled on/off");
    println(String("dryRun=") + (settings.dryRun ? 1 : 0) + "    command: auto dry on/off");
    println(String("diagnosticVerbose=") + (settings.diagnosticVerbose ? 1 : 0) + "    command: auto verbose on/off");
    println(String("targetTempC=") + String(settings.targetTempC, 1) + "    command: auto target <temp>");
    println(String("coolingStartDeltaC=") + String(settings.coolingStartDeltaC, 2) + "    command: auto cooling-delta <value>");
    println(String("heatingStartDeltaC=") + String(settings.heatingStartDeltaC, 2) + "    command: auto heating-delta <value>");
    println(String("allowVentCooling=") + (settings.allowVentCooling ? 1 : 0) + "    command: auto vent-cool on/off");
    println(String("allowAcCooling=") + (settings.allowAcCooling ? 1 : 0) + "    command: auto ac-cool on/off");
    println(String("allowAcHeating=") + (settings.allowAcHeating ? 1 : 0) + "    command: auto ac-heat on/off");
    println(String("outdoorCoolingMinDeltaC=") + String(settings.outdoorCoolingMinDeltaC, 2) + "    command: auto outdoor-min-delta <value>");
    println(String("outdoorCoolingMaxTempC=") + String(settings.outdoorCoolingMaxTempC, 1) + "    command: auto outdoor-max-temp <temp>");
    println(String("autoVentAlwaysOn=") + (settings.autoVentAlwaysOn ? 1 : 0) + "    command: auto vent-always on/off");
    println(String("autoVentDefaultStep=") + (int)settings.autoVentDefaultStep + "    command: auto vent-default-step <0-6>");
    println(String("autoVentMinStep=") + (int)settings.autoVentMinStep + "    command: auto vent-min-step <0-6>");
    println(String("autoVentCoolingStep=") + (int)settings.autoVentCoolingStep + "    command: auto vent-cooling-step <0-6>");
    println(String("autoVentMaxStep=") + (int)settings.autoVentMaxStep + "    command: auto vent-max-step <0-6>");
    println(String("bathExhaustCompStep=") + (int)settings.bathExhaustCompStep + "    command: auto bath-comp-step <0-6>");
    println(String("hoodCompStep1=") + (int)settings.hoodCompStep1 + "    command: auto hood-comp-1 <0-6>");
    println(String("hoodCompStep2=") + (int)settings.hoodCompStep2 + "    command: auto hood-comp-2 <0-6>");
    println(String("hoodCompStep3=") + (int)settings.hoodCompStep3 + "    command: auto hood-comp-3 <0-6>");
    println(String("additiveVentCompensation=") + (settings.additiveVentCompensation ? 1 : 0) + "    command: auto vent-additive on/off");
    println(String("coldOutdoorTempLimitC=") + String(settings.coldOutdoorTempLimitC, 1) + "    command: auto cold-limit <temp>");
    println(String("coldOutdoorMaxVentStep=") + (int)settings.coldOutdoorMaxVentStep + "    command: auto cold-max-step <0-6>");
    println(String("keepAcFanOnInAuto=") + (settings.keepAcFanOnInAuto ? 1 : 0) + "    command: auto ac-fan-in-auto on/off");
    println(String("keepAcFanOnWithVent=") + (settings.keepAcFanOnWithVent ? 1 : 0) + "    command: auto ac-fan-with-vent on/off");
    println(String("acFanOnlyMode=") + (int)settings.acFanOnlyMode + "    command: auto ac-fan-mode <value>");
    println(String("acFanMinSpeed=") + (int)settings.acFanMinSpeed + "    command: auto ac-fan-min <0-4>");
    println(String("acFanNormalSpeed=") + (int)settings.acFanNormalSpeed + "    command: auto ac-fan-normal <0-4>");
    println(String("acFanBoostSpeed=") + (int)settings.acFanBoostSpeed + "    command: auto ac-fan-boost <0-4>");
    println(String("acFanMaxSpeed=") + (int)settings.acFanMaxSpeed + "    command: auto ac-fan-max <0-4>");
    println(String("acFanAutoAllowed=") + (settings.acFanAutoAllowed ? 1 : 0) + "    command: auto ac-fan-auto on/off");
    println(String("acDynamicControlEnabled=") + (settings.acDynamicControlEnabled ? 1 : 0) + "    command: auto ac-dynamic on/off");
    println(String("acCoolingFullPowerDeltaC=") + String(settings.acCoolingFullPowerDeltaC, 2) + "    command: auto ac-cool-full-delta <value>");
    println(String("acCoolingMinTempOffsetC=") + String(settings.acCoolingMinTempOffsetC, 2) + "    command: auto ac-cool-min-offset <value>");
    println(String("acCoolingMaxTempOffsetC=") + String(settings.acCoolingMaxTempOffsetC, 2) + "    command: auto ac-cool-max-offset <value>");
    println(String("acCoolingMinSetpointC=") + String(settings.acCoolingMinSetpointC, 1) + "    command: auto ac-cool-min-temp <temp>");
    println(String("acCoolingMinFanSpeed=") + (int)settings.acCoolingMinFanSpeed + "    command: auto ac-cool-fan-min <0-4>");
    println(String("acCoolingMaxFanSpeed=") + (int)settings.acCoolingMaxFanSpeed + "    command: auto ac-cool-fan-max <0-4>");
    println(String("acHeatingFullPowerDeltaC=") + String(settings.acHeatingFullPowerDeltaC, 2) + "    command: auto ac-heat-full-delta <value>");
    println(String("acHeatingMinTempOffsetC=") + String(settings.acHeatingMinTempOffsetC, 2) + "    command: auto ac-heat-min-offset <value>");
    println(String("acHeatingMaxTempOffsetC=") + String(settings.acHeatingMaxTempOffsetC, 2) + "    command: auto ac-heat-max-offset <value>");
    println(String("acHeatingMaxSetpointC=") + String(settings.acHeatingMaxSetpointC, 1) + "    command: auto ac-heat-max-temp <temp>");
    println(String("acHeatingMinFanSpeed=") + (int)settings.acHeatingMinFanSpeed + "    command: auto ac-heat-fan-min <0-4>");
    println(String("acHeatingMaxFanSpeed=") + (int)settings.acHeatingMaxFanSpeed + "    command: auto ac-heat-fan-max <0-4>");
    println(String("decisionIntervalMs=") + String(settings.decisionIntervalMs) + "    command: auto interval <ms>");
    println(String("minStateHoldMs=") + String(settings.minStateHoldMs) + "    command: auto hold <ms>");
    println(String("ventCoolingCheckIntervalMs=") + String(settings.ventCoolingCheckIntervalMs) + "    command: auto vent-check <ms>");
    println(String("ventCoolingMinDropC=") + String(settings.ventCoolingMinDropC, 2) + "    command: auto vent-min-drop <value>");
    println(String("ventCoolingStepUpOnFail=") + (settings.ventCoolingStepUpOnFail ? 1 : 0) + "    command: auto vent-step-up on/off");
    println(String("ventCoolingFallbackToAc=") + (settings.ventCoolingFallbackToAc ? 1 : 0) + "    command: auto vent-fallback-ac on/off");
    println(String("safeOnIndoorSensorMissing=") + (settings.safeOnIndoorSensorMissing ? 1 : 0) + "    command: auto safe-no-indoor on/off");
    println(String("safeOnCriticalEquipmentError=") + (settings.safeOnCriticalEquipmentError ? 1 : 0) + "    command: auto safe-equipment on/off");
    println(String("ventCompensationUpdateIntervalMs=") + String(settings.ventCompensationUpdateIntervalMs) + "    command: auto vent-comp-interval <ms>");
    println(String("ventCompensationOffDelayMs=") + String(settings.ventCompensationOffDelayMs) + "    command: auto vent-comp-off-delay <ms>");
    println(String("ventCompensationImmediateUp=") + (settings.ventCompensationImmediateUp ? 1 : 0) + "    command: auto vent-comp-up on/off");
    println(String("ventCompensationImmediateDown=") + (settings.ventCompensationImmediateDown ? 1 : 0) + "    command: auto vent-comp-down on/off");
}
