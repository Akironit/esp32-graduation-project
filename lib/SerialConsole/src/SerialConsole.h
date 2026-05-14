// SerialConsole.h
// SerialConsole.h
#pragma once

#include <Arduino.h>
#include <WiFi.h>

#include "FujiHeatPump.h"
#include "VfdController.h"
#include "TemperatureSensors.h"
#include "DisplayUi.h"
#include "DeviceState.h"
#include "DeviceController.h"
#include "Logger.h"

#ifndef ENABLE_STATE_DEBUG_COMMANDS
#define ENABLE_STATE_DEBUG_COMMANDS 1
#endif

class ClimateAlgorithm;

class SerialConsole {
public:
    void begin(
        FujiHeatPump* hp,
        VfdController* vfd,
        TemperatureSensors* temp,
        DisplayUi* display = nullptr,
        DeviceState* state = nullptr,
        DeviceController* controller = nullptr,
        ClimateAlgorithm* climateAlgorithm = nullptr
    );
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
    DisplayUi* display = nullptr;
    DeviceState* state = nullptr;
    DeviceController* controller = nullptr;
    ClimateAlgorithm* climateAlgorithm = nullptr;

    WiFiServer telnetServer{23};
    WiFiClient telnetClient;
    ConsoleOutput consoleOutput{*this};

    bool telnetStarted = false;
    uint8_t telnetNegotiationBytesToSkip = 0;
    bool telnetLastOutputWasCarriageReturn = false;

    String serialBuffer;
    String telnetBuffer;
    bool serialLastInputWasCarriageReturn = false;
    bool telnetLastInputWasCarriageReturn = false;

    void updateSerialInput();
    void updateTelnet();
    void sendTelnetNegotiation();
    bool isTelnetCommandByte(uint8_t value);
    void handleInputChar(char c, String& buffer, bool echo, bool& lastInputWasCarriageReturn);
    void handleInputLine(String& buffer, bool echo);
    void printPrompt();
    void printLogHistory(Print& output);

    void processCommand(const String& cmd);

    void processAcCommand(const String& cmd);
    void processVfdCommand(const String& cmd);
    void processTempCommand(const String& cmd);
    void processAutoCommand(const String& cmd);
    void processDisplayCommand(const String& cmd);
    void processLogCommand(const String& cmd);
    void processStateCommand(const String& cmd);
#if ENABLE_STATE_DEBUG_COMMANDS
    void processDebugCommand(const String& cmd);
#endif

    void printHelp();
    void printAcHelp();
    void printVfdHelp();
    void printDisplayHelp();
    void printAutoHelp();
    void printLogHelp();
    void printStateHelp();
#if ENABLE_STATE_DEBUG_COMMANDS
    void printDebugHelp();
    void printDebugStatus();
#endif
    void printStateStatus();
    void printTemperatureStateStatus();
    void printDisplayStateStatus();
    void printAcStatus();

    uint16_t parseHexU16(const String& value, bool& ok);
    bool parseLogLevel(const String& value, LogLevel& level);
#if ENABLE_STATE_DEBUG_COMMANDS
    bool parseDeviceMode(const String& value, DeviceMode& mode);
    bool parseControllerActivity(const String& value, ControllerActivity& activity);
#endif
    const char* deviceModeName(DeviceMode mode) const;
    const char* controllerActivityName(ControllerActivity activity) const;

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
