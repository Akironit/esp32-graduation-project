// NetworkManager.cpp
#include "NetworkManager.h"

#include <WiFi.h>
#include <ArduinoOTA.h>
#include "Logger.h"

namespace {
constexpr const char* TAG_NET = "NET";
constexpr const char* TAG_OTA = "OTA";
}

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
    Logger::info(TAG_NET, "Initializing network...");

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

        Logger::warning(TAG_NET, "Wi-Fi disconnected, reconnecting...");
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
    Logger::infof(TAG_NET, "Connecting to Wi-Fi: %s", ssid);

    WiFi.begin(ssid, password);

    const unsigned long startMs = millis();
    const unsigned long timeoutMs = 15000;
    unsigned long lastWaitLogMs = 0;

    while (WiFi.status() != WL_CONNECTED && millis() - startMs < timeoutMs) {
        delay(250);

        const unsigned long now = millis();
        if (now - lastWaitLogMs >= 1000) {
            lastWaitLogMs = now;
            Logger::debug(TAG_NET, "Waiting for Wi-Fi connection...");
        }
    }

    if (WiFi.status() == WL_CONNECTED) {
        Logger::info(TAG_NET, "Wi-Fi connected");
        Logger::infof(TAG_NET, "IP address: %s", WiFi.localIP().toString().c_str());
    } else {
        Logger::warning(TAG_NET, "Wi-Fi connection timeout");
    }
}

void NetworkManager::setupOTA() {
    ArduinoOTA.setHostname(hostname);
    ArduinoOTA.setPassword(otaPassword);

    ArduinoOTA.onStart([]() {
        Logger::info(TAG_OTA, "Start");
    });

    ArduinoOTA.onEnd([]() {
        Logger::info(TAG_OTA, "End");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        const unsigned int percent = (progress * 100) / total;
        Logger::debugf(TAG_OTA, "Progress: %u%%", percent);
    });

    ArduinoOTA.onError([](ota_error_t error) {
        if (error == OTA_AUTH_ERROR) {
            Logger::errorf(TAG_OTA, "Error[%u]: Auth failed", error);
        } else if (error == OTA_BEGIN_ERROR) {
            Logger::errorf(TAG_OTA, "Error[%u]: Begin failed", error);
        } else if (error == OTA_CONNECT_ERROR) {
            Logger::errorf(TAG_OTA, "Error[%u]: Connect failed", error);
        } else if (error == OTA_RECEIVE_ERROR) {
            Logger::errorf(TAG_OTA, "Error[%u]: Receive failed", error);
        } else if (error == OTA_END_ERROR) {
            Logger::errorf(TAG_OTA, "Error[%u]: End failed", error);
        } else {
            Logger::errorf(TAG_OTA, "Error[%u]: Unknown error", error);
        }
    });

    ArduinoOTA.begin();

    Logger::info(TAG_OTA, "Ready");
    Logger::infof(TAG_OTA, "Hostname: %s", hostname);
}
