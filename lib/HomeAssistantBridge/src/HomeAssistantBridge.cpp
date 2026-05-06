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
    publishDiscovery();
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

void HomeAssistantBridge::publishDiscovery() {
    if (discoveryPublished || state == nullptr) {
        return;
    }

    publishSensorDiscovery("ip", "IP address", "state/ip");
    publishSensorDiscovery("uptime", "Uptime", "state/uptime_ms", "duration", "ms", "measurement");
    publishBinarySensorDiscovery("wifi", "Wi-Fi", "state/wifi", "connectivity", "online", "offline");

    publishSwitchDiscovery("ac_power", "AC power", "state/ac/power", "cmd/ac/power");
    publishBinarySensorDiscovery("ac_bound", "AC bound", "state/ac/bound", "connectivity");
    publishNumberDiscovery("ac_temp", "AC temperature", "state/ac/temp", "cmd/ac/temp", 16, 30, 1, "C");
    publishNumberDiscovery("ac_fan", "AC fan", "state/ac/fan", "cmd/ac/fan", 0, 4, 1);
    publishSelectDiscovery("ac_mode", "AC mode", "state/ac/mode", "cmd/ac/mode", "[\"1\",\"2\",\"3\",\"4\",\"5\"]");
    publishSensorDiscovery("ac_role", "AC role", "state/ac/role");

    publishSensorDiscovery("temperature_count", "Temperature sensor count", "state/temp/count");

    for (uint8_t i = 0; i < state->temperatures.sensorCount && i < TEMP_MAX_SENSORS; i++) {
        char objectId[32];
        char name[48];
        char suffix[32];
        snprintf(objectId, sizeof(objectId), "temperature_%u", i);
        snprintf(name, sizeof(name), "Temperature %u", i);
        snprintf(suffix, sizeof(suffix), "state/temp/%u", i);
        publishSensorDiscovery(objectId, name, suffix, "temperature", "C", "measurement");
    }

    publishSensorDiscovery("vfd_last_action", "VFD last action", "state/vfd/last_action");
    publishSensorDiscovery("vfd_requested_frequency", "VFD requested frequency", "state/vfd/requested_frequency", nullptr, "Hz", "measurement");
    publishSensorDiscovery("vfd_requests", "VFD requests", "state/vfd/requests", nullptr, nullptr, "total_increasing");
    publishSensorDiscovery("vfd_errors", "VFD errors", "state/vfd/errors", nullptr, nullptr, "total_increasing");
    publishNumberDiscovery("vfd_frequency", "VFD frequency command", "state/vfd/requested_frequency", "cmd/vfd/hz", 0, 50, 1, "Hz");
    publishSelectDiscovery("vfd_run", "VFD run command", "state/vfd/last_action", "cmd/vfd/run", "[\"fwd\",\"rev\",\"stop\"]");

    publishSelectDiscovery(
        "display_page",
        "Display page",
        "state/display/page",
        "cmd/display/page",
        "[\"overview\",\"temp\",\"ac\",\"network\",\"next\",\"prev\"]"
    );

    discoveryPublished = true;
    Logger::info(TAG_HA, "MQTT discovery published");
}

void HomeAssistantBridge::publishSensorDiscovery(
    const char* objectId,
    const char* name,
    const char* stateSuffix,
    const char* deviceClass,
    const char* unit,
    const char* stateClass
) {
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/sensor/%s_%s/config", HA_DISCOVERY_PREFIX, baseTopic, objectId);

    char payload[512];
    snprintf(
        payload,
        sizeof(payload),
        "{\"name\":\"%s\",\"unique_id\":\"%s_%s\",\"state_topic\":\"%s/%s\",\"availability_topic\":\"%s/status\",\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\",\"manufacturer\":\"%s\",\"model\":\"%s\"}%s%s%s}",
        name,
        baseTopic,
        objectId,
        baseTopic,
        stateSuffix,
        baseTopic,
        baseTopic,
        HA_DEVICE_NAME,
        HA_DEVICE_MANUFACTURER,
        HA_DEVICE_MODEL,
        deviceClass != nullptr ? ",\"device_class\":\"" : "",
        deviceClass != nullptr ? deviceClass : "",
        deviceClass != nullptr ? "\"" : ""
    );

    if (unit != nullptr) {
        const size_t length = strlen(payload);
        snprintf(payload + length - 1, sizeof(payload) - length + 1, ",\"unit_of_measurement\":\"%s\"}", unit);
    }

    if (stateClass != nullptr) {
        const size_t length = strlen(payload);
        snprintf(payload + length - 1, sizeof(payload) - length + 1, ",\"state_class\":\"%s\"}", stateClass);
    }

    publishFullTopic(topic, payload, true);
}

void HomeAssistantBridge::publishBinarySensorDiscovery(
    const char* objectId,
    const char* name,
    const char* stateSuffix,
    const char* deviceClass,
    const char* payloadOn,
    const char* payloadOff
) {
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/binary_sensor/%s_%s/config", HA_DISCOVERY_PREFIX, baseTopic, objectId);

    char payload[512];
    snprintf(
        payload,
        sizeof(payload),
        "{\"name\":\"%s\",\"unique_id\":\"%s_%s\",\"state_topic\":\"%s/%s\",\"availability_topic\":\"%s/status\",\"payload_on\":\"%s\",\"payload_off\":\"%s\",\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\",\"manufacturer\":\"%s\",\"model\":\"%s\"}%s%s%s}",
        name,
        baseTopic,
        objectId,
        baseTopic,
        stateSuffix,
        baseTopic,
        payloadOn,
        payloadOff,
        baseTopic,
        HA_DEVICE_NAME,
        HA_DEVICE_MANUFACTURER,
        HA_DEVICE_MODEL,
        deviceClass != nullptr ? ",\"device_class\":\"" : "",
        deviceClass != nullptr ? deviceClass : "",
        deviceClass != nullptr ? "\"" : ""
    );

    publishFullTopic(topic, payload, true);
}

void HomeAssistantBridge::publishSwitchDiscovery(
    const char* objectId,
    const char* name,
    const char* stateSuffix,
    const char* commandSuffix
) {
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/switch/%s_%s/config", HA_DISCOVERY_PREFIX, baseTopic, objectId);

    char payload[512];
    snprintf(
        payload,
        sizeof(payload),
        "{\"name\":\"%s\",\"unique_id\":\"%s_%s\",\"state_topic\":\"%s/%s\",\"command_topic\":\"%s/%s\",\"availability_topic\":\"%s/status\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\",\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\",\"manufacturer\":\"%s\",\"model\":\"%s\"}}",
        name,
        baseTopic,
        objectId,
        baseTopic,
        stateSuffix,
        baseTopic,
        commandSuffix,
        baseTopic,
        baseTopic,
        HA_DEVICE_NAME,
        HA_DEVICE_MANUFACTURER,
        HA_DEVICE_MODEL
    );

    publishFullTopic(topic, payload, true);
}

void HomeAssistantBridge::publishNumberDiscovery(
    const char* objectId,
    const char* name,
    const char* stateSuffix,
    const char* commandSuffix,
    int min,
    int max,
    int step,
    const char* unit
) {
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/number/%s_%s/config", HA_DISCOVERY_PREFIX, baseTopic, objectId);

    char payload[512];
    snprintf(
        payload,
        sizeof(payload),
        "{\"name\":\"%s\",\"unique_id\":\"%s_%s\",\"state_topic\":\"%s/%s\",\"command_topic\":\"%s/%s\",\"availability_topic\":\"%s/status\",\"min\":%d,\"max\":%d,\"step\":%d,\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\",\"manufacturer\":\"%s\",\"model\":\"%s\"}%s%s%s}",
        name,
        baseTopic,
        objectId,
        baseTopic,
        stateSuffix,
        baseTopic,
        commandSuffix,
        baseTopic,
        min,
        max,
        step,
        baseTopic,
        HA_DEVICE_NAME,
        HA_DEVICE_MANUFACTURER,
        HA_DEVICE_MODEL,
        unit != nullptr ? ",\"unit_of_measurement\":\"" : "",
        unit != nullptr ? unit : "",
        unit != nullptr ? "\"" : ""
    );

    publishFullTopic(topic, payload, true);
}

void HomeAssistantBridge::publishSelectDiscovery(
    const char* objectId,
    const char* name,
    const char* stateSuffix,
    const char* commandSuffix,
    const char* optionsJson
) {
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/select/%s_%s/config", HA_DISCOVERY_PREFIX, baseTopic, objectId);

    char payload[640];
    snprintf(
        payload,
        sizeof(payload),
        "{\"name\":\"%s\",\"unique_id\":\"%s_%s\",\"state_topic\":\"%s/%s\",\"command_topic\":\"%s/%s\",\"availability_topic\":\"%s/status\",\"options\":%s,\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\",\"manufacturer\":\"%s\",\"model\":\"%s\"}}",
        name,
        baseTopic,
        objectId,
        baseTopic,
        stateSuffix,
        baseTopic,
        commandSuffix,
        baseTopic,
        optionsJson,
        baseTopic,
        HA_DEVICE_NAME,
        HA_DEVICE_MANUFACTURER,
        HA_DEVICE_MODEL
    );

    publishFullTopic(topic, payload, true);
}

void HomeAssistantBridge::publishTopic(const char* suffix, const char* value, bool retained) {
    if (!mqttClient.connected() || baseTopic == nullptr) {
        return;
    }

    char topic[96];
    snprintf(topic, sizeof(topic), "%s/%s", baseTopic, suffix);

    if (!mqttClient.publish(topic, value, retained)) {
        Logger::warningf(TAG_HA, "MQTT publish failed: %s", topic);
    }
}

void HomeAssistantBridge::publishFullTopic(const char* topic, const char* value, bool retained) {
    if (!mqttClient.connected()) {
        return;
    }

    if (!mqttClient.publish(topic, value, retained)) {
        Logger::warningf(TAG_HA, "MQTT publish failed: %s", topic);
    }
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
