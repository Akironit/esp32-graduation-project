#include "HomeAssistantBridge.h"

#include <stdarg.h>
#include <stdio.h>

#include "Logger.h"

namespace {
constexpr const char* TAG_HA = "HA";
}

HomeAssistantBridge* HomeAssistantBridge::activeInstance = nullptr;

void HomeAssistantBridge::begin(
    bool enabled,
    const char* host,
    uint16_t port,
    const char* user,
    const char* password,
    const char* clientId,
    const char* baseTopic,
    DeviceState* state,
    DeviceController* controller
) {
    this->enabled = enabled;
    this->host = host;
    this->port = port;
    this->user = user;
    this->password = password;
    this->clientId = clientId;
    this->baseTopic = baseTopic;
    this->state = state;
    this->controller = controller;

    if (!enabled) {
        Logger::info(TAG_HA, "MQTT bridge disabled");
        return;
    }

    activeInstance = this;
    mqttClient.setServer(host, port);
    mqttClient.setCallback(&HomeAssistantBridge::handleMqttMessage);

    Logger::infof(TAG_HA, "MQTT bridge configured for %s:%u", host, port);
}

void HomeAssistantBridge::update(bool networkConnected) {
    if (!enabled || host == nullptr || clientId == nullptr || baseTopic == nullptr) {
        return;
    }

    if (!networkConnected) {
        return;
    }

    if (!mqttClient.connected()) {
        const unsigned long now = millis();

        if (now - lastReconnectAttemptMs >= RECONNECT_INTERVAL_MS) {
            lastReconnectAttemptMs = now;
            reconnect();
        }

        return;
    }

    mqttClient.loop();

    const unsigned long now = millis();
    if (now - lastPublishMs >= PUBLISH_INTERVAL_MS) {
        lastPublishMs = now;
        publishState();
    }
}

bool HomeAssistantBridge::isEnabled() const {
    return enabled;
}

bool HomeAssistantBridge::isConnected() {
    return enabled && mqttClient.connected();
}

uint32_t HomeAssistantBridge::getReconnectCount() const {
    return reconnectCount;
}

uint32_t HomeAssistantBridge::getPublishCount() const {
    return publishCount;
}

uint32_t HomeAssistantBridge::getCommandCount() const {
    return commandCount;
}

bool HomeAssistantBridge::hasPublished() const {
    return publishedOnce;
}

unsigned long HomeAssistantBridge::getLastPublishAgeMs() const {
    if (!publishedOnce) {
        return 0;
    }

    return millis() - lastPublishMs;
}

void HomeAssistantBridge::reconnect() {
    Logger::info(TAG_HA, "Connecting to MQTT broker...");

    char availabilityTopic[96];
    snprintf(availabilityTopic, sizeof(availabilityTopic), "%s/status", baseTopic);

    bool connected = false;

    if (user != nullptr && strlen(user) > 0) {
        connected = mqttClient.connect(clientId, user, password, availabilityTopic, 0, true, "offline");
    } else {
        connected = mqttClient.connect(clientId, availabilityTopic, 0, true, "offline");
    }

    if (!connected) {
        Logger::warningf(TAG_HA, "MQTT connect failed, state=%d", mqttClient.state());
        return;
    }

    Logger::info(TAG_HA, "MQTT connected");
    reconnectCount++;
    publishAvailability(true);
    subscribeCommands();
    publishState();
}

void HomeAssistantBridge::publishState() {
    if (state == nullptr) {
        return;
    }

    publishedOnce = true;
    publishCount++;
    lastPublishMs = millis();

    publishTopic("state/wifi", state->wifiConnected ? "online" : "offline", true);
    publishTopic("state/ip", state->ip.toString().c_str(), true);
    publishTopicf("state/uptime_ms", "%lu", state->uptimeMs);

    publishTopic("state/ac/power", state->ac.powerOn ? "ON" : "OFF", true);
    publishTopic("state/ac/bound", state->ac.bound ? "ON" : "OFF", true);
    publishTopicf("state/ac/temp", "%u", state->ac.temperature);
    publishTopicf("state/ac/mode", "%u", state->ac.mode);
    publishTopicf("state/ac/fan", "%u", state->ac.fanMode);
    publishTopic("state/ac/role", state->ac.primaryController ? "primary" : "secondary", true);

    publishTopicf("state/temp/count", "%u", state->temperatures.sensorCount);

    for (uint8_t i = 0; i < state->temperatures.sensorCount && i < TEMP_MAX_SENSORS; i++) {
        char suffix[32];
        snprintf(suffix, sizeof(suffix), "state/temp/%u", i);
        publishTopicf(suffix, "%.2f", state->temperatures.values[i]);
    }

    publishTopic("state/vfd/last_action", state->vfd.lastAction, true);
    publishTopicf("state/vfd/requested_frequency", "%.2f", state->vfd.requestedFrequencyHz);
    publishTopicf("state/vfd/requests", "%lu", (unsigned long)state->vfd.requestCount);
    publishTopicf("state/vfd/errors", "%lu", (unsigned long)state->vfd.errorCount);

    publishTopic("state/display/page", state->display.pageName, true);
}

void HomeAssistantBridge::publishAvailability(bool online) {
    publishTopic("status", online ? "online" : "offline", true);
}

void HomeAssistantBridge::publishTopic(const char* suffix, const char* value, bool retained) {
    if (!mqttClient.connected() || baseTopic == nullptr) {
        return;
    }

    char topic[96];
    snprintf(topic, sizeof(topic), "%s/%s", baseTopic, suffix);
    mqttClient.publish(topic, value, retained);
}

void HomeAssistantBridge::publishTopicf(const char* suffix, const char* format, ...) {
    char value[64];
    va_list args;
    va_start(args, format);
    vsnprintf(value, sizeof(value), format, args);
    va_end(args);

    publishTopic(suffix, value, true);
}

void HomeAssistantBridge::subscribeCommands() {
    mqttClient.subscribe(commandTopic("ac/power").c_str());
    mqttClient.subscribe(commandTopic("ac/temp").c_str());
    mqttClient.subscribe(commandTopic("ac/mode").c_str());
    mqttClient.subscribe(commandTopic("ac/fan").c_str());
    mqttClient.subscribe(commandTopic("ac/debug").c_str());
    mqttClient.subscribe(commandTopic("display/page").c_str());
    mqttClient.subscribe(commandTopic("vfd/run").c_str());
    mqttClient.subscribe(commandTopic("vfd/hz").c_str());

    Logger::info(TAG_HA, "MQTT command topics subscribed");
}

void HomeAssistantBridge::handleMessage(char* topic, byte* payload, unsigned int length) {
    const String suffix = topicSuffix(topic);
    const String value = payloadToString(payload, length);
    commandCount++;

    Logger::debugf(TAG_HA, "Command %s = %s", suffix.c_str(), value.c_str());
    handleCommand(suffix, value);
}

void HomeAssistantBridge::handleCommand(const String& suffix, const String& payload) {
    if (controller == nullptr) {
        Logger::warning(TAG_HA, "MQTT command rejected: DeviceController is not connected");
        return;
    }

    if (suffix == "ac/power") {
        controller->setAcPower(payload == "ON" || payload == "on" || payload == "1");
    } else if (suffix == "ac/temp") {
        controller->setAcTemperature((uint8_t)payload.toInt());
    } else if (suffix == "ac/mode") {
        controller->setAcMode((uint8_t)payload.toInt());
    } else if (suffix == "ac/fan") {
        controller->setAcFanMode((uint8_t)payload.toInt());
    } else if (suffix == "ac/debug") {
        controller->setAcDebug(payload == "ON" || payload == "on" || payload == "1");
    } else if (suffix == "display/page") {
        if (payload == "next") {
            controller->displayNextPage();
        } else if (payload == "prev" || payload == "previous") {
            controller->displayPreviousPage();
        } else if (payload == "overview") {
            controller->displaySetPage(DisplayUi::Page::Overview);
        } else if (payload == "temp" || payload == "temperatures") {
            controller->displaySetPage(DisplayUi::Page::Temperatures);
        } else if (payload == "ac") {
            controller->displaySetPage(DisplayUi::Page::AirConditioner);
        } else if (payload == "network") {
            controller->displaySetPage(DisplayUi::Page::Network);
        }
    } else if (suffix == "vfd/run") {
        if (payload == "fwd" || payload == "forward") {
            controller->vfdForward();
        } else if (payload == "rev" || payload == "reverse") {
            controller->vfdReverse();
        } else if (payload == "stop") {
            controller->vfdStop();
        }
    } else if (suffix == "vfd/hz") {
        controller->vfdSetFrequency(payload.toFloat());
    }
}

String HomeAssistantBridge::topicSuffix(const char* topic) const {
    const String fullTopic(topic);
    const String prefix = commandTopic("");

    if (!fullTopic.startsWith(prefix)) {
        return fullTopic;
    }

    return fullTopic.substring(prefix.length());
}

String HomeAssistantBridge::payloadToString(byte* payload, unsigned int length) const {
    String value;
    value.reserve(length);

    for (unsigned int i = 0; i < length; i++) {
        value += (char)payload[i];
    }

    value.trim();
    return value;
}

String HomeAssistantBridge::commandTopic(const char* suffix) const {
    String topic(baseTopic);
    topic += "/cmd/";
    topic += suffix;
    return topic;
}

void HomeAssistantBridge::handleMqttMessage(char* topic, byte* payload, unsigned int length) {
    if (activeInstance != nullptr) {
        activeInstance->handleMessage(topic, payload, length);
    }
}
