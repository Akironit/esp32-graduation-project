// App.cpp
#include "App.h"



void App::begin() {
    console.begin(&hp, &vfd, &tempSensors);

    network.begin();

    if (network.isConnected()) {
        console.startTelnet();
    }

    hp.connect(&Serial1, IS_SECONDARY_CONTROLLER, AC_LIN_RX1_PIN, AC_LIN_TX1_PIN);
    hp.setDebug(AC_DEBUG);

    vfd.begin(Serial2, RS485_RX2_PIN, RS485_TX2_PIN, RS485_BAUD, SERIAL_8E1);

    tempSensors.begin(TEMP_ONE_WIRE_PIN);
}


void App::update() {
    network.update();

    if (network.isConnected()) {
        console.startTelnet();
    }

    console.update();
    tempSensors.update();
}

