#include <Arduino.h>
#include "FujiHeatPump.h"

FujiHeatPump hp;

// Simple command parser
void processSerialCommand() {
    if (!Serial.available()) return;
    
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();
    
    if (cmd == "debug on") {
        hp.setDebug(true);
        Serial.println("=== Debug ENABLED ===");
        return;
    }
    
    if (cmd == "debug off") {
        hp.setDebug(false);
        Serial.println("=== Debug DISABLED ===");
        return;
    }
    
    if (cmd == "on") {
        hp.setOnOff(true);
        Serial.println(">>> Command: Power ON");
        return;
    }
    
    if (cmd == "off") {
        hp.setOnOff(false);
        Serial.println(">>> Command: Power OFF");
        return;
    }
    
    if (cmd.startsWith("temp ")) {
        int temp = cmd.substring(5).toInt();
        if (temp >= 16 && temp <= 30) {
            hp.setTemp(temp);
            Serial.print(">>> Command: Set temperature ");
            Serial.println(temp);
        } else {
            Serial.println("Error: Temp must be 16-30");
        }
        return;
    }
    
    if (cmd.startsWith("mode ")) {
        String modeStr = cmd.substring(5);
        byte modeVal = 0;
        if (modeStr == "fan") modeVal = 1;
        else if (modeStr == "dry") modeVal = 2;
        else if (modeStr == "cool") modeVal = 3;
        else if (modeStr == "heat") modeVal = 4;
        else if (modeStr == "auto") modeVal = 5;
        else {
            Serial.println("Error: mode fan/dry/cool/heat/auto");
            return;
        }
        hp.setMode(modeVal);
        Serial.print(">>> Command: Set mode ");
        Serial.println(modeStr);
        return;
    }

    if (cmd.startsWith("fan ")) {
        byte speed = (byte)cmd.substring(4).toInt();
        if (speed >= 0 && speed <= 4) {
            hp.setFanMode(speed);
            Serial.print(">>> Command: Set Fan mode ");
            Serial.println(speed);
            return;
        } else {
            Serial.println("Error: fan mode: [0-AUTO / 1-QUIET / 2-LOW / 3-MEDIUM / 4-HIGH]");
            return;
        }
        
    }
    
    if (cmd == "status") {
        Serial.println("\n--- CURRENT STATUS ---");
        Serial.print("Power: "); Serial.println(hp.getOnOff() ? "ON" : "OFF");
        Serial.print("Temp: "); Serial.println(hp.getTemp());
        Serial.print("Mode: "); Serial.println(hp.getMode());
        Serial.print("Fan: "); Serial.println(hp.getFanMode());
        Serial.print("Bound: "); Serial.println(hp.isBound() ? "YES" : "NO");
        Serial.println("----------------------\n");
        return;
    }
    
    if (cmd == "help") {
        Serial.println("\n--- Available commands ---");
        Serial.println("debug on/off  - Enable/disable debug output");
        Serial.println("on/off        - Power control");
        Serial.println("temp 16-30    - Set temperature");
        Serial.println("mode  - fan/dry/cool/heat/auto");
        Serial.println("fan   - [0-AUTO / 1-QUIET / 2-LOW / 3-MEDIUM / 4-HIGH]");
        Serial.println("status        - Show current state");
        Serial.println("help          - Show this help");
        Serial.println("-------------------------\n");
        return;
    }
    
    Serial.println("Unknown command. Type 'help'");
}

void setup() {
    Serial.begin(115200);
    Serial.println("-=-=-=-=- Controller: Start -=-=-=-=- ");

    hp.connect(&Serial2, false, 16, 17);
    hp.setDebug(true);
    
}

void loop() {

    processSerialCommand();

    if (hp.waitForFrame()) {   
        delay(60);            
        hp.sendPendingFrame();
    }

}
