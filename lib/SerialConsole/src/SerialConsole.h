// SerialConsole.h
// SerialConsole.h
#pragma once

#include <Arduino.h>
#include <WiFi.h>

#include "FujiHeatPump.h"
#include "VfdController.h"
#include "TemperatureSensors.h"


class SerialConsole {
public:
    void begin(FujiHeatPump* hp, VfdController* vfd, TemperatureSensors* temp);
    void update();
    void startTelnet();

private:
    class ConsoleOutput : public Print {
    public:
        explicit ConsoleOutput(SerialConsole& console);

        size_t write(uint8_t value) override;
        size_t write(const uint8_t* buffer, size_t size) override;

    private:
        SerialConsole& console;
    };

    FujiHeatPump* hp = nullptr;
    VfdController* vfd = nullptr;
    TemperatureSensors* temp = nullptr;

    WiFiServer telnetServer{23};
    WiFiClient telnetClient;
    ConsoleOutput consoleOutput{*this};

    bool telnetStarted = false;
    uint8_t telnetNegotiationBytesToSkip = 0;

    String serialBuffer;
    String telnetBuffer;
    bool serialLastInputWasCarriageReturn = false;
    bool telnetLastInputWasCarriageReturn = false;

    void updateSerialInput();
    void updateTelnet();
    bool isTelnetCommandByte(uint8_t value);
    void handleInputChar(char c, String& buffer, bool echo, bool& lastInputWasCarriageReturn);
    void handleInputLine(String& buffer, bool echo);
    void printPrompt();

    void processCommand(const String& cmd);

    void processAcCommand(const String& cmd);
    void processVfdCommand(const String& cmd);
    void processTempCommand(const String& cmd);

    void printHelp();
    void printAcHelp();
    void printVfdHelp();
    void printAcStatus();

    uint16_t parseHexU16(const String& value, bool& ok);

    void print(const String& value);
    void print(const char* value);
    void print(int value);
    void print(uint16_t value);
    void print(float value, int digits = 2);

    void println();
    void println(const String& value);
    void println(const char* value);
    void println(int value);
    void println(uint16_t value);
    void println(float value, int digits = 2);
};
