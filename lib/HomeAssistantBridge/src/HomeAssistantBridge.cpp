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
    mqttClient.setSocketTimeout(HA_MQTT_SOCKET_TIMEOUT_SECONDS);
    mqttClient.setKeepAlive(15);

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

        if (!reconnectAttempted || now - lastReconnectAttemptMs >= reconnectIntervalMs) {
            reconnectAttempted = true;
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
    Logger::infof(TAG_HA, "Connecting to MQTT broker... retry interval=%lu ms", reconnectIntervalMs);

    char availabilityTopic[96];
    snprintf(availabilityTopic, sizeof(availabilityTopic), "%s/status", baseTopic);

    bool connected = false;

    if (user != nullptr && strlen(user) > 0) {
        connected = mqttClient.connect(clientId, user, password, availabilityTopic, 0, true, "offline");
    } else {
        connected = mqttClient.connect(clientId, availabilityTopic, 0, true, "offline");
    }

    if (!connected) {
        if (reconnectFailureCount < 8) {
            reconnectFailureCount++;
        }

        unsigned long nextIntervalMs = RECONNECT_INTERVAL_MS;
        for (uint8_t i = 1; i < reconnectFailureCount; i++) {
            if (nextIntervalMs >= RECONNECT_BACKOFF_MAX_MS / 2) {
                nextIntervalMs = RECONNECT_BACKOFF_MAX_MS;
                break;
            }
            nextIntervalMs *= 2;
        }

        if (nextIntervalMs > RECONNECT_BACKOFF_MAX_MS) {
            nextIntervalMs = RECONNECT_BACKOFF_MAX_MS;
        }

        reconnectIntervalMs = nextIntervalMs;
        Logger::warningf(
            TAG_HA,
            "MQTT connect failed, state=%d, next retry in %lu ms",
            mqttClient.state(),
            reconnectIntervalMs
        );
        return;
    }

    Logger::info(TAG_HA, "MQTT connected");
    reconnectCount++;
    reconnectFailureCount = 0;
    reconnectIntervalMs = RECONNECT_INTERVAL_MS;
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
    publishTopic("state/uptime", state->uptimeText, true);
    publishTopicf("state/uptime_ms", "%lu", state->uptimeMs);

    publishTopic("state/ac/power", state->ac.powerOn ? "ON" : "OFF", true);
    publishTopic("state/ac/bound", state->ac.bound ? "ON" : "OFF", true);
    publishTopicf("state/ac/temp", "%u", state->ac.temperature);
    publishTopic("state/ac/mode", acModeName(state->ac.mode), true);
    publishTopic("state/ac/fan", acFanName(state->ac.fanMode), true);
    publishTopic("state/ac/role", state->ac.primaryController ? "primary" : "secondary", true);

    publishTopicf("state/temp/count", "%u", state->temperatures.sensorCount);

    for (uint8_t i = 0; i < state->temperatures.sensorCount && i < TEMP_MAX_SENSORS; i++) {
        char suffix[32];
        snprintf(suffix, sizeof(suffix), "state/temp/%u", i);
        publishTopicf(suffix, "%.2f", state->temperatures.values[i]);
    }

    publishTopic("state/vfd/last_action", state->vfd.lastAction, true);
    publishTopic("state/vfd/online", state->vfd.online ? "online" : "offline", true);
    publishTopic("state/vfd/link", state->vfd.communicationError ? "error" : ((state->vfd.everOnline || state->vfd.online) ? "linked" : "waiting"), true);
    publishTopic("state/vfd/run", state->vfd.statusWord == 0x0002 ? "rev" : (state->vfd.running ? "fwd" : "stop"), true);
    publishTopicf("state/vfd/status_word", "%u", state->vfd.statusWord);
    publishTopicf("state/vfd/requested_frequency", "%.2f", state->vfd.requestedFrequencyHz);
    publishTopicf("state/vfd/actual_frequency", "%.2f", state->vfd.actualFrequencyHz);
    publishTopicf("state/vfd/actual_step", "%u", state->vfd.actualStep);
    publishTopicf("state/vfd/requests", "%lu", (unsigned long)state->vfd.requestCount);
    publishTopicf("state/vfd/errors", "%lu", (unsigned long)state->vfd.errorCount);

    publishTopic("state/display/page", displayPageName(state->display.pageIndex), true);
}

void HomeAssistantBridge::publishAvailability(bool online) {
    publishTopic("status", online ? "online" : "offline", true);
}

void HomeAssistantBridge::publishDiscovery() {
    if (discoveryPublished || state == nullptr) {
        return;
    }

    publishSensorDiscovery("ip", "IP address", "climate_controller_ip", "state/ip");
    publishSensorDiscovery("uptime", "Uptime", "climate_controller_uptime", "state/uptime");
    publishSensorDiscovery("uptime_ms", "Uptime milliseconds", "climate_controller_uptime_ms", "state/uptime_ms", "duration", "ms", "measurement");
    publishBinarySensorDiscovery("wifi", "Wi-Fi", "climate_controller_wifi", "state/wifi", "connectivity", "online", "offline");

    publishSwitchDiscovery("ac_power", "AC power", "climate_controller_ac_power", "state/ac/power", "cmd/ac/power");
    publishBinarySensorDiscovery("ac_bound", "AC bound", "climate_controller_ac_bound", "state/ac/bound", "connectivity");
    publishNumberDiscovery("ac_temp", "AC temperature", "climate_controller_ac_temperature", "state/ac/temp", "cmd/ac/temp", 16, 30, 1, "C");
    publishSelectDiscovery("ac_fan", "AC fan", "climate_controller_ac_fan", "state/ac/fan", "cmd/ac/fan", "[\"auto\",\"low\",\"medium\",\"high\",\"max\"]");
    publishSelectDiscovery("ac_mode", "AC mode", "climate_controller_ac_mode", "state/ac/mode", "cmd/ac/mode", "[\"unknown\",\"fan\",\"dry\",\"cool\",\"heat\",\"auto\"]");
    publishSensorDiscovery("ac_role", "AC role", "climate_controller_ac_role", "state/ac/role");

    publishSensorDiscovery("temperature_count", "Temperature sensor count", "climate_controller_temperature_count", "state/temp/count");

    for (uint8_t i = 0; i < state->temperatures.sensorCount && i < TEMP_MAX_SENSORS; i++) {
        char objectId[32];
        char name[48];
        char entityObjectId[48];
        char suffix[32];
        snprintf(objectId, sizeof(objectId), "temperature_%u", i);
        snprintf(name, sizeof(name), "Temperature %u", i);
        snprintf(entityObjectId, sizeof(entityObjectId), "climate_controller_temperature_%u", i);
        snprintf(suffix, sizeof(suffix), "state/temp/%u", i);
        publishSensorDiscovery(objectId, name, entityObjectId, suffix, "temperature", "C", "measurement");
    }

    publishSensorDiscovery("vfd_last_action", "VFD last action", "climate_controller_vfd_last_action", "state/vfd/last_action");
    publishBinarySensorDiscovery("vfd_online", "VFD online", "climate_controller_vfd_online", "state/vfd/online", "connectivity", "online", "offline");
    publishSensorDiscovery("vfd_link", "VFD link", "climate_controller_vfd_link", "state/vfd/link");
    publishSensorDiscovery("vfd_status_word", "VFD status word", "climate_controller_vfd_status_word", "state/vfd/status_word");
    publishSensorDiscovery("vfd_requested_frequency", "VFD requested frequency", "climate_controller_vfd_requested_frequency", "state/vfd/requested_frequency", nullptr, "Hz", "measurement");
    publishSensorDiscovery("vfd_actual_frequency", "VFD actual frequency", "climate_controller_vfd_actual_frequency", "state/vfd/actual_frequency", nullptr, "Hz", "measurement");
    publishSensorDiscovery("vfd_actual_step", "VFD actual step", "climate_controller_vfd_actual_step", "state/vfd/actual_step");
    publishSensorDiscovery("vfd_requests", "VFD requests", "climate_controller_vfd_requests", "state/vfd/requests", nullptr, nullptr, "total_increasing");
    publishSensorDiscovery("vfd_errors", "VFD errors", "climate_controller_vfd_errors", "state/vfd/errors", nullptr, nullptr, "total_increasing");
    publishNumberDiscovery("vfd_frequency", "VFD frequency command", "climate_controller_vfd_frequency", "state/vfd/requested_frequency", "cmd/vfd/hz", 20, 50, 1, "Hz");
    publishSelectDiscovery("vfd_run", "VFD run command", "climate_controller_vfd_run", "state/vfd/run", "cmd/vfd/run", "[\"unknown\",\"fwd\",\"rev\",\"stop\"]");

    publishSelectDiscovery(
        "display_page",
        "Display page",
        "climate_controller_display_page",
        "state/display/page",
        "cmd/display/page",
        "[\"overview\",\"ac\",\"vent\",\"temp\",\"settings\",\"diag\",\"next\",\"prev\"]"
    );

    discoveryPublished = true;
    Logger::info(TAG_HA, "MQTT discovery published");
}

void HomeAssistantBridge::publishSensorDiscovery(
    const char* objectId,
    const char* name,
    const char* entityObjectId,
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
        "{\"name\":\"%s\",\"object_id\":\"%s\",\"unique_id\":\"%s_%s\",\"has_entity_name\":true,\"state_topic\":\"%s/%s\",\"availability_topic\":\"%s/status\",\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\",\"manufacturer\":\"%s\",\"model\":\"%s\"}%s%s%s}",
        name,
        entityObjectId,
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
    const char* entityObjectId,
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
        "{\"name\":\"%s\",\"object_id\":\"%s\",\"unique_id\":\"%s_%s\",\"has_entity_name\":true,\"state_topic\":\"%s/%s\",\"availability_topic\":\"%s/status\",\"payload_on\":\"%s\",\"payload_off\":\"%s\",\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\",\"manufacturer\":\"%s\",\"model\":\"%s\"}%s%s%s}",
        name,
        entityObjectId,
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
    const char* entityObjectId,
    const char* stateSuffix,
    const char* commandSuffix
) {
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/switch/%s_%s/config", HA_DISCOVERY_PREFIX, baseTopic, objectId);

    char payload[512];
    snprintf(
        payload,
        sizeof(payload),
        "{\"name\":\"%s\",\"object_id\":\"%s\",\"unique_id\":\"%s_%s\",\"has_entity_name\":true,\"state_topic\":\"%s/%s\",\"command_topic\":\"%s/%s\",\"availability_topic\":\"%s/status\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\",\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\",\"manufacturer\":\"%s\",\"model\":\"%s\"}}",
        name,
        entityObjectId,
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
    const char* entityObjectId,
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
        "{\"name\":\"%s\",\"object_id\":\"%s\",\"unique_id\":\"%s_%s\",\"has_entity_name\":true,\"state_topic\":\"%s/%s\",\"command_topic\":\"%s/%s\",\"availability_topic\":\"%s/status\",\"min\":%d,\"max\":%d,\"step\":%d,\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\",\"manufacturer\":\"%s\",\"model\":\"%s\"}%s%s%s}",
        name,
        entityObjectId,
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
    const char* entityObjectId,
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
        "{\"name\":\"%s\",\"object_id\":\"%s\",\"unique_id\":\"%s_%s\",\"has_entity_name\":true,\"state_topic\":\"%s/%s\",\"command_topic\":\"%s/%s\",\"availability_topic\":\"%s/status\",\"options\":%s,\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\",\"manufacturer\":\"%s\",\"model\":\"%s\"}}",
        name,
        entityObjectId,
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
        controller->setAcMode(acModeValue(payload));
    } else if (suffix == "ac/fan") {
        controller->setAcFanMode(acFanValue(payload));
    } else if (suffix == "ac/debug") {
        controller->setAcDebug(payload == "ON" || payload == "on" || payload == "1");
    } else if (suffix == "display/page") {
        if (payload == "next") {
            controller->displayNextPage();
        } else if (payload == "prev" || payload == "previous") {
            controller->displayPreviousPage();
        } else if (payload == "overview") {
            controller->displaySetPage(DisplayUi::Page::Overview);
        } else if (payload == "ac") {
            controller->displaySetPage(DisplayUi::Page::AirConditioner);
        } else if (payload == "vent" || payload == "ventilation") {
            controller->displaySetPage(DisplayUi::Page::Ventilation);
        } else if (payload == "temp" || payload == "temperatures") {
            controller->displaySetPage(DisplayUi::Page::Temperatures);
        } else if (payload == "settings" || payload == "network") {
            controller->displaySetPage(DisplayUi::Page::Settings);
        } else if (payload == "diag" || payload == "diagnostics") {
            controller->displaySetPage(DisplayUi::Page::Diagnostics);
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

const char* HomeAssistantBridge::acModeName(uint8_t mode) const {
    switch (mode) {
        case 1:
            return "fan";
        case 2:
            return "dry";
        case 3:
            return "cool";
        case 4:
            return "heat";
        case 5:
            return "auto";
        default:
            return "unknown";
    }
}

uint8_t HomeAssistantBridge::acModeValue(const String& mode) const {
    if (mode == "fan") {
        return 1;
    }
    if (mode == "dry") {
        return 2;
    }
    if (mode == "cool") {
        return 3;
    }
    if (mode == "heat") {
        return 4;
    }
    if (mode == "auto") {
        return 5;
    }

    return (uint8_t)mode.toInt();
}

const char* HomeAssistantBridge::acFanName(uint8_t fanMode) const {
    switch (fanMode) {
        case 0:
            return "auto";
        case 1:
            return "low";
        case 2:
            return "medium";
        case 3:
            return "high";
        case 4:
            return "max";
        default:
            return "unknown";
    }
}

uint8_t HomeAssistantBridge::acFanValue(const String& fanMode) const {
    if (fanMode == "auto") {
        return 0;
    }
    if (fanMode == "low") {
        return 1;
    }
    if (fanMode == "medium") {
        return 2;
    }
    if (fanMode == "high") {
        return 3;
    }
    if (fanMode == "max") {
        return 4;
    }

    return (uint8_t)fanMode.toInt();
}

const char* HomeAssistantBridge::displayPageName(uint8_t pageIndex) const {
    switch (pageIndex) {
        case 0:
            return "overview";
        case 1:
            return "ac";
        case 2:
            return "vent";
        case 3:
            return "temp";
        case 4:
            return "settings";
        case 5:
            return "diag";
        default:
            return "unknown";
    }
}

const char* HomeAssistantBridge::vfdRunState(const char* lastAction) const {
    if (strcmp(lastAction, "forward") == 0) {
        return "fwd";
    }
    if (strcmp(lastAction, "reverse") == 0) {
        return "rev";
    }
    if (strcmp(lastAction, "stop") == 0) {
        return "stop";
    }

    return "unknown";
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
