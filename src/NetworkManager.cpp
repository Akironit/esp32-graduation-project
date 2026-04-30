// NetworkManager.cpp
#include "NetworkManager.h"

#include <WiFi.h>
#include <ArduinoOTA.h>

NetworkManager::NetworkManager(
    const char* ssid,
    const char* password,
    const char* hostname,
    const char* otaPassword
)
    : ssid(ssid),
      password(password),
      hostname(hostname),
      otaPassword(otaPassword) {
}

void NetworkManager::begin() {
    Serial.println("[NET] Initializing network...");

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(hostname);
    WiFi.setAutoReconnect(true);

    connectWiFi();

    if (isConnected()) {
        setupOTA();
    }
}

void NetworkManager::update() {
    if (isConnected()) {
        ArduinoOTA.handle();
        return;
    }

    const unsigned long now = millis();

    if (now - lastReconnectAttemptMs >= RECONNECT_INTERVAL_MS) {
        lastReconnectAttemptMs = now;

        Serial.println("[NET] Wi-Fi disconnected, reconnecting...");
        WiFi.disconnect();
        WiFi.begin(ssid, password);
    }
}

bool NetworkManager::isConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

IPAddress NetworkManager::getIp() const {
    return WiFi.localIP();
}

void NetworkManager::connectWiFi() {
    Serial.print("[NET] Connecting to Wi-Fi: ");
    Serial.println(ssid);

    WiFi.begin(ssid, password);

    const unsigned long startMs = millis();
    const unsigned long timeoutMs = 15000;

    while (WiFi.status() != WL_CONNECTED && millis() - startMs < timeoutMs) {
        delay(250);
        Serial.print(".");
    }

    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[NET] Wi-Fi connected");
        Serial.print("[NET] IP address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("[NET] Wi-Fi connection timeout");
    }
}

void NetworkManager::setupOTA() {
    ArduinoOTA.setHostname(hostname);
    ArduinoOTA.setPassword(otaPassword);

    ArduinoOTA.onStart([]() {
        Serial.println("[OTA] Start");
    });

    ArduinoOTA.onEnd([]() {
        Serial.println();
        Serial.println("[OTA] End");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("[OTA] Progress: %u%%\r", (progress * 100) / total);
    });

    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("[OTA] Error[%u]: ", error);

        if (error == OTA_AUTH_ERROR) {
            Serial.println("Auth failed");
        } else if (error == OTA_BEGIN_ERROR) {
            Serial.println("Begin failed");
        } else if (error == OTA_CONNECT_ERROR) {
            Serial.println("Connect failed");
        } else if (error == OTA_RECEIVE_ERROR) {
            Serial.println("Receive failed");
        } else if (error == OTA_END_ERROR) {
            Serial.println("End failed");
        } else {
            Serial.println("Unknown error");
        }
    });

    ArduinoOTA.begin();

    Serial.println("[OTA] Ready");
    Serial.print("[OTA] Hostname: ");
    Serial.println(hostname);
}