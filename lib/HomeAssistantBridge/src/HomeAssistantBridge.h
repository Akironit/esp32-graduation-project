#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "DeviceController.h"
#include "DeviceState.h"

// Home Assistant MQTT Discovery prefix. Default Home Assistant setting is "homeassistant".
#ifndef HA_DISCOVERY_PREFIX
#define HA_DISCOVERY_PREFIX "homeassistant"
#endif

// Device metadata shown in Home Assistant.
#ifndef HA_DEVICE_NAME
#define HA_DEVICE_NAME "Climate Controller"
#endif

#ifndef HA_DEVICE_MANUFACTURER
#define HA_DEVICE_MANUFACTURER "Korolev Artem"
#endif

#ifndef HA_DEVICE_MODEL
#define HA_DEVICE_MODEL "ESP32 Climate Controller"
#endif

// How often ESP32 publishes DeviceState values to MQTT.
#ifndef HA_PUBLISH_INTERVAL_MS
#define HA_PUBLISH_INTERVAL_MS 3000UL
#endif

// Base MQTT reconnect interval (ms) when broker is unavailable.
// Keep this fairly large: MQTT connect can block the main loop for a short time.
#ifndef HA_RECONNECT_INTERVAL_MS
#define HA_RECONNECT_INTERVAL_MS 60000UL
#endif

// Maximum retry interval after repeated MQTT connection failures (ms).
#ifndef HA_RECONNECT_BACKOFF_MAX_MS
#define HA_RECONNECT_BACKOFF_MAX_MS 300000UL
#endif

// MQTT packet wait timeout in seconds. Lower value reduces worst-case stalls
// when the broker is reachable by TCP but does not complete MQTT handshake.
#ifndef HA_MQTT_SOCKET_TIMEOUT_SECONDS
#define HA_MQTT_SOCKET_TIMEOUT_SECONDS 1
#endif

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
    void setEnabled(bool enabled);

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
    unsigned long reconnectIntervalMs = HA_RECONNECT_INTERVAL_MS;
    unsigned long lastPublishMs = 0;
    uint32_t reconnectCount = 0;
    uint32_t publishCount = 0;
    uint32_t commandCount = 0;
    uint8_t reconnectFailureCount = 0;
    bool reconnectAttempted = false;
    bool publishedOnce = false;
    bool discoveryPublished = false;

    static constexpr unsigned long RECONNECT_INTERVAL_MS = HA_RECONNECT_INTERVAL_MS;
    static constexpr unsigned long RECONNECT_BACKOFF_MAX_MS = HA_RECONNECT_BACKOFF_MAX_MS;
    static constexpr unsigned long PUBLISH_INTERVAL_MS = HA_PUBLISH_INTERVAL_MS;
    static HomeAssistantBridge* activeInstance;

    void reconnect();
    void publishState();
    void publishDiscovery();
    void publishSensorDiscovery(
        const char* objectId,
        const char* name,
        const char* entityObjectId,
        const char* stateSuffix,
        const char* deviceClass = nullptr,
        const char* unit = nullptr,
        const char* stateClass = nullptr
    );
    void publishBinarySensorDiscovery(
        const char* objectId,
        const char* name,
        const char* entityObjectId,
        const char* stateSuffix,
        const char* deviceClass = nullptr,
        const char* payloadOn = "ON",
        const char* payloadOff = "OFF"
    );
    void publishSwitchDiscovery(
        const char* objectId,
        const char* name,
        const char* entityObjectId,
        const char* stateSuffix,
        const char* commandSuffix
    );
    void publishNumberDiscovery(
        const char* objectId,
        const char* name,
        const char* entityObjectId,
        const char* stateSuffix,
        const char* commandSuffix,
        int min,
        int max,
        int step,
        const char* unit = nullptr
    );
    void publishSelectDiscovery(
        const char* objectId,
        const char* name,
        const char* entityObjectId,
        const char* stateSuffix,
        const char* commandSuffix,
        const char* optionsJson
    );
    void publishAvailability(bool online);
    void publishTopic(const char* suffix, const char* value, bool retained = false);
    void publishFullTopic(const char* topic, const char* value, bool retained = false);
    void publishTopicf(const char* suffix, const char* format, ...);
    void subscribeCommands();
    void handleMessage(char* topic, byte* payload, unsigned int length);
    void handleCommand(const String& suffix, const String& payload);
    const char* acModeName(uint8_t mode) const;
    uint8_t acModeValue(const String& mode) const;
    const char* acFanName(uint8_t fanMode) const;
    uint8_t acFanValue(const String& fanMode) const;
    const char* displayPageName(uint8_t pageIndex) const;
    const char* vfdRunState(const char* lastAction) const;
    String topicSuffix(const char* topic) const;
    String payloadToString(byte* payload, unsigned int length) const;
    String commandTopic(const char* suffix) const;

    static void handleMqttMessage(char* topic, byte* payload, unsigned int length);
};
