// SerialConsole.h
#pragma once

#include <Arduino.h>
#include "FujiHeatPump.h"


class SerialConsole {
public:
    void begin(FujiHeatPump* heatPump);
    void update();

private:
    FujiHeatPump* hp = nullptr;

    void processCommand(const String& cmd);
    void printHelp();
    void printStatus();

};