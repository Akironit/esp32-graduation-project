// App.h
#pragma once

#include <Arduino.h>
#include "FujiHeatPump.h"
#include "SerialConsole.h"
#include "VfdController.h"

// -=-=-=-=-= Pin definitions and settings -=-=-=-=-=-
// AC LIN bus (Serial1)
#define AC_LIN_TX1_PIN  16
#define AC_LIN_RX1_PIN  17
#define AC_RESET_PIN    36
#define IS_SECONDARY_CONTROLLER false
#define AC_DEBUG true

// VFD RS485 bus (Serial2)
#define RS485_TX2_PIN   32
#define RS485_RX2_PIN   33
#define RS485_DERE_PIN  18
#define RS485_BAUD  19200


class App {
public:
    void begin();
    void update();

private:
    FujiHeatPump hp;
    SerialConsole console;
    VfdController vfd{RS485_DERE_PIN};

};