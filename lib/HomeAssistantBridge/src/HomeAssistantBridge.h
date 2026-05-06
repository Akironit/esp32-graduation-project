#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "DeviceController.h"
#include "DeviceState.h"

class HomeAssistantBridge {
public:
    void begin(
        bool enabled,
        const char* host,
        uint16_t port,
        const char* user,
        const char* password,
        const char* clientId,
        const char* baseTopic,
        DeviceState* state,
        DeviceController* controller
    );

    void update(bool networkConnected);

    bool isEnabled() const;
    bool isConnected();
    uint32_t getReconnectCount() const;
    uint32_t getPublishCount() const;
    uint32_t getCommandCount() const;
    bool hasPublished() const;
    unsigned long getLastPublishAgeMs() const;

private:
    WiFiClient wifiClient;
    PubSubClient mqttClient{wifiClient};

    bool enabled = false;
    const char* host = nullptr;
    uint16_t port = 1883;
    const char* user = nullptr;
    const char* password = nullptr;
    const char* clientId = nullptr;
    const char* baseTopic = nullptr;
    DeviceState* state = nullptr;
    DeviceController* controller = nullptr;
    unsigned long lastReconnectAttemptMs = 0;
    unsigned long lastPublishMs = 0;
    uint32_t reconnectCount = 0;
    uint32_t publishCount = 0;
    uint32_t commandCount = 0;
    bool publishedOnce = false;

    static constexpr unsigned long RECONNECT_INTERVAL_MS = 5000;
    static constexpr unsigned long PUBLISH_INTERVAL_MS = 5000;
    static HomeAssistantBridge* activeInstance;

    void reconnect();
    void publishState();
    void publishAvailability(bool online);
    void publishTopic(const char* suffix, const char* value, bool retained = false);
    void publishTopicf(const char* suffix, const char* format, ...);
    void subscribeCommands();
    void handleMessage(char* topic, byte* payload, unsigned int length);
    void handleCommand(const String& suffix, const String& payload);
    String topicSuffix(const char* topic) const;
    String payloadToString(byte* payload, unsigned int length) const;
    String commandTopic(const char* suffix) const;

    static void handleMqttMessage(char* topic, byte* payload, unsigned int length);
};
