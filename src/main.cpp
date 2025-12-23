#include <Arduino.h>
#include "FujiHeatPump.h"

FujiHeatPump hp;

void setup() {
    Serial.begin(115200);
    Serial.println("-=-=-=-=- Master: Start -=-=-=-=- ");

    hp.connect(&Serial2, false, 16, 17);
    hp.setDebug(true);
}

void loop() {

    hp.waitForFrame();

}
