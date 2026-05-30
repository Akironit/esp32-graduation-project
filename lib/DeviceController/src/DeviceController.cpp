#include "DeviceController.h"

#include <string.h>

#include "Logger.h"

namespace {
constexpr const char* TAG_ACTION = "ACTION";
}

void DeviceController::begin(
    FujiHeatPump* heatPump,
    VfdController* vfd,
    DisplayUi* display,
    TemperatureSensors* temperatures
) {
    this->heatPump = heatPump;
    this->vfd = vfd;
    this->display = display;
    this->temperatures = temperatures;
}

bool DeviceController::setAcDebug(bool enabled) {
    if (heatPump == nullptr) {
        Logger::warning(TAG_ACTION, "AC debug command rejected: heat pump is not connected");
        return false;
    }

    heatPump->setDebug(enabled);
    Logger::infof(TAG_ACTION, "AC debug %s", enabled ? "enabled" : "disabled");
    return true;
}

bool DeviceController::setAcPrimaryRole(bool primary) {
    if (heatPump == nullptr) {
        Logger::warning(TAG_ACTION, "AC role command rejected: heat pump is not connected");
        return false;
    }

    heatPump->setControllerRole(primary);
    Logger::infof(TAG_ACTION, "AC role set to %s", primary ? "PRIMARY" : "SECONDARY");
    return true;
}

bool DeviceController::setAcPower(bool enabled) {
    if (heatPump == nullptr) {
        Logger::warning(TAG_ACTION, "AC power command rejected: heat pump is not connected");
        return false;
    }

    heatPump->setOnOff(enabled);
    Logger::infof(TAG_ACTION, "AC power %s", enabled ? "ON" : "OFF");
    return true;
}

bool DeviceController::setAcTemperature(uint8_t temperature) {
    if (heatPump == nullptr || temperature < 16 || temperature > 30) {
        Logger::warningf(TAG_ACTION, "AC temperature command rejected: value=%u", temperature);
        return false;
    }

    heatPump->setTemp(temperature);
    Logger::infof(TAG_ACTION, "AC temperature set to %u C", temperature);
    return true;
}

bool DeviceController::setAcMode(uint8_t mode) {
    if (heatPump == nullptr || mode < 1 || mode > 5) {
        Logger::warningf(TAG_ACTION, "AC mode command rejected: value=%u", mode);
        return false;
    }

    heatPump->setMode(mode);
    Logger::infof(TAG_ACTION, "AC mode set to %u", mode);
    return true;
}

bool DeviceController::setAcFanMode(uint8_t fanMode) {
    if (heatPump == nullptr || fanMode > 4) {
        Logger::warningf(TAG_ACTION, "AC fan command rejected: value=%u", fanMode);
        return false;
    }

    heatPump->setFanMode(fanMode);
    Logger::infof(TAG_ACTION, "AC fan mode set to %u", fanMode);
    return true;
}

bool DeviceController::vfdForward(const char* source, bool syncActive) {
    if (vfd == nullptr) {
        Logger::warning(TAG_ACTION, "VFD forward command rejected: VFD is not connected");
        return false;
    }

    Logger::debugf(
        TAG_ACTION,
        "VFD command source=%s action=forward sync=%u token=%lu hasRequestedHz=%u requestedHz=%.2f lastAction=%s",
        source,
        syncActive ? 1 : 0,
        (unsigned long)vfd->getLastToken(),
        vfd->hasRequestedFrequency() ? 1 : 0,
        vfd->hasRequestedFrequency() ? vfd->getRequestedFrequencyHz() : 0.0f,
        vfd->getLastAction()
    );

    if (!vfd->forward()) {
        return false;
    }
    if (strcmp(source, "console") == 0 || strncmp(source, "display", 7) == 0) {
        Logger::info(TAG_ACTION, "VFD user power ON");
    } else {
        Logger::debug(TAG_ACTION, "VFD forward");
    }
    return true;
}

bool DeviceController::vfdReverse(const char* source, bool syncActive) {
    if (vfd == nullptr) {
        Logger::warning(TAG_ACTION, "VFD reverse command rejected: VFD is not connected");
        return false;
    }

    Logger::debugf(
        TAG_ACTION,
        "VFD command source=%s action=reverse sync=%u token=%lu hasRequestedHz=%u requestedHz=%.2f lastAction=%s",
        source,
        syncActive ? 1 : 0,
        (unsigned long)vfd->getLastToken(),
        vfd->hasRequestedFrequency() ? 1 : 0,
        vfd->hasRequestedFrequency() ? vfd->getRequestedFrequencyHz() : 0.0f,
        vfd->getLastAction()
    );

    if (!vfd->reverse()) {
        return false;
    }
    if (strcmp(source, "console") == 0 || strncmp(source, "display", 7) == 0) {
        Logger::info(TAG_ACTION, "VFD user reverse");
    } else {
        Logger::debug(TAG_ACTION, "VFD reverse");
    }
    return true;
}

bool DeviceController::vfdStop(const char* source, bool syncActive) {
    if (vfd == nullptr) {
        Logger::warning(TAG_ACTION, "VFD stop command rejected: VFD is not connected");
        return false;
    }

    Logger::debugf(
        TAG_ACTION,
        "VFD command source=%s action=stop sync=%u token=%lu hasRequestedHz=%u requestedHz=%.2f lastAction=%s",
        source,
        syncActive ? 1 : 0,
        (unsigned long)vfd->getLastToken(),
        vfd->hasRequestedFrequency() ? 1 : 0,
        vfd->hasRequestedFrequency() ? vfd->getRequestedFrequencyHz() : 0.0f,
        vfd->getLastAction()
    );

    if (!vfd->stop()) {
        return false;
    }
    if (strcmp(source, "console") == 0 || strncmp(source, "display", 7) == 0) {
        Logger::info(TAG_ACTION, "VFD user power OFF");
    } else {
        Logger::debug(TAG_ACTION, "VFD stop");
    }
    return true;
}

bool DeviceController::vfdSetFrequency(float hz, const char* source, bool syncActive) {
    if (vfd == nullptr || hz < 20.0f || hz > 50.0f) {
        Logger::warningf(TAG_ACTION, "VFD frequency command rejected: value=%.2f", hz);
        return false;
    }

    Logger::debugf(
        TAG_ACTION,
        "VFD command source=%s action=set_frequency sync=%u token=%lu hz=%.2f hasRequestedHz=%u lastRequestedHz=%.2f lastAction=%s",
        source,
        syncActive ? 1 : 0,
        (unsigned long)vfd->getLastToken(),
        hz,
        vfd->hasRequestedFrequency() ? 1 : 0,
        vfd->hasRequestedFrequency() ? vfd->getRequestedFrequencyHz() : 0.0f,
        vfd->getLastAction()
    );

    if (!vfd->setFrequency(hz)) {
        return false;
    }
    if (strcmp(source, "console") == 0 || strncmp(source, "display", 7) == 0) {
        Logger::infof(TAG_ACTION, "VFD user frequency %.2f Hz", hz);
    } else {
        Logger::debugf(TAG_ACTION, "VFD frequency set to %.2f Hz", hz);
    }
    return true;
}

bool DeviceController::vfdReadRegister(uint16_t address, uint16_t count) {
    if (vfd == nullptr || count == 0) {
        Logger::warningf(TAG_ACTION, "VFD read command rejected: address=0x%04X count=%u", address, count);
        return false;
    }

    if (!vfd->readRegister(address, count)) {
        Logger::debugf(TAG_ACTION, "VFD read rejected: queue full token=%lu", (unsigned long)vfd->getLastToken());
        return false;
    }
    Logger::debugf(TAG_ACTION, "VFD read register address=0x%04X count=%u", address, count);
    return true;
}

bool DeviceController::vfdWriteRegister(uint16_t address, uint16_t value) {
    if (vfd == nullptr) {
        Logger::warning(TAG_ACTION, "VFD write command rejected: VFD is not connected");
        return false;
    }

    if (!vfd->writeRegister(address, value)) {
        Logger::debugf(TAG_ACTION, "VFD write rejected: queue full token=%lu", (unsigned long)vfd->getLastToken());
        return false;
    }
    Logger::debugf(TAG_ACTION, "VFD write register address=0x%04X value=0x%04X", address, value);
    return true;
}

bool DeviceController::displayNextPage() {
    if (display == nullptr) {
        Logger::warning(TAG_ACTION, "Display next page command rejected: display is not connected");
        return false;
    }

    display->nextPage();
    Logger::debug(TAG_ACTION, "Display next page");
    return true;
}

bool DeviceController::displayPreviousPage() {
    if (display == nullptr) {
        Logger::warning(TAG_ACTION, "Display previous page command rejected: display is not connected");
        return false;
    }

    display->previousPage();
    Logger::debug(TAG_ACTION, "Display previous page");
    return true;
}

bool DeviceController::displaySetPage(DisplayUi::Page page) {
    if (display == nullptr) {
        Logger::warning(TAG_ACTION, "Display set page command rejected: display is not connected");
        return false;
    }

    display->setPage(page);
    Logger::debugf(TAG_ACTION, "Display page set to %u", static_cast<unsigned int>(page));
    return true;
}

bool DeviceController::temperatureForceRead() {
    if (temperatures == nullptr) {
        Logger::warning(TAG_ACTION, "Temperature read command rejected: sensors are not connected");
        return false;
    }

    temperatures->forceRead();
    Logger::info(TAG_ACTION, "Temperature force read");
    return true;
}

bool DeviceController::temperatureRescan() {
    if (temperatures == nullptr) {
        Logger::warning(TAG_ACTION, "Temperature scan command rejected: sensors are not connected");
        return false;
    }

    temperatures->rescan();
    Logger::info(TAG_ACTION, "Temperature bus rescan");
    return true;
}

void DeviceController::restart(uint16_t delayMs) {
    Logger::warning(TAG_ACTION, "ESP32 restart requested");

    if (delayMs > 0) {
        delay(delayMs);
    }

    ESP.restart();
}
