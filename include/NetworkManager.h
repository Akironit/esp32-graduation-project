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

    void begin(bool enabled = true);
    void update();
    void setEnabled(bool enabled);

    bool isEnabled() const;
    bool isConnected() const;
    IPAddress getIp() const;

private:
    const char* ssid;
    const char* password;
    const char* hostname;
    const char* otaPassword;

    bool enabled = true;
    bool otaStarted = false;
    unsigned long lastReconnectAttemptMs = 0;
    static constexpr unsigned long RECONNECT_INTERVAL_MS = 10000;

    void connectWiFi();
    void setupOTA();
};
