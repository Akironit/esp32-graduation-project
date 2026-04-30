// SerialConsole.cpp
#include "SerialConsole.h"


void SerialConsole::begin(FujiHeatPump* heatPump, VfdController* vfdController) {
    hp = heatPump;
    vfd = vfdController;

    Serial.begin(115200);
    Serial.println();
    Serial.println("-=-=-=-=- ESP32: Start -=-=-=-=-");
    Serial.println("Type 'help' for available commands");
    Serial.println();
}


void SerialConsole::update() {
    if (!Serial.available()) {
        return;
    }

    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();

    if (cmd.length() == 0) {
        return;
    }

    processCommand(cmd);
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

    Serial.println("Unknown command. Use: ac <command> or vfd <command>");
    Serial.println("Type 'help' for available commands");
}


void SerialConsole::processAcCommand(const String& cmd) {
    if (hp == nullptr) {
        Serial.println("Error: heat pump module is not connected to console");
        return;
    }

    if (cmd == "debug on") {
        hp->setDebug(true);
        Serial.println("=== AC debug ENABLED ===");
        return;
    }

    if (cmd == "debug off") {
        hp->setDebug(false);
        Serial.println("=== AC debug DISABLED ===");
        return;
    }

    if (cmd == "on") {
        hp->setOnOff(true);
        Serial.println(">>> AC: Power ON");
        return;
    }

    if (cmd == "off") {
        hp->setOnOff(false);
        Serial.println(">>> AC: Power OFF");
        return;
    }

    if (cmd.startsWith("temp ")) {
        int temp = cmd.substring(5).toInt();

        if (temp >= 16 && temp <= 30) {
            hp->setTemp(temp);
            Serial.print(">>> AC: Set temperature ");
            Serial.println(temp);
        } else {
            Serial.println("Error: temp must be 16-30");
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
            Serial.println("Error: mode fan/dry/cool/heat/auto");
            return;
        }

        hp->setMode(modeVal);
        Serial.print(">>> AC: Set mode ");
        Serial.println(modeStr);
        return;
    }

    if (cmd.startsWith("fan ")) {
        byte speed = (byte)cmd.substring(4).toInt();

        if (speed <= 4) {
            hp->setFanMode(speed);
            Serial.print(">>> AC: Set fan mode ");
            Serial.println(speed);
        } else {
            Serial.println("Error: fan mode: 0-4");
        }

        return;
    }

    if (cmd == "status") {
        printAcStatus();
        return;
    }

    Serial.println("Unknown AC command. Type 'ac help'");
}


void SerialConsole::processVfdCommand(const String& cmd) {
    if (vfd == nullptr) {
        Serial.println("Error: VFD module is not connected to console");
        return;
    }

    if (cmd == "fwd") {
        vfd->forward();
        Serial.println(">>> VFD: Forward");
        return;
    }

    if (cmd == "rev") {
        vfd->reverse();
        Serial.println(">>> VFD: Reverse");
        return;
    }

    if (cmd == "stop") {
        vfd->stop();
        Serial.println(">>> VFD: Stop");
        return;
    }

    if (cmd.startsWith("hz ")) {
        float hz = cmd.substring(3).toFloat();

        if (hz < 0.0f) {
            Serial.println("Error: frequency must be >= 0");
            return;
        }

        vfd->setFrequency(hz);

        Serial.print(">>> VFD: Set frequency ");
        Serial.print(hz, 2);
        Serial.println(" Hz");
        return;
    }

    if (cmd.startsWith("read ")) {
        int separator = cmd.indexOf(' ', 5);

        if (separator < 0) {
            Serial.println("Format: vfd read <hexAddr> <count>");
            return;
        }

        String addressStr = cmd.substring(5, separator);
        String countStr = cmd.substring(separator + 1);

        bool okAddress = false;
        uint16_t address = parseHexU16(addressStr, okAddress);
        uint16_t count = (uint16_t)countStr.toInt();

        if (!okAddress || count == 0) {
            Serial.println("Error: bad read arguments");
            return;
        }

        vfd->readRegister(address, count);
        Serial.println(">>> VFD: Read request queued");
        return;
    }

    if (cmd.startsWith("write ")) {
        int separator = cmd.indexOf(' ', 6);

        if (separator < 0) {
            Serial.println("Format: vfd write <hexAddr> <hexVal>");
            return;
        }

        String addressStr = cmd.substring(6, separator);
        String valueStr = cmd.substring(separator + 1);

        bool okAddress = false;
        bool okValue = false;

        uint16_t address = parseHexU16(addressStr, okAddress);
        uint16_t value = parseHexU16(valueStr, okValue);

        if (!okAddress || !okValue) {
            Serial.println("Error: bad write arguments");
            return;
        }

        vfd->writeRegister(address, value);
        Serial.println(">>> VFD: Write request queued");
        return;
    }

    Serial.println("Unknown VFD command. Type 'vfd help'");
}


void SerialConsole::printAcStatus() {
    if (hp == nullptr) {
        Serial.println("Error: heat pump module is not connected to console");
        return;
    }

    Serial.println();
    Serial.println("--- AC STATUS ---");
    Serial.print("Power: ");
    Serial.println(hp->getOnOff() ? "ON" : "OFF");
    Serial.print("Temp: ");
    Serial.println(hp->getTemp());
    Serial.print("Mode: ");
    Serial.println(hp->getMode());
    Serial.print("Fan: ");
    Serial.println(hp->getFanMode());
    Serial.print("Bound: ");
    Serial.println(hp->isBound() ? "YES" : "NO");
    Serial.println("-----------------");
    Serial.println();
}


void SerialConsole::printHelp() {
    Serial.println();
    Serial.println("--- Available command groups ---");
    Serial.println("ac <command>   - Air conditioner control");
    Serial.println("vfd <command>  - Frequency drive control");
    Serial.println();
    Serial.println("Examples:");
    Serial.println("ac on");
    Serial.println("ac temp 23");
    Serial.println("ac status");
    Serial.println("vfd fwd");
    Serial.println("vfd hz 10.0");
    Serial.println("vfd stop");
    Serial.println();
    Serial.println("Type 'ac help' or 'vfd help'");
    Serial.println("------------------------------");
    Serial.println();
}


void SerialConsole::printAcHelp() {
    Serial.println();
    Serial.println("--- AC commands ---");
    Serial.println("ac debug on/off");
    Serial.println("ac on/off");
    Serial.println("ac temp 16-30");
    Serial.println("ac mode fan/dry/cool/heat/auto");
    Serial.println("ac fan 0-4");
    Serial.println("ac status");
    Serial.println("-------------------");
    Serial.println();
}


void SerialConsole::printVfdHelp() {
    Serial.println();
    Serial.println("--- VFD commands ---");
    Serial.println("vfd fwd");
    Serial.println("vfd rev");
    Serial.println("vfd stop");
    Serial.println("vfd hz <frequency>");
    Serial.println("vfd read <hexAddr> <count>");
    Serial.println("vfd write <hexAddr> <hexVal>");
    Serial.println();
    Serial.println("Examples:");
    Serial.println("vfd hz 10.0");
    Serial.println("vfd read 2100 1");
    Serial.println("vfd write 2000 0001");
    Serial.println("--------------------");
    Serial.println();
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