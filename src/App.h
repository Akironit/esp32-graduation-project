// App.h
#pragma once

#include <Arduino.h>
#include "NetworkManager.h"
#include "Secrets.h"
#include "FujiHeatPump.h"
#include "SerialConsole.h"
#include "VfdController.h"
#include "TemperatureSensors.h"
#include "Mcp23017Expander.h"

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

#define TEMP_ONE_WIRE_PIN 4

// I2C bus
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22
#define MCP23017_ADDRESS 0x20


class App {
public:
    void begin();
    void update();

private:
    FujiHeatPump hp;
    SerialConsole console;
    VfdController vfd{RS485_DERE_PIN};
    TemperatureSensors tempSensors;
    Mcp23017Expander ioExpander;
    NetworkManager network{
        WIFI_SSID,
        WIFI_PASSWORD,
        DEVICE_HOSTNAME,
        OTA_PASSWORD
    };
};
