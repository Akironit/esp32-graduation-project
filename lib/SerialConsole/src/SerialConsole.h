// SerialConsole.h
#pragma once

#include <Arduino.h>
#include "FujiHeatPump.h"
#include "VfdController.h"


class SerialConsole {
public:
    void begin(FujiHeatPump* heatPump, VfdController* vfdController);
    void update();

private:
    FujiHeatPump* hp = nullptr;
    VfdController* vfd = nullptr;

    void processCommand(const String& cmd);

    void processAcCommand(const String& cmd);
    void processVfdCommand(const String& cmd);

    void printHelp();
    void printAcHelp();
    void printVfdHelp();
    void printAcStatus();

    uint16_t parseHexU16(const String& value, bool& ok);
};