#include "DeviceController.h"

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
        return false;
    }

    heatPump->setDebug(enabled);
    return true;
}

bool DeviceController::setAcPrimaryRole(bool primary) {
    if (heatPump == nullptr) {
        return false;
    }

    heatPump->setControllerRole(primary);
    return true;
}

bool DeviceController::setAcPower(bool enabled) {
    if (heatPump == nullptr) {
        return false;
    }

    heatPump->setOnOff(enabled);
    return true;
}

bool DeviceController::setAcTemperature(uint8_t temperature) {
    if (heatPump == nullptr || temperature < 16 || temperature > 30) {
        return false;
    }

    heatPump->setTemp(temperature);
    return true;
}

bool DeviceController::setAcMode(uint8_t mode) {
    if (heatPump == nullptr || mode < 1 || mode > 5) {
        return false;
    }

    heatPump->setMode(mode);
    return true;
}

bool DeviceController::setAcFanMode(uint8_t fanMode) {
    if (heatPump == nullptr || fanMode > 4) {
        return false;
    }

    heatPump->setFanMode(fanMode);
    return true;
}

bool DeviceController::vfdForward() {
    if (vfd == nullptr) {
        return false;
    }

    vfd->forward();
    return true;
}

bool DeviceController::vfdReverse() {
    if (vfd == nullptr) {
        return false;
    }

    vfd->reverse();
    return true;
}

bool DeviceController::vfdStop() {
    if (vfd == nullptr) {
        return false;
    }

    vfd->stop();
    return true;
}

bool DeviceController::vfdSetFrequency(float hz) {
    if (vfd == nullptr || hz < 0.0f) {
        return false;
    }

    vfd->setFrequency(hz);
    return true;
}

bool DeviceController::vfdReadRegister(uint16_t address, uint16_t count) {
    if (vfd == nullptr || count == 0) {
        return false;
    }

    vfd->readRegister(address, count);
    return true;
}

bool DeviceController::vfdWriteRegister(uint16_t address, uint16_t value) {
    if (vfd == nullptr) {
        return false;
    }

    vfd->writeRegister(address, value);
    return true;
}

bool DeviceController::displayNextPage() {
    if (display == nullptr) {
        return false;
    }

    display->nextPage();
    return true;
}

bool DeviceController::displayPreviousPage() {
    if (display == nullptr) {
        return false;
    }

    display->previousPage();
    return true;
}

bool DeviceController::displaySetPage(DisplayUi::Page page) {
    if (display == nullptr) {
        return false;
    }

    display->setPage(page);
    return true;
}

bool DeviceController::temperatureForceRead() {
    if (temperatures == nullptr) {
        return false;
    }

    temperatures->forceRead();
    return true;
}

bool DeviceController::temperatureRescan() {
    if (temperatures == nullptr) {
        return false;
    }

    temperatures->rescan();
    return true;
}

void DeviceController::restart(uint16_t delayMs) {
    if (delayMs > 0) {
        delay(delayMs);
    }

    ESP.restart();
}
