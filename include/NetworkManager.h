// NetworkManager.h
#pragma once

#include <Arduino.h>
#include <IPAddress.h>

class NetworkManager {
public:
    NetworkManager(
        const char* ssid,
        const char* password,
        const char* hostname,
        const char* otaPassword
    );

    void begin();
    void update();

    bool isConnected() const;
    IPAddress getIp() const;

private:
    const char* ssid;
    const char* password;
    const char* hostname;
    const char* otaPassword;

    unsigned long lastReconnectAttemptMs = 0;
    static constexpr unsigned long RECONNECT_INTERVAL_MS = 10000;

    void connectWiFi();
    void setupOTA();
};