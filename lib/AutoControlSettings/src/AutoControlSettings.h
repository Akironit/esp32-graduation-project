#pragma once

#include <Arduino.h>

struct AutoControlSettings {
    bool autoEnabled = true;
    bool dryRun = true;
    float targetTempC = 22.5f;
    float coolingStartDeltaC = 0.7f;
    float heatingStartDeltaC = 0.7f;

    bool allowVentCooling = true;
    bool allowAcCooling = true;
    bool allowAcHeating = true;
    float outdoorCoolingMinDeltaC = 10.0f;
    float outdoorCoolingMaxTempC = 24.0f;

    bool autoVentAlwaysOn = true;
    uint8_t autoVentDefaultStep = 2;
    uint8_t autoVentMinStep = 1;
    uint8_t autoVentCoolingStep = 4;
    uint8_t autoVentMaxStep = 6;
    uint8_t bathExhaustCompStep = 1;
    uint8_t hoodCompStep1 = 1;
    uint8_t hoodCompStep2 = 2;
    uint8_t hoodCompStep3 = 3;
    bool additiveVentCompensation = false;
    float coldOutdoorTempLimitC = -5.0f;
    uint8_t coldOutdoorMaxVentStep = 2;

    bool keepAcFanOnInAuto = true;
    bool keepAcFanOnWithVent = true;
    uint8_t acFanOnlyMode = 1;
    uint8_t acFanMinSpeed = 1;
    uint8_t acFanNormalSpeed = 2;
    uint8_t acFanBoostSpeed = 3;
    uint8_t acFanMaxSpeed = 4;
    bool acFanAutoAllowed = true;
    bool acDynamicControlEnabled = true;
    float acCoolingFullPowerDeltaC = 3.0f;
    uint8_t acCoolingMinTempOffsetC = 1;
    uint8_t acCoolingMaxTempOffsetC = 5;
    uint8_t acCoolingMinSetpointC = 18;
    uint8_t acCoolingMinFanSpeed = 2;
    uint8_t acCoolingMaxFanSpeed = 4;
    float acHeatingFullPowerDeltaC = 3.0f;
    uint8_t acHeatingMinTempOffsetC = 1;
    uint8_t acHeatingMaxTempOffsetC = 4;
    uint8_t acHeatingMaxSetpointC = 30;
    uint8_t acHeatingMinFanSpeed = 2;
    uint8_t acHeatingMaxFanSpeed = 4;

    unsigned long decisionIntervalMs = 5000;
    unsigned long minStateHoldMs = 5000;
    uint32_t ventCoolingCheckIntervalSec = 600UL;
    float ventCoolingMinDropC = 0.3f;
    bool ventCoolingStepUpOnFail = true;
    bool ventCoolingFallbackToAc = true;

    bool safeOnIndoorSensorMissing = true;
    bool safeOnCriticalEquipmentError = true;
    bool diagnosticVerbose = false;

    uint32_t ventCompensationUpdateIntervalSec = 1;
    uint32_t ventCompensationOffDelaySec = 600;
    bool ventCompensationImmediateUp = true;
    bool ventCompensationImmediateDown = false;
};
