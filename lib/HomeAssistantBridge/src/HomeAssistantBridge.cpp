#include "HomeAssistantBridge.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "Logger.h"

namespace {
constexpr const char* TAG_HA = "HA";

bool parseFloatPayload(const String& payload, float& value) {
    char buffer[32];
    payload.toCharArray(buffer, sizeof(buffer));
    char* end = nullptr;
    value = strtof(buffer, &end);
    while (end != nullptr && *end == ' ') {
        end++;
    }
    return end != buffer && end != nullptr && *end == '\0' && isfinite(value);
}

bool parseIntPayload(const String& payload, int& value) {
    char buffer[24];
    payload.toCharArray(buffer, sizeof(buffer));
    char* end = nullptr;
    long parsed = strtol(buffer, &end, 10);
    while (end != nullptr && *end == ' ') {
        end++;
    }
    if (end == buffer || end == nullptr || *end != '\0') {
        return false;
    }
    value = (int)parsed;
    return true;
}

bool parseOnOffPayload(const String& payload, bool& value) {
    if (payload == "ON" || payload == "on" || payload == "1" || payload == "true") {
        value = true;
        return true;
    }
    if (payload == "OFF" || payload == "off" || payload == "0" || payload == "false") {
        value = false;
        return true;
    }
    return false;
}
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
    DeviceController* controller,
    ClimateAlgorithm* climateAlgorithm
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
    this->climateAlgorithm = climateAlgorithm;

    activeInstance = this;
    mqttClient.setServer(host, port);
    mqttClient.setCallback(&HomeAssistantBridge::handleMqttMessage);
    mqttClient.setSocketTimeout(HA_MQTT_SOCKET_TIMEOUT_SECONDS);
    mqttClient.setKeepAlive(15);

    if (!enabled) {
        Logger::info(TAG_HA, "MQTT bridge disabled");
        return;
    }

    Logger::infof(TAG_HA, "MQTT bridge configured for %s:%u", host, port);
}

void HomeAssistantBridge::setCommandHandlers(
    void* context,
    VfdSyncHandler vfdSyncHandler,
    SettingsChangedHandler settingsChangedHandler,
    RebootHandler rebootHandler
) {
    this->commandContext = context;
    this->vfdSyncHandler = vfdSyncHandler;
    this->settingsChangedHandler = settingsChangedHandler;
    this->rebootHandler = rebootHandler;
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

void HomeAssistantBridge::setEnabled(bool enabled) {
    if (this->enabled == enabled) {
        return;
    }

    this->enabled = enabled;
    reconnectAttempted = false;
    reconnectIntervalMs = RECONNECT_INTERVAL_MS;
    reconnectFailureCount = 0;
    lastReconnectAttemptMs = 0;

    if (!enabled) {
        if (mqttClient.connected()) {
            publishAvailability(false);
            mqttClient.disconnect();
        }
        Logger::info(TAG_HA, "MQTT bridge disabled at runtime");
        return;
    }

    discoveryPublished = false;
    Logger::info(TAG_HA, "MQTT bridge enabled at runtime");
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

void HomeAssistantBridge::forceDiscoveryRepublish() {
    discoveryPublished = false;
    if (!mqttClient.connected()) {
        return;
    }
    publishDiscovery();
    publishState();
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

    publishTopic("state/controller/mode", deviceModeName(state->controllerState.mode), true);
    publishTopic("state/controller/status", activityName(state->controllerState.activity), true);
    publishTopicf("state/controller/warnings", "%u", state->controllerState.warningCount);
    publishTopicf("state/controller/errors", "%u", state->controllerState.errorCount);
    publishTopic("state/controller/has_warnings", state->controllerState.warningCount > 0 ? "ON" : "OFF", true);
    publishTopic("state/controller/has_errors", state->controllerState.errorCount > 0 ? "ON" : "OFF", true);
    const DiagnosticItem* summary = nullptr;
    for (uint8_t i = 0; i < state->diagnostics.itemCount; i++) {
        const DiagnosticItem& item = state->diagnostics.items[i];
        if (!item.active) {
            continue;
        }
        if (summary == nullptr || item.severity == DiagnosticSeverity::Error) {
            summary = &item;
            if (item.severity == DiagnosticSeverity::Error) {
                break;
            }
        }
    }
    publishTopic("state/controller/diagnostics_summary", summary != nullptr ? summary->title : "OK", true);

    publishTopic("state/ac/power", state->ac.powerOn ? "ON" : "OFF", true);
    publishTopic("state/ac/bound", state->ac.bound ? "ON" : "OFF", true);
    publishTopic("state/ac/link", state->ac.communicationError ? "error" : (state->ac.bound ? "linked" : "waiting"), true);
    publishTopicf("state/ac/temp", "%u", state->ac.temperature);
    publishTopicf("state/ac/controller_temp", "%u", state->ac.controllerTemp);
    publishTopic("state/ac/mode", acModeName(state->ac.mode), true);
    publishTopic("state/ac/fan", acFanName(state->ac.fanMode), true);
    publishTopic("state/ac/role", state->ac.primaryController ? "primary" : "secondary", true);
    publishTopicf("state/ac/errors", "%lu", (unsigned long)state->ac.errorCount);
    publishTopicf("state/ac/consecutive_errors", "%u", state->ac.consecutiveErrorCount);

    const auto validTemperature = [](float value) {
        return value > -100.0f && value < 100.0f && value != DEVICE_DISCONNECTED_C;
    };

    if (state->environment.hasIndoorTemp && validTemperature(state->environment.indoorTempC)) {
        publishTopicf("state/temp/indoor", "%.2f", state->environment.indoorTempC);
    } else {
        publishTopic("state/temp/indoor", "unknown", true);
    }
    if (state->environment.hasOutdoorTemp && validTemperature(state->environment.outdoorTempC)) {
        publishTopicf("state/temp/outdoor", "%.2f", state->environment.outdoorTempC);
    } else {
        publishTopic("state/temp/outdoor", "unknown", true);
    }
    publishTopicf("state/temp/target", "%.1f", state->environment.targetIndoorTempC);
    if (state->environment.hasIndoorTemp && validTemperature(state->environment.indoorTempC)) {
        publishTopicf("state/temp/delta", "%.2f", state->environment.indoorTempC - state->environment.targetIndoorTempC);
    } else {
        publishTopic("state/temp/delta", "unknown", true);
    }

    publishTopicf("state/temp/count", "%u", state->temperatures.sensorCount);

    for (uint8_t i = 0; i < state->temperatures.sensorCount && i < TEMP_MAX_SENSORS; i++) {
        char suffix[32];
        snprintf(suffix, sizeof(suffix), "state/temp/%u", i);
        if (state->temperatures.sensors[i].hasTemperature && validTemperature(state->temperatures.values[i])) {
            publishTopicf(suffix, "%.2f", state->temperatures.values[i]);
        } else {
            publishTopic(suffix, "unknown", true);
        }
    }

    publishTopic("state/vfd/last_action", state->vfd.lastAction, true);
    publishTopic("state/vfd/online", state->vfd.online ? "online" : "offline", true);
    publishTopic("state/vfd/link", state->vfd.communicationError ? "error" : ((state->vfd.everOnline || state->vfd.online) ? "linked" : "waiting"), true);
    publishTopic("state/vfd/run", state->vfd.statusWord == 0x0002 ? "rev" : (state->vfd.running ? "fwd" : "stop"), true);
    publishTopic("state/vfd/running", state->vfd.running ? "ON" : "OFF", true);
    const AutoControlSettings settings = climateAlgorithm != nullptr ? climateAlgorithm->getSettings() : AutoControlSettings{};
    const bool manualVentAssist = state->controllerState.mode == DeviceMode::Manual && settings.manualVentCompensationEnabled;
    const uint8_t manualVfdStep = state->settings.manualVfdPower ? min<uint8_t>(state->settings.manualVfdStep, 6) : 0;
    const bool desiredVfdPower = state->controllerState.mode == DeviceMode::Auto
        ? state->ventilation.desiredVfdPower
        : (manualVentAssist ? max<uint8_t>(manualVfdStep, state->ventilation.requestedStepAfterLimit) > 0 : state->settings.manualVfdPower);
    const uint8_t desiredVfdStep = state->controllerState.mode == DeviceMode::Auto
        ? state->ventilation.desiredVfdStep
        : (manualVentAssist ? max<uint8_t>(manualVfdStep, state->ventilation.requestedStepAfterLimit) : manualVfdStep);
    publishTopic("state/vfd/desired_power", desiredVfdPower ? "ON" : "OFF", true);
    publishTopicf("state/vfd/desired_step", "%u", desiredVfdStep);
    publishTopicf("state/vfd/status_word", "%u", state->vfd.statusWord);
    publishTopicf("state/vfd/status_word_hex", "0x%04X", state->vfd.statusWord);
    publishTopicf("state/vfd/requested_frequency", "%.2f", state->vfd.requestedFrequencyHz);
    publishTopicf("state/vfd/actual_frequency", "%.2f", state->vfd.actualFrequencyHz);
    publishTopicf("state/vfd/actual_step", "%u", state->vfd.actualStep);
    publishTopicf("state/vfd/requests", "%lu", (unsigned long)state->vfd.requestCount);
    publishTopicf("state/vfd/ok", "%lu", (unsigned long)state->vfd.okCount);
    publishTopicf("state/vfd/errors", "%lu", (unsigned long)state->vfd.errorCount);
    publishTopicf("state/vfd/consecutive_errors", "%u", state->vfd.consecutiveErrorCount);
    publishTopic("state/vfd/last_error", vfdErrorName(state->vfd.lastErrorCode), true);

    publishTopic("state/vent/bath", state->environment.exhaustVentEnabled ? "ON" : "OFF", true);
    publishTopicf("state/vent/hood_level", "%u", state->environment.kitchenHoodLevel);
    publishTopicf("state/vent/requested_step", "%u", state->ventilation.requestedStepBeforeLimit);
    publishTopicf("state/vent/limited_step", "%u", state->ventilation.requestedStepAfterLimit);
    publishTopicf("state/vent/bath_comp_step", "%u", state->ventilation.bathCompStep);
    publishTopicf("state/vent/hood_comp_step", "%u", state->ventilation.hoodCompStep);
    publishTopicf("state/vent/exhaust_comp_step", "%u", state->ventilation.exhaustCompRequirementStep);
    publishTopic("state/vent/off_delay_active", state->ventilation.compensationOffDelayActive ? "ON" : "OFF", true);
    publishTopicf("state/vent/off_delay_remaining", "%lu", (unsigned long)state->ventilation.compensationOffDelayRemainingSec);
    publishTopic("state/vent/reason", state->ventilation.reason, true);

    publishTopic("state/auto/enabled", settings.autoEnabled ? "ON" : "OFF", true);
    publishTopic("state/auto/dry_run", settings.dryRun ? "ON" : "OFF", true);
    publishTopic("state/auto/diagnostic_verbose", settings.diagnosticVerbose ? "ON" : "OFF", true);
    publishTopic("state/auto/manual_vent_compensation", settings.manualVentCompensationEnabled ? "ON" : "OFF", true);
    publishTopicf("state/auto/target_temp", "%.1f", settings.targetTempC);
    publishTopicf("state/auto/cooling_delta", "%.2f", settings.coolingStartDeltaC);
    publishTopicf("state/auto/heating_delta", "%.2f", settings.heatingStartDeltaC);

    publishTopic("state/system/vfd_polling_enabled", state->settings.vfdPollingEnabled ? "ON" : "OFF", true);
    publishTopic("state/system/auto_save_enabled", state->settings.autoSaveEnabled ? "ON" : "OFF", true);
    publishTopic("state/system/ha_connected", isConnected() ? "ON" : "OFF", true);
    publishTopic("state/system/io_expander_ready", state->input.ioExpanderReady && !state->input.ioExpanderCommunicationError ? "ON" : "OFF", true);
    publishTopic(
        "state/system/io_expander",
        state->input.ioExpanderCommunicationError ? "error" : (state->input.ioExpanderReady ? "online" : "offline"),
        true
    );
    publishTopicf("state/system/io_expander_errors", "%lu", (unsigned long)state->input.ioExpanderErrorCount);
    publishTopicf("state/system/io_expander_consecutive_errors", "%u", state->input.ioExpanderConsecutiveErrorCount);
    publishTopicf("state/system/ha_publish_count", "%lu", (unsigned long)publishCount);
    publishTopicf("state/system/ha_command_count", "%lu", (unsigned long)commandCount);
    publishTopicf("state/system/ha_reconnect_count", "%lu", (unsigned long)reconnectCount);

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
    publishBinarySensorDiscovery("wifi", "Wi-Fi", "climate_controller_wifi", "state/wifi", "connectivity", "online", "offline");

    publishSelectDiscovery("device_mode", "Device mode", "climate_controller_device_mode", "state/controller/mode", "cmd/device/mode", "[\"Auto\",\"Manual\",\"Off\"]");
    publishSensorDiscovery("controller_status", "Controller status", "climate_controller_status", "state/controller/status");
    publishSensorDiscovery("warning_count", "Warning count", "climate_controller_warning_count", "state/controller/warnings");
    publishSensorDiscovery("error_count", "Error count", "climate_controller_error_count", "state/controller/errors");
    publishBinarySensorDiscovery("has_warnings", "Has warnings", "climate_controller_has_warnings", "state/controller/has_warnings", "problem");
    publishBinarySensorDiscovery("has_errors", "Has errors", "climate_controller_has_errors", "state/controller/has_errors", "problem");
    publishSensorDiscovery("diagnostics_summary", "Diagnostics summary", "climate_controller_diagnostics_summary", "state/controller/diagnostics_summary");

    publishSwitchDiscovery("ac_power", "AC power", "climate_controller_ac_power", "state/ac/power", "cmd/ac/power");
    publishBinarySensorDiscovery("ac_bound", "AC bound", "climate_controller_ac_bound", "state/ac/bound", "connectivity");
    publishSensorDiscovery("ac_link", "AC link", "climate_controller_ac_link", "state/ac/link");
    publishNumberDiscovery("ac_temp", "AC temperature", "climate_controller_ac_temperature", "state/ac/temp", "cmd/ac/temp", 16, 30, 1, "°C");
    publishSensorDiscovery("ac_controller_temperature", "AC controller temperature", "climate_controller_ac_controller_temperature", "state/ac/controller_temp", "temperature", "°C", "measurement");
    publishSelectDiscovery("ac_fan", "AC fan", "climate_controller_ac_fan", "state/ac/fan", "cmd/ac/fan", "[\"auto\",\"low\",\"medium\",\"high\",\"max\"]");
    publishSelectDiscovery("ac_mode", "AC mode", "climate_controller_ac_mode", "state/ac/mode", "cmd/ac/mode", "[\"unknown\",\"fan\",\"dry\",\"cool\",\"heat\",\"auto\"]");
    publishSensorDiscovery("ac_role", "AC role", "climate_controller_ac_role", "state/ac/role");
    publishSensorDiscovery("ac_errors", "AC errors", "climate_controller_ac_errors", "state/ac/errors", nullptr, nullptr, "total_increasing");
    publishSensorDiscovery("ac_consecutive_errors", "AC consecutive errors", "climate_controller_ac_consecutive_errors", "state/ac/consecutive_errors");

    publishSensorDiscovery("indoor_temperature", "Indoor temperature", "climate_controller_indoor_temperature", "state/temp/indoor", "temperature", "°C", "measurement");
    publishSensorDiscovery("outdoor_temperature", "Outdoor temperature", "climate_controller_outdoor_temperature", "state/temp/outdoor", "temperature", "°C", "measurement");
    publishSensorDiscovery("auto_target_temperature", "Auto target temperature", "climate_controller_auto_target_temperature", "state/temp/target", "temperature", "°C", "measurement");
    publishSensorDiscovery("indoor_temperature_delta", "Indoor temperature delta", "climate_controller_indoor_temperature_delta", "state/temp/delta", "temperature", "°C", "measurement");

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
        publishSensorDiscovery(objectId, name, entityObjectId, suffix, "temperature", "°C", "measurement");
    }

    publishSensorDiscovery("vfd_last_action", "VFD last action", "climate_controller_vfd_last_action", "state/vfd/last_action");
    publishBinarySensorDiscovery("vfd_online", "VFD online", "climate_controller_vfd_online", "state/vfd/online", "connectivity", "online", "offline");
    publishSensorDiscovery("vfd_link", "VFD link", "climate_controller_vfd_link", "state/vfd/link");
    publishBinarySensorDiscovery("vfd_running", "VFD running", "climate_controller_vfd_running", "state/vfd/running", "running");
    publishSensorDiscovery("vfd_status_word", "VFD status word", "climate_controller_vfd_status_word", "state/vfd/status_word");
    publishSensorDiscovery("vfd_status_word_hex", "VFD status word hex", "climate_controller_vfd_status_word_hex", "state/vfd/status_word_hex");
    publishSensorDiscovery("vfd_requested_frequency", "VFD requested frequency", "climate_controller_vfd_requested_frequency", "state/vfd/requested_frequency", nullptr, "Hz", "measurement");
    publishSensorDiscovery("vfd_actual_frequency", "VFD actual frequency", "climate_controller_vfd_actual_frequency", "state/vfd/actual_frequency", nullptr, "Hz", "measurement");
    publishSensorDiscovery("vfd_actual_step", "VFD actual step", "climate_controller_vfd_actual_step", "state/vfd/actual_step");
    publishSensorDiscovery("vfd_requests", "VFD requests", "climate_controller_vfd_requests", "state/vfd/requests", nullptr, nullptr, "total_increasing");
    publishSensorDiscovery("vfd_ok", "VFD ok responses", "climate_controller_vfd_ok", "state/vfd/ok", nullptr, nullptr, "total_increasing");
    publishSensorDiscovery("vfd_errors", "VFD errors", "climate_controller_vfd_errors", "state/vfd/errors", nullptr, nullptr, "total_increasing");
    publishSensorDiscovery("vfd_consecutive_errors", "VFD consecutive errors", "climate_controller_vfd_consecutive_errors", "state/vfd/consecutive_errors");
    publishSensorDiscovery("vfd_last_error", "VFD last error", "climate_controller_vfd_last_error", "state/vfd/last_error");
    publishSwitchDiscovery("vfd_power", "VFD desired power", "climate_controller_vfd_power", "state/vfd/desired_power", "cmd/vfd/power");
    publishNumberDiscovery("vfd_step", "VFD step command", "climate_controller_vfd_step", "state/vfd/desired_step", "cmd/vfd/step", 0, 6, 1);
    publishNumberDiscovery("vfd_frequency", "VFD frequency command", "climate_controller_vfd_frequency", "state/vfd/requested_frequency", "cmd/vfd/hz", 20, 50, 1, "Hz");
    publishSelectDiscovery("vfd_run", "VFD run command", "climate_controller_vfd_run", "state/vfd/run", "cmd/vfd/run", "[\"unknown\",\"fwd\",\"stop\"]");

    publishBinarySensorDiscovery("bath_exhaust", "Bath exhaust", "climate_controller_bath_exhaust", "state/vent/bath", "running");
    publishSensorDiscovery("kitchen_hood_level", "Kitchen hood level", "climate_controller_kitchen_hood_level", "state/vent/hood_level");
    publishSensorDiscovery("vent_requested_step", "Vent requested step", "climate_controller_vent_requested_step", "state/vent/requested_step");
    publishSensorDiscovery("vent_limited_step", "Vent limited step", "climate_controller_vent_limited_step", "state/vent/limited_step");
    publishSensorDiscovery("vent_bath_comp_step", "Vent bath comp step", "climate_controller_vent_bath_comp_step", "state/vent/bath_comp_step");
    publishSensorDiscovery("vent_hood_comp_step", "Vent hood comp step", "climate_controller_vent_hood_comp_step", "state/vent/hood_comp_step");
    publishSensorDiscovery("vent_exhaust_comp_step", "Vent exhaust comp step", "climate_controller_vent_exhaust_comp_step", "state/vent/exhaust_comp_step");
    publishBinarySensorDiscovery("vent_off_delay_active", "Vent off delay active", "climate_controller_vent_off_delay_active", "state/vent/off_delay_active", "running");
    publishSensorDiscovery("vent_off_delay_remaining", "Vent off delay remaining", "climate_controller_vent_off_delay_remaining", "state/vent/off_delay_remaining", "duration", "s", "measurement");
    publishSensorDiscovery("vent_reason", "Vent reason", "climate_controller_vent_reason", "state/vent/reason");

    publishSwitchDiscovery("auto_enabled", "Auto enabled", "climate_controller_auto_enabled", "state/auto/enabled", "cmd/auto/enabled");
    publishSwitchDiscovery("auto_dry_run", "Auto dry run", "climate_controller_auto_dry_run", "state/auto/dry_run", "cmd/auto/dry_run");
    publishSwitchDiscovery("auto_diagnostic_verbose", "Auto diagnostic verbose", "climate_controller_auto_diagnostic_verbose", "state/auto/diagnostic_verbose", "cmd/auto/diagnostic_verbose");
    publishSwitchDiscovery("manual_vent_compensation", "Manual vent compensation", "climate_controller_manual_vent_compensation", "state/auto/manual_vent_compensation", "cmd/auto/manual_vent_compensation");
    publishNumberDiscovery("auto_target_temp", "Auto target temp", "climate_controller_auto_target_temp", "state/auto/target_temp", "cmd/auto/target_temp", "16", "30", "0.5", "°C");
    publishNumberDiscovery("cooling_start_delta", "Cooling start delta", "climate_controller_cooling_start_delta", "state/auto/cooling_delta", "cmd/auto/cooling_delta", "0.1", "5", "0.1", "°C");
    publishNumberDiscovery("heating_start_delta", "Heating start delta", "climate_controller_heating_start_delta", "state/auto/heating_delta", "cmd/auto/heating_delta", "0.1", "5", "0.1", "°C");

    publishSwitchDiscovery("vfd_polling_enabled", "VFD polling enabled", "climate_controller_vfd_polling_enabled", "state/system/vfd_polling_enabled", "cmd/system/vfd_polling_enabled");
    publishSwitchDiscovery("auto_save_enabled", "Auto save enabled", "climate_controller_auto_save_enabled", "state/system/auto_save_enabled", "cmd/system/auto_save_enabled");
    publishBinarySensorDiscovery("ha_connected", "Home Assistant connected", "climate_controller_ha_connected", "state/system/ha_connected", "connectivity");
    publishBinarySensorDiscovery("io_expander_ready", "IO expander ready", "climate_controller_io_expander_ready", "state/system/io_expander_ready", "connectivity");
    publishSensorDiscovery("io_expander_state", "IO expander state", "climate_controller_io_expander_state", "state/system/io_expander");
    publishSensorDiscovery("io_expander_errors", "IO expander errors", "climate_controller_io_expander_errors", "state/system/io_expander_errors", nullptr, nullptr, "total_increasing");
    publishSensorDiscovery("io_expander_consecutive_errors", "IO expander consecutive errors", "climate_controller_io_expander_consecutive_errors", "state/system/io_expander_consecutive_errors");
    publishSensorDiscovery("ha_publish_count", "HA publish count", "climate_controller_ha_publish_count", "state/system/ha_publish_count", nullptr, nullptr, "total_increasing");
    publishSensorDiscovery("ha_command_count", "HA command count", "climate_controller_ha_command_count", "state/system/ha_command_count", nullptr, nullptr, "total_increasing");
    publishSensorDiscovery("ha_reconnect_count", "HA reconnect count", "climate_controller_ha_reconnect_count", "state/system/ha_reconnect_count", nullptr, nullptr, "total_increasing");
    publishButtonDiscovery("reboot_device", "Reboot device", "climate_controller_reboot_device", "cmd/system/reboot", "restart");

    publishSelectDiscovery(
        "display_page",
        "Display page",
        "climate_controller_display_page",
        "state/display/page",
        "cmd/display/page",
        "[\"overview\",\"ac\",\"vent\",\"temp\",\"settings\",\"system\",\"diag\",\"next\",\"prev\"]"
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
    char minText[16];
    char maxText[16];
    char stepText[16];
    snprintf(minText, sizeof(minText), "%d", min);
    snprintf(maxText, sizeof(maxText), "%d", max);
    snprintf(stepText, sizeof(stepText), "%d", step);
    publishNumberDiscovery(objectId, name, entityObjectId, stateSuffix, commandSuffix, minText, maxText, stepText, unit);
}

void HomeAssistantBridge::publishNumberDiscovery(
    const char* objectId,
    const char* name,
    const char* entityObjectId,
    const char* stateSuffix,
    const char* commandSuffix,
    const char* min,
    const char* max,
    const char* step,
    const char* unit
) {
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/number/%s_%s/config", HA_DISCOVERY_PREFIX, baseTopic, objectId);

    char payload[512];
    snprintf(
        payload,
        sizeof(payload),
        "{\"name\":\"%s\",\"object_id\":\"%s\",\"unique_id\":\"%s_%s\",\"has_entity_name\":true,\"state_topic\":\"%s/%s\",\"command_topic\":\"%s/%s\",\"availability_topic\":\"%s/status\",\"min\":%s,\"max\":%s,\"step\":%s,\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\",\"manufacturer\":\"%s\",\"model\":\"%s\"}%s%s%s}",
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

void HomeAssistantBridge::publishButtonDiscovery(
    const char* objectId,
    const char* name,
    const char* entityObjectId,
    const char* commandSuffix,
    const char* deviceClass
) {
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/button/%s_%s/config", HA_DISCOVERY_PREFIX, baseTopic, objectId);

    char payload[512];
    snprintf(
        payload,
        sizeof(payload),
        "{\"name\":\"%s\",\"object_id\":\"%s\",\"unique_id\":\"%s_%s\",\"has_entity_name\":true,\"command_topic\":\"%s/%s\",\"availability_topic\":\"%s/status\",\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\",\"manufacturer\":\"%s\",\"model\":\"%s\"}%s%s%s}",
        name,
        entityObjectId,
        baseTopic,
        objectId,
        baseTopic,
        commandSuffix,
        baseTopic,
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
    mqttClient.subscribe(commandTopic("device/mode").c_str());
    mqttClient.subscribe(commandTopic("ac/power").c_str());
    mqttClient.subscribe(commandTopic("ac/temp").c_str());
    mqttClient.subscribe(commandTopic("ac/mode").c_str());
    mqttClient.subscribe(commandTopic("ac/fan").c_str());
    mqttClient.subscribe(commandTopic("ac/debug").c_str());
    mqttClient.subscribe(commandTopic("display/page").c_str());
    mqttClient.subscribe(commandTopic("vfd/power").c_str());
    mqttClient.subscribe(commandTopic("vfd/step").c_str());
    mqttClient.subscribe(commandTopic("vfd/run").c_str());
    mqttClient.subscribe(commandTopic("vfd/hz").c_str());
    mqttClient.subscribe(commandTopic("auto/enabled").c_str());
    mqttClient.subscribe(commandTopic("auto/dry_run").c_str());
    mqttClient.subscribe(commandTopic("auto/diagnostic_verbose").c_str());
    mqttClient.subscribe(commandTopic("auto/manual_vent_compensation").c_str());
    mqttClient.subscribe(commandTopic("auto/target_temp").c_str());
    mqttClient.subscribe(commandTopic("auto/cooling_delta").c_str());
    mqttClient.subscribe(commandTopic("auto/heating_delta").c_str());
    mqttClient.subscribe(commandTopic("system/vfd_polling_enabled").c_str());
    mqttClient.subscribe(commandTopic("system/auto_save_enabled").c_str());
    mqttClient.subscribe(commandTopic("system/reboot").c_str());

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

    if (suffix == "device/mode") {
        if (state == nullptr) {
            return;
        }
        if (payload.equalsIgnoreCase("Auto")) {
            state->controllerState.mode = DeviceMode::Auto;
        } else if (payload.equalsIgnoreCase("Manual")) {
            state->controllerState.mode = DeviceMode::Manual;
        } else if (payload.equalsIgnoreCase("Off") || payload.equalsIgnoreCase("Disabled")) {
            state->controllerState.mode = DeviceMode::Disabled;
        } else if (payload.equalsIgnoreCase("Safe")) {
            Logger::warning(TAG_HA, "MQTT command rejected: Safe mode is read-only from Home Assistant");
            return;
        } else {
            Logger::warningf(TAG_HA, "MQTT command rejected: invalid device mode '%s'", payload.c_str());
            return;
        }
        notifySettingsChanged();
    } else if (suffix == "ac/power") {
        bool enabled = false;
        if (!parseOnOffPayload(payload, enabled)) {
            Logger::warningf(TAG_HA, "MQTT command rejected: invalid AC power '%s'", payload.c_str());
            return;
        }
        controller->setAcPower(enabled);
    } else if (suffix == "ac/temp") {
        int value = 0;
        if (!parseIntPayload(payload, value) || value < 16 || value > 30) {
            Logger::warningf(TAG_HA, "MQTT command rejected: invalid AC temperature '%s'", payload.c_str());
            return;
        }
        controller->setAcTemperature((uint8_t)value);
    } else if (suffix == "ac/mode") {
        const uint8_t mode = acModeValue(payload);
        if (mode == 0 || mode > 5) {
            Logger::warningf(TAG_HA, "MQTT command rejected: invalid AC mode '%s'", payload.c_str());
            return;
        }
        controller->setAcMode(mode);
    } else if (suffix == "ac/fan") {
        const uint8_t fan = acFanValue(payload);
        if (fan > 4) {
            Logger::warningf(TAG_HA, "MQTT command rejected: invalid AC fan '%s'", payload.c_str());
            return;
        }
        controller->setAcFanMode(fan);
    } else if (suffix == "ac/debug") {
        bool enabled = false;
        if (!parseOnOffPayload(payload, enabled)) {
            Logger::warningf(TAG_HA, "MQTT command rejected: invalid AC debug '%s'", payload.c_str());
            return;
        }
        controller->setAcDebug(enabled);
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
        } else if (payload == "settings" || payload == "auto") {
            controller->displaySetPage(DisplayUi::Page::Settings);
        } else if (payload == "system" || payload == "network") {
            controller->displaySetPage(DisplayUi::Page::SystemSettings);
        } else if (payload == "diag" || payload == "diagnostics") {
            controller->displaySetPage(DisplayUi::Page::Diagnostics);
        }
    } else if (suffix == "vfd/power") {
        if (state == nullptr) {
            return;
        }
        bool enabled = false;
        if (!parseOnOffPayload(payload, enabled)) {
            Logger::warningf(TAG_HA, "MQTT command rejected: invalid VFD power '%s'", payload.c_str());
            return;
        }
        state->settings.manualVfdPower = enabled;
        if (enabled && state->settings.manualVfdStep == 0) {
            state->settings.manualVfdStep = 1;
        }
        notifySettingsChanged();
        syncVfd("ha vfd power");
    } else if (suffix == "vfd/step") {
        if (state == nullptr) {
            return;
        }
        int value = 0;
        if (!parseIntPayload(payload, value) || value < 0 || value > 6) {
            Logger::warningf(TAG_HA, "MQTT command rejected: invalid VFD step '%s'", payload.c_str());
            return;
        }
        state->settings.manualVfdStep = (uint8_t)value;
        state->settings.manualVfdPower = value > 0;
        notifySettingsChanged();
        syncVfd("ha vfd step");
    } else if (suffix == "vfd/run") {
        if (state == nullptr) {
            return;
        }
        if (payload == "fwd" || payload == "forward") {
            state->settings.manualVfdPower = true;
            if (state->settings.manualVfdStep == 0) {
                state->settings.manualVfdStep = 1;
            }
        } else if (payload == "stop") {
            state->settings.manualVfdPower = false;
        } else if (payload == "rev" || payload == "reverse") {
            Logger::warning(TAG_HA, "MQTT VFD reverse command is not exposed through the effective target layer");
            return;
        } else {
            Logger::warningf(TAG_HA, "MQTT command rejected: invalid VFD run '%s'", payload.c_str());
            return;
        }
        notifySettingsChanged();
        syncVfd("ha vfd run");
    } else if (suffix == "vfd/hz") {
        if (state == nullptr) {
            return;
        }
        float hz = 0.0f;
        if (!parseFloatPayload(payload, hz) || hz < 0.0f || hz > 50.0f) {
            Logger::warningf(TAG_HA, "MQTT command rejected: invalid VFD frequency '%s'", payload.c_str());
            return;
        }
        const uint8_t step = frequencyToStep(hz);
        state->settings.manualVfdStep = step;
        state->settings.manualVfdPower = step > 0;
        notifySettingsChanged();
        syncVfd("ha vfd hz");
    } else if (suffix.startsWith("auto/")) {
        if (climateAlgorithm == nullptr) {
            Logger::warning(TAG_HA, "MQTT auto command rejected: ClimateAlgorithm is not connected");
            return;
        }
        AutoControlSettings settings = climateAlgorithm->getSettings();
        bool changed = true;
        if (suffix == "auto/enabled") {
            bool value = false;
            if (!parseOnOffPayload(payload, value)) {
                Logger::warningf(TAG_HA, "MQTT command rejected: invalid auto enabled '%s'", payload.c_str());
                return;
            }
            settings.autoEnabled = value;
        } else if (suffix == "auto/dry_run") {
            bool value = false;
            if (!parseOnOffPayload(payload, value)) {
                Logger::warningf(TAG_HA, "MQTT command rejected: invalid dry run '%s'", payload.c_str());
                return;
            }
            settings.dryRun = value;
        } else if (suffix == "auto/diagnostic_verbose") {
            bool value = false;
            if (!parseOnOffPayload(payload, value)) {
                Logger::warningf(TAG_HA, "MQTT command rejected: invalid diagnostic verbose '%s'", payload.c_str());
                return;
            }
            settings.diagnosticVerbose = value;
        } else if (suffix == "auto/manual_vent_compensation") {
            bool value = false;
            if (!parseOnOffPayload(payload, value)) {
                Logger::warningf(TAG_HA, "MQTT command rejected: invalid manual vent compensation '%s'", payload.c_str());
                return;
            }
            settings.manualVentCompensationEnabled = value;
        } else if (suffix == "auto/target_temp") {
            float value = 0.0f;
            if (!parseFloatPayload(payload, value) || value < 16.0f || value > 30.0f) {
                Logger::warningf(TAG_HA, "MQTT command rejected: invalid target temperature '%s'", payload.c_str());
                return;
            }
            settings.targetTempC = value;
            if (state != nullptr) {
                state->settings.targetIndoorTempC = value;
                state->environment.targetIndoorTempC = value;
            }
        } else if (suffix == "auto/cooling_delta") {
            float value = 0.0f;
            if (!parseFloatPayload(payload, value) || value < 0.1f || value > 5.0f) {
                Logger::warningf(TAG_HA, "MQTT command rejected: invalid cooling delta '%s'", payload.c_str());
                return;
            }
            settings.coolingStartDeltaC = value;
        } else if (suffix == "auto/heating_delta") {
            float value = 0.0f;
            if (!parseFloatPayload(payload, value) || value < 0.1f || value > 5.0f) {
                Logger::warningf(TAG_HA, "MQTT command rejected: invalid heating delta '%s'", payload.c_str());
                return;
            }
            settings.heatingStartDeltaC = value;
        } else {
            changed = false;
        }
        if (changed) {
            updateAutoSettings(settings);
        }
    } else if (suffix == "system/vfd_polling_enabled") {
        if (state == nullptr) {
            return;
        }
        bool enabled = false;
        if (!parseOnOffPayload(payload, enabled)) {
            Logger::warningf(TAG_HA, "MQTT command rejected: invalid VFD polling '%s'", payload.c_str());
            return;
        }
        state->settings.vfdPollingEnabled = enabled;
        notifySettingsChanged();
    } else if (suffix == "system/auto_save_enabled") {
        if (state == nullptr) {
            return;
        }
        bool enabled = false;
        if (!parseOnOffPayload(payload, enabled)) {
            Logger::warningf(TAG_HA, "MQTT command rejected: invalid auto save '%s'", payload.c_str());
            return;
        }
        state->settings.autoSaveEnabled = enabled;
        notifySettingsChanged();
    } else if (suffix == "system/reboot") {
        if (rebootHandler != nullptr) {
            rebootHandler(commandContext);
        }
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

const char* HomeAssistantBridge::deviceModeName(DeviceMode mode) const {
    switch (mode) {
        case DeviceMode::Auto:
            return "Auto";
        case DeviceMode::Manual:
            return "Manual";
        case DeviceMode::Safe:
            return "Safe";
        case DeviceMode::Disabled:
            return "Off";
        default:
            return "Unknown";
    }
}

const char* HomeAssistantBridge::activityName(ControllerActivity activity) const {
    switch (activity) {
        case ControllerActivity::Start:
            return "Start";
        case ControllerActivity::Normal:
            return "Normal";
        case ControllerActivity::VentCool:
            return "VentCool";
        case ControllerActivity::AcCool:
            return "AcCool";
        case ControllerActivity::Heat:
            return "Heat";
        case ControllerActivity::Vent:
            return "Vent";
        case ControllerActivity::Error:
            return "Error";
        case ControllerActivity::Hold:
            return "Hold";
        case ControllerActivity::Idle:
            return "Idle";
        default:
            return "Unknown";
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

const char* HomeAssistantBridge::vfdErrorName(uint8_t code) const {
    switch (code) {
        case 0:
            return "none";
        case 0xE0:
            return "timeout";
        case 0xE1:
            return "short_frame";
        case 0xE2:
            return "crc_error";
        case 0xE3:
            return "exception";
        case 0xE4:
            return "wrong_slave";
        case 0xE5:
            return "packet_length";
        case 0xE6:
            return "busy";
        case 0xE8:
            return "queue_full";
        default:
            return "unknown";
    }
}

uint8_t HomeAssistantBridge::frequencyToStep(float hz) const {
    if (hz < 10.0f) {
        return 0;
    }
    if (hz < 23.0f) {
        return 1;
    }
    if (hz < 29.0f) {
        return 2;
    }
    if (hz < 35.0f) {
        return 3;
    }
    if (hz < 41.0f) {
        return 4;
    }
    if (hz < 47.0f) {
        return 5;
    }
    return 6;
}

bool HomeAssistantBridge::syncVfd(const char* reason) {
    if (vfdSyncHandler == nullptr) {
        Logger::warning(TAG_HA, "MQTT VFD command rejected: App VFD sync handler is not connected");
        return false;
    }
    return vfdSyncHandler(commandContext, reason);
}

void HomeAssistantBridge::notifySettingsChanged() {
    if (settingsChangedHandler != nullptr) {
        settingsChangedHandler(commandContext);
    }
}

bool HomeAssistantBridge::updateAutoSettings(const AutoControlSettings& settings) {
    if (climateAlgorithm == nullptr) {
        return false;
    }
    climateAlgorithm->setSettings(settings);
    if (state != nullptr && state->controllerState.mode == DeviceMode::Manual) {
        syncVfd("ha auto settings");
    }
    return true;
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
