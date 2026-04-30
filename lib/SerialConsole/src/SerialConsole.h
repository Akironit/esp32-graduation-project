// SerialConsole.h
#pragma once

#include <Arduino.h>
#include "FujiHeatPump.h"
#include "VfdController.h"
#include "TemperatureSensors.h"


class SerialConsole {
public:
    void begin(FujiHeatPump* hp, VfdController* vfd, TemperatureSensors* temp);
    void update();

private:
    FujiHeatPump* hp = nullptr;
    VfdController* vfd = nullptr;
    TemperatureSensors* temp = nullptr;

    void processCommand(const String& cmd);

    void processAcCommand(const String& cmd);
    void processVfdCommand(const String& cmd);
    void processTempCommand(const String& cmd);

    void printHelp();
    void printAcHelp();
    void printVfdHelp();
    void printAcStatus();

    uint16_t parseHexU16(const String& value, bool& ok);
};