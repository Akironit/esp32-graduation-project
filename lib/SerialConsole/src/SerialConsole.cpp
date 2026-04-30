// SerialConsole.cpp
#include "SerialConsole.h"

#include "Logger.h"

namespace {
constexpr const char* TAG_TELNET = "TELNET";
}


SerialConsole::ConsoleOutput::ConsoleOutput(SerialConsole& console)
    : console(console) {
}


size_t SerialConsole::ConsoleOutput::write(uint8_t value) {
    Serial.write(value);

    if (console.telnetClient && console.telnetClient.connected()) {
        console.telnetClient.write(value);
    }

    return 1;
}


size_t SerialConsole::ConsoleOutput::write(const uint8_t* buffer, size_t size) {
    Serial.write(buffer, size);

    if (console.telnetClient && console.telnetClient.connected()) {
        console.telnetClient.write(buffer, size);
    }

    return size;
}


void SerialConsole::begin(FujiHeatPump* hp, VfdController* vfd, TemperatureSensors* temp) {
    this->hp = hp;
    this->vfd = vfd;
    this->temp = temp;

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

            println();
            println("[TELNET] Client connected");
            println("-=-=-=-=- ESP32 Telnet Console -=-=-=-=-");
            println("Type 'help' for available commands");
            println();
            printPrompt();
        }

        return;
    }

    while (telnetClient.available()) {
        char c = (char)telnetClient.read();
        handleInputChar(c, telnetBuffer, true, telnetLastInputWasCarriageReturn);
    }
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


void SerialConsole::processCommand(const String& cmd) {
    if (cmd == "help") {
        printHelp();
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

    println("Unknown command. Use: ac <cmd>, vfd <cmd>, temp <cmd>");
    println("Type 'help' for available commands");
}


void SerialConsole::processAcCommand(const String& cmd) {
    if (hp == nullptr) {
        println("Error: heat pump module is not connected to console");
        return;
    }

    if (cmd == "debug on") {
        hp->setDebug(true);
        println("=== AC debug ENABLED ===");
        return;
    }

    if (cmd == "debug off") {
        hp->setDebug(false);
        println("=== AC debug DISABLED ===");
        return;
    }

    if (cmd == "on") {
        hp->setOnOff(true);
        println(">>> AC: Power ON");
        return;
    }

    if (cmd == "off") {
        hp->setOnOff(false);
        println(">>> AC: Power OFF");
        return;
    }

    if (cmd.startsWith("temp ")) {
        int temp = cmd.substring(5).toInt();

        if (temp >= 16 && temp <= 30) {
            hp->setTemp(temp);
            print(">>> AC: Set temperature ");
            println(temp);
        } else {
            println("Error: temp must be 16-30");
        }

        return;
    }

    if (cmd.startsWith("mode ")) {
        String modeStr = cmd.substring(5);
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

        hp->setMode(modeVal);
        print(">>> AC: Set mode ");
        println(modeStr);
        return;
    }

    if (cmd.startsWith("fan ")) {
        byte speed = (byte)cmd.substring(4).toInt();

        if (speed <= 4) {
            hp->setFanMode(speed);
            print(">>> AC: Set fan mode ");
            println(speed);
        } else {
            println("Error: fan mode: 0-4");
        }

        return;
    }

    if (cmd == "status") {
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
        vfd->forward();
        println(">>> VFD: Forward");
        return;
    }

    if (cmd == "rev") {
        vfd->reverse();
        println(">>> VFD: Reverse");
        return;
    }

    if (cmd == "stop") {
        vfd->stop();
        println(">>> VFD: Stop");
        return;
    }

    if (cmd.startsWith("hz ")) {
        float hz = cmd.substring(3).toFloat();

        if (hz < 0.0f) {
            println("Error: frequency must be >= 0");
            return;
        }

        vfd->setFrequency(hz);

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

        vfd->readRegister(address, count);
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

        vfd->writeRegister(address, value);
        println(">>> VFD: Write request queued");
        return;
    }

    println("Unknown VFD command. Type 'vfd help'");
}


void SerialConsole::printAcStatus() {
    if (hp == nullptr) {
        println("Error: heat pump module is not connected to console");
        return;
    }

    println();
    println("--- AC STATUS ---");
    print("Power: ");
    println(hp->getOnOff() ? "ON" : "OFF");
    print("Temp: ");
    println(hp->getTemp());
    print("Mode: ");
    println(hp->getMode());
    print("Fan: ");
    println(hp->getFanMode());
    print("Bound: ");
    println(hp->isBound() ? "YES" : "NO");
    println("-----------------");
    println();
}


void SerialConsole::printHelp() {
    println();
    println("--- Available command groups ---");
    println("ac <command>   - Air conditioner control");
    println("vfd <command>  - Frequency drive control");
    println("temp <command> - Temperature sensors control");
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
    println();
    println("Type 'ac help', 'vfd help' or 'temp help'");
    println("------------------------------");
    println();
}


void SerialConsole::printAcHelp() {
    println();
    println("--- AC commands ---");
    println("ac debug on/off");
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


void SerialConsole::processTempCommand(const String& cmd) {
    String args = cmd;
    args.trim();

    if (temp == nullptr) {
        println("[TEMP] TemperatureSensors module is not connected to console");
        return;
    }

    if (args.length() == 0 || args == "status") {
        temp->printStatus(consoleOutput);
        return;
    }

    if (args == "read") {
        temp->forceRead();
        temp->printStatus(consoleOutput);
        return;
    }

    if (args == "scan") {
        temp->rescan();
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
