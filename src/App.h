// App.h
#pragma once

#include <Arduino.h>
#include "FujiHeatPump.h"
#include "SerialConsole.h"


class App {
public:
    void begin();
    void update();

private:
    FujiHeatPump hp;
    SerialConsole console;

};