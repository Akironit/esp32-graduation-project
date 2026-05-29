#include "DisplayUi.h"

#include <math.h>

#include "Logger.h"

namespace {
constexpr const char* TAG_DISPLAY = "DISPLAY";
constexpr const char* TAG_UI = "UI";

constexpr uint16_t COLOR_BG = TFT_BLACK;
constexpr uint16_t COLOR_PANEL = 0x1082;
constexpr uint16_t COLOR_TEXT = TFT_WHITE;
constexpr uint16_t COLOR_MUTED = 0x8410;
constexpr uint16_t COLOR_ACCENT = TFT_CYAN;
constexpr uint16_t COLOR_TITLE = TFT_YELLOW;
constexpr uint16_t COLOR_OK = TFT_GREEN;
constexpr uint16_t COLOR_WARN = TFT_ORANGE;
constexpr uint16_t COLOR_DANGER = TFT_RED;
constexpr uint16_t COLOR_CARD = 0x0861;
constexpr uint16_t COLOR_LINE = 0x39E7;
constexpr int16_t HEADER_H = 20;
constexpr int16_t FOOTER_Y = 216;
constexpr int16_t FOOTER_H = 24;

// UI font aliases used on the overview page:
// F1  = built-in GLCD font 1
// F2  = built-in TFT_eSPI font 2
// F4  = built-in TFT_eSPI font 4
// FF1 = FreeMono9pt7b
// FF5 = FreeMonoBold9pt7b
// FF21 = FreeSansBold9pt7b

bool isAcTemperatureMode(uint8_t mode) {
    return mode >= 1 && mode <= 5;
}

uint8_t normalizeAcTemperature(uint8_t temperature) {
    return constrain(temperature, (uint8_t)16, (uint8_t)30);
}

uint8_t getAcModeTemperature(const UserSettingsSnapshot& settings, uint8_t mode) {
    if (!isAcTemperatureMode(mode)) {
        return normalizeAcTemperature(settings.manualAcTemperature);
    }

    return normalizeAcTemperature(settings.manualAcModeTemperatures[mode]);
}

void setAcModeTemperature(UserSettingsSnapshot& settings, uint8_t mode, uint8_t temperature) {
    if (!isAcTemperatureMode(mode)) {
        return;
    }

    settings.manualAcModeTemperatures[mode] = normalizeAcTemperature(temperature);
}

enum class AutoSettingType : uint8_t {
    Bool,
    UInt,
    Float,
    Milliseconds
};

enum class AutoSettingId : uint8_t {
    AutoEnabled,
    DryRun,
    DiagnosticVerbose,
    TargetTemp,
    CoolingDelta,
    HeatingDelta,
    AllowVentCooling,
    AllowAcCooling,
    AllowAcHeating,
    OutdoorMinDelta,
    OutdoorMaxTemp,
    VentAlwaysOn,
    VentDefaultStep,
    VentMinStep,
    VentCoolingStep,
    VentMaxStep,
    BathCompStep,
    HoodComp1,
    HoodComp2,
    HoodComp3,
    AdditiveVent,
    ColdLimit,
    ColdMaxStep,
    AcFanInAuto,
    AcFanWithVent,
    AcFanMode,
    AcFanMin,
    AcFanNormal,
    AcFanBoost,
    AcFanMax,
    AcFanAuto,
    AcDynamic,
    AcCoolFullDelta,
    AcCoolMinOffset,
    AcCoolMaxOffset,
    AcCoolMinTemp,
    AcCoolFanMin,
    AcCoolFanMax,
    AcHeatFullDelta,
    AcHeatMinOffset,
    AcHeatMaxOffset,
    AcHeatMaxTemp,
    AcHeatFanMin,
    AcHeatFanMax,
    DecisionInterval,
    MinStateHold,
    VentCoolCheck,
    VentCoolMinDrop,
    VentCoolStepUp,
    VentCoolFallback,
    SafeNoIndoor,
    SafeEquipment,
    VentCompInterval,
    VentCompOffDelay,
    VentCompUp,
    VentCompDown
};

struct AutoSettingDescriptor {
    AutoSettingId id;
    const char* group;
    const char* label;
    AutoSettingType type;
    float minValue;
    float maxValue;
    float step;
    float fastStep;
    uint8_t decimals;
    const char* unit;
};

constexpr AutoSettingDescriptor AUTO_SETTINGS[] = {
    {AutoSettingId::AutoEnabled, "MAIN", "autoEnabled", AutoSettingType::Bool, 0, 1, 1, 1, 0, ""},
    {AutoSettingId::DryRun, "MAIN", "dryRun", AutoSettingType::Bool, 0, 1, 1, 1, 0, ""},
    {AutoSettingId::DiagnosticVerbose, "MAIN", "diagnosticVerbose", AutoSettingType::Bool, 0, 1, 1, 1, 0, ""},
    {AutoSettingId::TargetTemp, "MAIN", "targetTempC", AutoSettingType::Float, 16.0f, 30.0f, 0.5f, 1.0f, 1, "C"},
    {AutoSettingId::CoolingDelta, "MAIN", "coolingStartDeltaC", AutoSettingType::Float, 0.1f, 5.0f, 0.1f, 0.5f, 1, "C"},
    {AutoSettingId::HeatingDelta, "MAIN", "heatingStartDeltaC", AutoSettingType::Float, 0.1f, 5.0f, 0.1f, 0.5f, 1, "C"},
    {AutoSettingId::AllowVentCooling, "ALLOW", "allowVentCooling", AutoSettingType::Bool, 0, 1, 1, 1, 0, ""},
    {AutoSettingId::AllowAcCooling, "ALLOW", "allowAcCooling", AutoSettingType::Bool, 0, 1, 1, 1, 0, ""},
    {AutoSettingId::AllowAcHeating, "ALLOW", "allowAcHeating", AutoSettingType::Bool, 0, 1, 1, 1, 0, ""},
    {AutoSettingId::OutdoorMinDelta, "FREE COOL", "outdoorCoolingMinDeltaC", AutoSettingType::Float, 1.0f, 20.0f, 0.5f, 1.0f, 1, "C"},
    {AutoSettingId::OutdoorMaxTemp, "FREE COOL", "outdoorCoolingMaxTempC", AutoSettingType::Float, 5.0f, 35.0f, 0.5f, 1.0f, 1, "C"},
    {AutoSettingId::VentAlwaysOn, "VENT", "autoVentAlwaysOn", AutoSettingType::Bool, 0, 1, 1, 1, 0, ""},
    {AutoSettingId::VentDefaultStep, "VENT", "autoVentDefaultStep", AutoSettingType::UInt, 0, 6, 1, 1, 0, ""},
    {AutoSettingId::VentMinStep, "VENT", "autoVentMinStep", AutoSettingType::UInt, 0, 6, 1, 1, 0, ""},
    {AutoSettingId::VentCoolingStep, "VENT", "autoVentCoolingStep", AutoSettingType::UInt, 0, 6, 1, 1, 0, ""},
    {AutoSettingId::VentMaxStep, "VENT", "autoVentMaxStep", AutoSettingType::UInt, 0, 6, 1, 1, 0, ""},
    {AutoSettingId::BathCompStep, "EXHAUST", "bathExhaustCompStep", AutoSettingType::UInt, 0, 6, 1, 1, 0, ""},
    {AutoSettingId::HoodComp1, "EXHAUST", "hoodCompStep1", AutoSettingType::UInt, 0, 6, 1, 1, 0, ""},
    {AutoSettingId::HoodComp2, "EXHAUST", "hoodCompStep2", AutoSettingType::UInt, 0, 6, 1, 1, 0, ""},
    {AutoSettingId::HoodComp3, "EXHAUST", "hoodCompStep3", AutoSettingType::UInt, 0, 6, 1, 1, 0, ""},
    {AutoSettingId::AdditiveVent, "EXHAUST", "additiveVentComp", AutoSettingType::Bool, 0, 1, 1, 1, 0, ""},
    {AutoSettingId::ColdLimit, "COLD LIMIT", "coldOutdoorTempLimitC", AutoSettingType::Float, -30.0f, 10.0f, 0.5f, 1.0f, 1, "C"},
    {AutoSettingId::ColdMaxStep, "COLD LIMIT", "coldOutdoorMaxVentStep", AutoSettingType::UInt, 0, 6, 1, 1, 0, ""},
    {AutoSettingId::AcFanInAuto, "AC FAN", "keepAcFanOnInAuto", AutoSettingType::Bool, 0, 1, 1, 1, 0, ""},
    {AutoSettingId::AcFanWithVent, "AC FAN", "keepAcFanOnWithVent", AutoSettingType::Bool, 0, 1, 1, 1, 0, ""},
    {AutoSettingId::AcFanMode, "AC FAN", "acFanOnlyMode", AutoSettingType::UInt, 1, 5, 1, 1, 0, ""},
    {AutoSettingId::AcFanMin, "AC FAN", "acFanMinSpeed", AutoSettingType::UInt, 0, 4, 1, 1, 0, ""},
    {AutoSettingId::AcFanNormal, "AC FAN", "acFanNormalSpeed", AutoSettingType::UInt, 0, 4, 1, 1, 0, ""},
    {AutoSettingId::AcFanBoost, "AC FAN", "acFanBoostSpeed", AutoSettingType::UInt, 0, 4, 1, 1, 0, ""},
    {AutoSettingId::AcFanMax, "AC FAN", "acFanMaxSpeed", AutoSettingType::UInt, 0, 4, 1, 1, 0, ""},
    {AutoSettingId::AcFanAuto, "AC FAN", "acFanAutoAllowed", AutoSettingType::Bool, 0, 1, 1, 1, 0, ""},
    {AutoSettingId::AcDynamic, "AC DYNAMIC", "acDynamicControl", AutoSettingType::Bool, 0, 1, 1, 1, 0, ""},
    {AutoSettingId::AcCoolFullDelta, "AC DYNAMIC", "acCoolingFullDelta", AutoSettingType::Float, 0.1f, 10.0f, 0.1f, 0.5f, 1, "C"},
    {AutoSettingId::AcCoolMinOffset, "AC DYNAMIC", "acCoolingMinOffset", AutoSettingType::Float, 0.0f, 8.0f, 0.1f, 0.5f, 1, "C"},
    {AutoSettingId::AcCoolMaxOffset, "AC DYNAMIC", "acCoolingMaxOffset", AutoSettingType::Float, 0.0f, 10.0f, 0.1f, 0.5f, 1, "C"},
    {AutoSettingId::AcCoolMinTemp, "AC DYNAMIC", "acCoolingMinSetpoint", AutoSettingType::Float, 16.0f, 30.0f, 1.0f, 2.0f, 0, "C"},
    {AutoSettingId::AcCoolFanMin, "AC DYNAMIC", "acCoolingMinFan", AutoSettingType::UInt, 0, 4, 1, 1, 0, ""},
    {AutoSettingId::AcCoolFanMax, "AC DYNAMIC", "acCoolingMaxFan", AutoSettingType::UInt, 0, 4, 1, 1, 0, ""},
    {AutoSettingId::AcHeatFullDelta, "AC HEAT", "acHeatingFullDelta", AutoSettingType::Float, 0.1f, 10.0f, 0.1f, 0.5f, 1, "C"},
    {AutoSettingId::AcHeatMinOffset, "AC HEAT", "acHeatingMinOffset", AutoSettingType::Float, 0.0f, 8.0f, 0.1f, 0.5f, 1, "C"},
    {AutoSettingId::AcHeatMaxOffset, "AC HEAT", "acHeatingMaxOffset", AutoSettingType::Float, 0.0f, 10.0f, 0.1f, 0.5f, 1, "C"},
    {AutoSettingId::AcHeatMaxTemp, "AC HEAT", "acHeatingMaxSetpoint", AutoSettingType::Float, 16.0f, 30.0f, 1.0f, 2.0f, 0, "C"},
    {AutoSettingId::AcHeatFanMin, "AC HEAT", "acHeatingMinFan", AutoSettingType::UInt, 0, 4, 1, 1, 0, ""},
    {AutoSettingId::AcHeatFanMax, "AC HEAT", "acHeatingMaxFan", AutoSettingType::UInt, 0, 4, 1, 1, 0, ""},
    {AutoSettingId::DecisionInterval, "TIMING", "decisionIntervalMs", AutoSettingType::Milliseconds, 1000, 60000, 1000, 10000, 0, "ms"},
    {AutoSettingId::MinStateHold, "TIMING", "minStateHoldMs", AutoSettingType::Milliseconds, 0, 3600000, 10000, 60000, 0, "ms"},
    {AutoSettingId::VentCoolCheck, "TIMING", "ventCoolingCheckMs", AutoSettingType::Milliseconds, 60000, 21600000, 60000, 300000, 0, "ms"},
    {AutoSettingId::VentCoolMinDrop, "TIMING", "ventCoolingMinDropC", AutoSettingType::Float, 0.0f, 5.0f, 0.1f, 0.5f, 1, "C"},
    {AutoSettingId::VentCoolStepUp, "TIMING", "ventCoolingStepUp", AutoSettingType::Bool, 0, 1, 1, 1, 0, ""},
    {AutoSettingId::VentCoolFallback, "TIMING", "ventCoolingFallbackAc", AutoSettingType::Bool, 0, 1, 1, 1, 0, ""},
    {AutoSettingId::SafeNoIndoor, "SAFETY", "safeOnIndoorMissing", AutoSettingType::Bool, 0, 1, 1, 1, 0, ""},
    {AutoSettingId::SafeEquipment, "SAFETY", "safeOnEquipmentErr", AutoSettingType::Bool, 0, 1, 1, 1, 0, ""},
    {AutoSettingId::VentCompInterval, "FAST VENT", "ventCompIntervalMs", AutoSettingType::Milliseconds, 500, 10000, 500, 1000, 0, "ms"},
    {AutoSettingId::VentCompOffDelay, "FAST VENT", "ventCompOffDelayMs", AutoSettingType::Milliseconds, 0, 300000, 1000, 60000, 0, "ms"},
    {AutoSettingId::VentCompUp, "FAST VENT", "ventCompImmediateUp", AutoSettingType::Bool, 0, 1, 1, 1, 0, ""},
    {AutoSettingId::VentCompDown, "FAST VENT", "ventCompImmediateDown", AutoSettingType::Bool, 0, 1, 1, 1, 0, ""}
};

constexpr uint8_t AUTO_SETTINGS_COUNT = sizeof(AUTO_SETTINGS) / sizeof(AUTO_SETTINGS[0]);

TempSensorRole tempRoleFromIndex(uint8_t index) {
    switch (index % 4) {
        case 0:
            return TempSensorRole::Indoor;
        case 1:
            return TempSensorRole::Outdoor;
        case 2:
            return TempSensorRole::Unknown;
        case 3:
        default:
            return TempSensorRole::Unused;
    }
}

uint8_t tempRoleToIndex(TempSensorRole role) {
    switch (role) {
        case TempSensorRole::Indoor:
            return 0;
        case TempSensorRole::Outdoor:
            return 1;
        case TempSensorRole::Unknown:
            return 2;
        case TempSensorRole::Unused:
        default:
            return 3;
    }
}

float clampAutoValue(float value, const AutoSettingDescriptor& descriptor) {
    if (value < descriptor.minValue) return descriptor.minValue;
    if (value > descriptor.maxValue) return descriptor.maxValue;
    return value;
}

float getAutoSettingValue(const AutoControlSettings& settings, AutoSettingId id) {
    switch (id) {
        case AutoSettingId::AutoEnabled: return settings.autoEnabled ? 1 : 0;
        case AutoSettingId::DryRun: return settings.dryRun ? 1 : 0;
        case AutoSettingId::DiagnosticVerbose: return settings.diagnosticVerbose ? 1 : 0;
        case AutoSettingId::TargetTemp: return settings.targetTempC;
        case AutoSettingId::CoolingDelta: return settings.coolingStartDeltaC;
        case AutoSettingId::HeatingDelta: return settings.heatingStartDeltaC;
        case AutoSettingId::AllowVentCooling: return settings.allowVentCooling ? 1 : 0;
        case AutoSettingId::AllowAcCooling: return settings.allowAcCooling ? 1 : 0;
        case AutoSettingId::AllowAcHeating: return settings.allowAcHeating ? 1 : 0;
        case AutoSettingId::OutdoorMinDelta: return settings.outdoorCoolingMinDeltaC;
        case AutoSettingId::OutdoorMaxTemp: return settings.outdoorCoolingMaxTempC;
        case AutoSettingId::VentAlwaysOn: return settings.autoVentAlwaysOn ? 1 : 0;
        case AutoSettingId::VentDefaultStep: return settings.autoVentDefaultStep;
        case AutoSettingId::VentMinStep: return settings.autoVentMinStep;
        case AutoSettingId::VentCoolingStep: return settings.autoVentCoolingStep;
        case AutoSettingId::VentMaxStep: return settings.autoVentMaxStep;
        case AutoSettingId::BathCompStep: return settings.bathExhaustCompStep;
        case AutoSettingId::HoodComp1: return settings.hoodCompStep1;
        case AutoSettingId::HoodComp2: return settings.hoodCompStep2;
        case AutoSettingId::HoodComp3: return settings.hoodCompStep3;
        case AutoSettingId::AdditiveVent: return settings.additiveVentCompensation ? 1 : 0;
        case AutoSettingId::ColdLimit: return settings.coldOutdoorTempLimitC;
        case AutoSettingId::ColdMaxStep: return settings.coldOutdoorMaxVentStep;
        case AutoSettingId::AcFanInAuto: return settings.keepAcFanOnInAuto ? 1 : 0;
        case AutoSettingId::AcFanWithVent: return settings.keepAcFanOnWithVent ? 1 : 0;
        case AutoSettingId::AcFanMode: return settings.acFanOnlyMode;
        case AutoSettingId::AcFanMin: return settings.acFanMinSpeed;
        case AutoSettingId::AcFanNormal: return settings.acFanNormalSpeed;
        case AutoSettingId::AcFanBoost: return settings.acFanBoostSpeed;
        case AutoSettingId::AcFanMax: return settings.acFanMaxSpeed;
        case AutoSettingId::AcFanAuto: return settings.acFanAutoAllowed ? 1 : 0;
        case AutoSettingId::AcDynamic: return settings.acDynamicControlEnabled ? 1 : 0;
        case AutoSettingId::AcCoolFullDelta: return settings.acCoolingFullPowerDeltaC;
        case AutoSettingId::AcCoolMinOffset: return settings.acCoolingMinTempOffsetC;
        case AutoSettingId::AcCoolMaxOffset: return settings.acCoolingMaxTempOffsetC;
        case AutoSettingId::AcCoolMinTemp: return settings.acCoolingMinSetpointC;
        case AutoSettingId::AcCoolFanMin: return settings.acCoolingMinFanSpeed;
        case AutoSettingId::AcCoolFanMax: return settings.acCoolingMaxFanSpeed;
        case AutoSettingId::AcHeatFullDelta: return settings.acHeatingFullPowerDeltaC;
        case AutoSettingId::AcHeatMinOffset: return settings.acHeatingMinTempOffsetC;
        case AutoSettingId::AcHeatMaxOffset: return settings.acHeatingMaxTempOffsetC;
        case AutoSettingId::AcHeatMaxTemp: return settings.acHeatingMaxSetpointC;
        case AutoSettingId::AcHeatFanMin: return settings.acHeatingMinFanSpeed;
        case AutoSettingId::AcHeatFanMax: return settings.acHeatingMaxFanSpeed;
        case AutoSettingId::DecisionInterval: return settings.decisionIntervalMs;
        case AutoSettingId::MinStateHold: return settings.minStateHoldMs;
        case AutoSettingId::VentCoolCheck: return settings.ventCoolingCheckIntervalMs;
        case AutoSettingId::VentCoolMinDrop: return settings.ventCoolingMinDropC;
        case AutoSettingId::VentCoolStepUp: return settings.ventCoolingStepUpOnFail ? 1 : 0;
        case AutoSettingId::VentCoolFallback: return settings.ventCoolingFallbackToAc ? 1 : 0;
        case AutoSettingId::SafeNoIndoor: return settings.safeOnIndoorSensorMissing ? 1 : 0;
        case AutoSettingId::SafeEquipment: return settings.safeOnCriticalEquipmentError ? 1 : 0;
        case AutoSettingId::VentCompInterval: return settings.ventCompensationUpdateIntervalMs;
        case AutoSettingId::VentCompOffDelay: return settings.ventCompensationOffDelayMs;
        case AutoSettingId::VentCompUp: return settings.ventCompensationImmediateUp ? 1 : 0;
        case AutoSettingId::VentCompDown: return settings.ventCompensationImmediateDown ? 1 : 0;
    }

    return 0.0f;
}

void setAutoSettingValue(AutoControlSettings& settings, AutoSettingId id, float value) {
    const uint8_t byteValue = (uint8_t)roundf(value);
    const unsigned long msValue = (unsigned long)roundf(value);
    switch (id) {
        case AutoSettingId::AutoEnabled: settings.autoEnabled = value >= 0.5f; break;
        case AutoSettingId::DryRun: settings.dryRun = value >= 0.5f; break;
        case AutoSettingId::DiagnosticVerbose: settings.diagnosticVerbose = value >= 0.5f; break;
        case AutoSettingId::TargetTemp: settings.targetTempC = value; break;
        case AutoSettingId::CoolingDelta: settings.coolingStartDeltaC = value; break;
        case AutoSettingId::HeatingDelta: settings.heatingStartDeltaC = value; break;
        case AutoSettingId::AllowVentCooling: settings.allowVentCooling = value >= 0.5f; break;
        case AutoSettingId::AllowAcCooling: settings.allowAcCooling = value >= 0.5f; break;
        case AutoSettingId::AllowAcHeating: settings.allowAcHeating = value >= 0.5f; break;
        case AutoSettingId::OutdoorMinDelta: settings.outdoorCoolingMinDeltaC = value; break;
        case AutoSettingId::OutdoorMaxTemp: settings.outdoorCoolingMaxTempC = value; break;
        case AutoSettingId::VentAlwaysOn: settings.autoVentAlwaysOn = value >= 0.5f; break;
        case AutoSettingId::VentDefaultStep: settings.autoVentDefaultStep = byteValue; break;
        case AutoSettingId::VentMinStep: settings.autoVentMinStep = byteValue; break;
        case AutoSettingId::VentCoolingStep: settings.autoVentCoolingStep = byteValue; break;
        case AutoSettingId::VentMaxStep: settings.autoVentMaxStep = byteValue; break;
        case AutoSettingId::BathCompStep: settings.bathExhaustCompStep = byteValue; break;
        case AutoSettingId::HoodComp1: settings.hoodCompStep1 = byteValue; break;
        case AutoSettingId::HoodComp2: settings.hoodCompStep2 = byteValue; break;
        case AutoSettingId::HoodComp3: settings.hoodCompStep3 = byteValue; break;
        case AutoSettingId::AdditiveVent: settings.additiveVentCompensation = value >= 0.5f; break;
        case AutoSettingId::ColdLimit: settings.coldOutdoorTempLimitC = value; break;
        case AutoSettingId::ColdMaxStep: settings.coldOutdoorMaxVentStep = byteValue; break;
        case AutoSettingId::AcFanInAuto: settings.keepAcFanOnInAuto = value >= 0.5f; break;
        case AutoSettingId::AcFanWithVent: settings.keepAcFanOnWithVent = value >= 0.5f; break;
        case AutoSettingId::AcFanMode: settings.acFanOnlyMode = byteValue; break;
        case AutoSettingId::AcFanMin: settings.acFanMinSpeed = byteValue; break;
        case AutoSettingId::AcFanNormal: settings.acFanNormalSpeed = byteValue; break;
        case AutoSettingId::AcFanBoost: settings.acFanBoostSpeed = byteValue; break;
        case AutoSettingId::AcFanMax: settings.acFanMaxSpeed = byteValue; break;
        case AutoSettingId::AcFanAuto: settings.acFanAutoAllowed = value >= 0.5f; break;
        case AutoSettingId::AcDynamic: settings.acDynamicControlEnabled = value >= 0.5f; break;
        case AutoSettingId::AcCoolFullDelta: settings.acCoolingFullPowerDeltaC = value; break;
        case AutoSettingId::AcCoolMinOffset: settings.acCoolingMinTempOffsetC = value; break;
        case AutoSettingId::AcCoolMaxOffset: settings.acCoolingMaxTempOffsetC = value; break;
        case AutoSettingId::AcCoolMinTemp: settings.acCoolingMinSetpointC = value; break;
        case AutoSettingId::AcCoolFanMin: settings.acCoolingMinFanSpeed = byteValue; break;
        case AutoSettingId::AcCoolFanMax: settings.acCoolingMaxFanSpeed = byteValue; break;
        case AutoSettingId::AcHeatFullDelta: settings.acHeatingFullPowerDeltaC = value; break;
        case AutoSettingId::AcHeatMinOffset: settings.acHeatingMinTempOffsetC = value; break;
        case AutoSettingId::AcHeatMaxOffset: settings.acHeatingMaxTempOffsetC = value; break;
        case AutoSettingId::AcHeatMaxTemp: settings.acHeatingMaxSetpointC = value; break;
        case AutoSettingId::AcHeatFanMin: settings.acHeatingMinFanSpeed = byteValue; break;
        case AutoSettingId::AcHeatFanMax: settings.acHeatingMaxFanSpeed = byteValue; break;
        case AutoSettingId::DecisionInterval: settings.decisionIntervalMs = msValue; break;
        case AutoSettingId::MinStateHold: settings.minStateHoldMs = msValue; break;
        case AutoSettingId::VentCoolCheck: settings.ventCoolingCheckIntervalMs = msValue; break;
        case AutoSettingId::VentCoolMinDrop: settings.ventCoolingMinDropC = value; break;
        case AutoSettingId::VentCoolStepUp: settings.ventCoolingStepUpOnFail = value >= 0.5f; break;
        case AutoSettingId::VentCoolFallback: settings.ventCoolingFallbackToAc = value >= 0.5f; break;
        case AutoSettingId::SafeNoIndoor: settings.safeOnIndoorSensorMissing = value >= 0.5f; break;
        case AutoSettingId::SafeEquipment: settings.safeOnCriticalEquipmentError = value >= 0.5f; break;
        case AutoSettingId::VentCompInterval: settings.ventCompensationUpdateIntervalMs = msValue; break;
        case AutoSettingId::VentCompOffDelay: settings.ventCompensationOffDelayMs = msValue; break;
        case AutoSettingId::VentCompUp: settings.ventCompensationImmediateUp = value >= 0.5f; break;
        case AutoSettingId::VentCompDown: settings.ventCompensationImmediateDown = value >= 0.5f; break;
    }
}

String formatAutoSettingValue(const AutoControlSettings& settings, const AutoSettingDescriptor& descriptor) {
    const float value = getAutoSettingValue(settings, descriptor.id);
    if (descriptor.type == AutoSettingType::Bool) {
        return value >= 0.5f ? "ON" : "OFF";
    }
    if (descriptor.type == AutoSettingType::UInt || descriptor.type == AutoSettingType::Milliseconds) {
        return String((unsigned long)roundf(value)) + (descriptor.unit[0] ? String(" ") + descriptor.unit : String(""));
    }
    return String(value, static_cast<unsigned int>(descriptor.decimals)) + (descriptor.unit[0] ? String(" ") + descriptor.unit : String(""));
}
}

void DisplayUi::begin() {
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(COLOR_BG);
    tft.setTextDatum(TL_DATUM);

    ready = true;
    dirty = true;
    fullRedraw = true;
    shellRedraw = true;

    Logger::info(TAG_DISPLAY, "ST7789 display initialized");
}

void DisplayUi::update(const DeviceState& state, const AutoControlSettings& autoSettings) {
    if (!ready) {
        return;
    }

    const unsigned long now = millis();

    if (!dirty && now - lastRenderMs < RENDER_INTERVAL_MS) {
        return;
    }

    lastRenderMs = now;
    dirty = false;

    render(state, autoSettings);
}

void DisplayUi::nextPage() {
    const uint8_t next = (getPageIndex() + 1) % static_cast<uint8_t>(Page::Count);
    currentPage = static_cast<Page>(next);
    tempPageMode = TempPageMode::View;
    dirty = true;
    fullRedraw = true;
    shellRedraw = true;
}

void DisplayUi::previousPage() {
    const uint8_t count = static_cast<uint8_t>(Page::Count);
    const uint8_t previous = (getPageIndex() + count - 1) % count;
    currentPage = static_cast<Page>(previous);
    tempPageMode = TempPageMode::View;
    dirty = true;
    fullRedraw = true;
    shellRedraw = true;
}

void DisplayUi::setPage(Page page) {
    if (page >= Page::Count) {
        return;
    }

    currentPage = page;
    interactionMode = InteractionMode::View;
    tempPageMode = TempPageMode::View;
    dirty = true;
    fullRedraw = true;
    shellRedraw = true;
}

bool DisplayUi::isReady() const {
    return ready;
}

uint8_t DisplayUi::getPageIndex() const {
    return static_cast<uint8_t>(currentPage);
}

const char* DisplayUi::getPageName() const {
    return getPageName(currentPage);
}

DisplayUi::Action DisplayUi::handleButton(Button button, bool longPress, DeviceState& state, const AutoControlSettings& autoSettings) {
    if (longPress && button == Button::Back) {
        currentPage = Page::Overview;
        interactionMode = InteractionMode::View;
        tempPageMode = TempPageMode::View;
        dirty = true;
        fullRedraw = true;
        shellRedraw = true;
        Logger::debug(TAG_UI, "Long BACK: return to Overview");
        return {};
    }

    if (currentPage == Page::Temperatures) {
        return handleTemperatureButton(button, longPress, state.temperatures);
    }

    if (interactionMode == InteractionMode::View) {
        if (longPress) {
            return {};
        }

        switch (button) {
            case Button::Left:
                previousPage();
                Logger::debug(TAG_UI, "Page previous");
                break;
            case Button::Right:
                nextPage();
                Logger::debug(TAG_UI, "Page next");
                break;
            case Button::Ok:
                if (currentPage == Page::Overview) {
                    enterSelectMode(state);
                } else if (currentPage == Page::Settings) {
                    enterAutoSettingsSelect();
                } else {
                    Logger::debug(TAG_UI, "OK ignored: page has no editable parameters yet");
                }
                break;
            case Button::Back:
                if (currentPage != Page::Overview) {
                    currentPage = Page::Overview;
                    dirty = true;
                    fullRedraw = true;
                    shellRedraw = true;
                    Logger::debug(TAG_UI, "BACK: return to Overview");
                }
                break;
        }
        return {};
    }

    if (currentPage == Page::Settings) {
        return handleAutoSettingsButton(button, longPress, autoSettings);
    }

    if (longPress) {
        return {};
    }

    if (interactionMode == InteractionMode::Select) {
        switch (button) {
            case Button::Left:
                moveSelection(state, -1);
                break;
            case Button::Right:
                moveSelection(state, 1);
                break;
            case Button::Ok:
                enterEditMode(state);
                break;
            case Button::Back:
                interactionMode = InteractionMode::View;
                dirty = true;
                fullRedraw = true;
                Logger::debug(TAG_UI, "Selection canceled");
                break;
        }
        return {};
    }

    if (interactionMode == InteractionMode::Edit) {
        switch (button) {
            case Button::Left:
                changeEditValue(-1);
                break;
            case Button::Right:
                changeEditValue(1);
                break;
            case Button::Ok:
                return applyEdit(state);
            case Button::Back:
                cancelEdit(state);
                break;
        }
    }

    return {};
}

void DisplayUi::render(const DeviceState& state, const AutoControlSettings& autoSettings) {
    if (fullRedraw) {
        if (shellRedraw) {
            tft.fillScreen(COLOR_BG);
        } else {
            tft.fillRect(0, HEADER_H + 1, 320, FOOTER_Y - HEADER_H - 1, COLOR_BG);
        }
        resetLineCache();
        if (shellRedraw) {
            headerStatusCached = false;
            lastInteractionLabel = nullptr;
            lastFooterText[0] = '\0';
            lastUptimeText[0] = '\0';
            lastWarningCount = 255;
            lastErrorCount = 255;
        }
    }

    drawHeader(state, getPageName());
    drawFooter(state);

    switch (currentPage) {
        case Page::Overview:
            drawOverview(state);
            break;
        case Page::AirConditioner:
            drawAirConditioner(state.ac);
            break;
        case Page::Ventilation:
            drawVentilation(state);
            break;
        case Page::Temperatures:
            drawTemperatures(state.temperatures);
            break;
        case Page::Settings:
            drawSettings(state, autoSettings);
            break;
        case Page::Diagnostics:
            drawDiagnostics(state);
            break;
        case Page::Count:
            break;
    }

    fullRedraw = false;
    shellRedraw = false;
}

void DisplayUi::drawHeader(const DeviceState& state, const char* title) {
    if (shellRedraw) {
        tft.fillRect(0, 0, 320, HEADER_H, COLOR_PANEL);
        String headerTitle(title);
        headerTitle.toUpperCase();
        tft.setFreeFont(&FreeMonoBold9pt7b);
        tft.setTextColor(COLOR_TITLE, COLOR_PANEL);
        tft.drawString(headerTitle, 8, 2);
        tft.drawFastHLine(0, HEADER_H, 320, COLOR_TITLE);
    }

    const char* label = interactionLabel();
    const bool editLabel = interactionMode == InteractionMode::Edit;
    if (shellRedraw || label != lastInteractionLabel || editLabel != lastInteractionEdit) {
        lastInteractionLabel = label;
        lastInteractionEdit = editLabel;
        tft.fillRect(174, 1, 74, 18, COLOR_PANEL);
        if (label[0] != '\0') {
            tft.setFreeFont(&FreeMono9pt7b);
            tft.setTextColor(editLabel ? COLOR_TITLE : COLOR_ACCENT, COLOR_PANEL);
            tft.drawString(label, 176, 4);
        }
    }

    if (shellRedraw || !headerStatusCached || state.wifiConnected != lastHeaderWifiConnected) {
        lastHeaderWifiConnected = state.wifiConnected;
        drawWifiIcon(268, 10, state.wifiConnected ? COLOR_OK : COLOR_DANGER);
    }

    if (shellRedraw || !headerStatusCached || state.homeAssistant.connected != lastHeaderHaConnected) {
        lastHeaderHaConnected = state.homeAssistant.connected;
        drawHomeAssistantIcon(304, 10, state.homeAssistant.connected ? COLOR_OK : COLOR_DANGER);
    }

    headerStatusCached = true;
}

void DisplayUi::drawFooter(const DeviceState& state) {
    char footer[32];
    snprintf(
        footer,
        sizeof(footer),
        "%u/%u",
        getPageIndex() + 1,
        static_cast<uint8_t>(Page::Count)
    );

    if (shellRedraw) {
        tft.fillRect(0, FOOTER_Y, 320, FOOTER_H, COLOR_PANEL);
    }

    if (strcmp(lastFooterText, footer) != 0) {
        strncpy(lastFooterText, footer, sizeof(lastFooterText));
        lastFooterText[sizeof(lastFooterText) - 1] = '\0';
        tft.fillRect(8, FOOTER_Y + 8, 30, 10, COLOR_PANEL);
        tft.setFreeFont(nullptr);
        tft.setTextFont(1);
        tft.setTextSize(1);
        tft.setTextColor(COLOR_MUTED, COLOR_PANEL);
        tft.drawString(footer, 8, FOOTER_Y + 8);
    }

    if (lastWarningCount != state.controllerState.warningCount) {
        lastWarningCount = state.controllerState.warningCount;
        drawWarningIcon(52, FOOTER_Y + 12, state.controllerState.warningCount);
    }

    if (lastErrorCount != state.controllerState.errorCount) {
        lastErrorCount = state.controllerState.errorCount;
        drawErrorIcon(98, FOOTER_Y + 12, state.controllerState.errorCount);
    }

    String uptime = String("Up ") + state.uptimeText;
    uptime.toUpperCase();
    if (strncmp(lastUptimeText, uptime.c_str(), sizeof(lastUptimeText)) != 0) {
        strncpy(lastUptimeText, uptime.c_str(), sizeof(lastUptimeText));
        lastUptimeText[sizeof(lastUptimeText) - 1] = '\0';
        tft.fillRect(250, FOOTER_Y + 6, 62, 12, COLOR_PANEL);
        tft.setFreeFont(nullptr);
        tft.setTextFont(1);
        tft.setTextSize(1);
        tft.setTextColor(COLOR_MUTED, COLOR_PANEL);
        tft.drawString("UP", 226, FOOTER_Y + 8);
        tft.drawString(state.uptimeText, 250, FOOTER_Y + 8);
    }
}

void DisplayUi::resetLineCache() {
    for (uint8_t i = 0; i < LINE_CACHE_SIZE; i++) {
        lineCache[i] = "";
        lineColorCache[i] = 0;
    }
}

void DisplayUi::drawLine(uint8_t slot, int16_t x, int16_t y, const String& text, uint16_t color, uint8_t font) {
    drawTextBox(slot, x, y, 320 - x, text, color, font);
}

void DisplayUi::drawTextBox(uint8_t slot, int16_t x, int16_t y, int16_t w, const String& text, uint16_t color, uint8_t font) {
    if (slot >= LINE_CACHE_SIZE && !fullRedraw) {
        return;
    }

    if (slot < LINE_CACHE_SIZE && lineCache[slot] == text && lineColorCache[slot] == color) {
        return;
    }

    if (slot < LINE_CACHE_SIZE) {
        lineCache[slot] = text;
        lineColorCache[slot] = color;
    }

    const int16_t lineHeight = font >= 4 ? 30 : (font == 1 ? 14 : 22);
    const bool shellLine = y < HEADER_H || y >= FOOTER_Y;
    const uint16_t bgColor = shellLine ? COLOR_PANEL : COLOR_BG;

    tft.fillRect(x, y, w, lineHeight, bgColor);
    tft.setFreeFont(nullptr);
    tft.setTextFont(font);
    tft.setTextSize(1);
    tft.setTextColor(color, bgColor);
    tft.drawString(text, x, y);
}

void DisplayUi::drawLabel(uint8_t slot, int16_t x, int16_t y, int16_t w, const String& text) {
    if (slot >= LINE_CACHE_SIZE && !fullRedraw) {
        return;
    }

    if (slot < LINE_CACHE_SIZE && lineCache[slot] == text && lineColorCache[slot] == COLOR_ACCENT) {
        return;
    }

    if (slot < LINE_CACHE_SIZE) {
        lineCache[slot] = text;
        lineColorCache[slot] = COLOR_ACCENT;
    }

    tft.fillRect(x, y, w, 14, COLOR_BG);
    tft.setFreeFont(nullptr);
    tft.setTextFont(1);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_ACCENT, COLOR_BG);
    tft.drawString(text, x, y);
}

void DisplayUi::drawFreeTextBox(uint8_t slot, int16_t x, int16_t y, int16_t w, int16_t h, const String& text, const GFXfont* font, uint16_t color) {
    if (slot >= LINE_CACHE_SIZE && !fullRedraw) {
        return;
    }

    if (slot < LINE_CACHE_SIZE && lineCache[slot] == text && lineColorCache[slot] == color) {
        return;
    }

    if (slot < LINE_CACHE_SIZE) {
        lineCache[slot] = text;
        lineColorCache[slot] = color;
    }

    tft.fillRect(x, y, w, h, COLOR_BG);
    tft.setFreeFont(font);
    tft.setTextColor(color, COLOR_BG);
    tft.drawString(text, x, y);
}

void DisplayUi::drawFontTextBox(uint8_t slot, int16_t x, int16_t y, int16_t w, int16_t h, const String& text, uint8_t font, uint16_t color) {
    if (slot >= LINE_CACHE_SIZE && !fullRedraw) {
        return;
    }

    if (slot < LINE_CACHE_SIZE && lineCache[slot] == text && lineColorCache[slot] == color) {
        return;
    }

    if (slot < LINE_CACHE_SIZE) {
        lineCache[slot] = text;
        lineColorCache[slot] = color;
    }

    tft.fillRect(x, y, w, h, COLOR_BG);
    tft.setFreeFont(nullptr);
    tft.setTextFont(font);
    tft.setTextSize(1);
    tft.setTextColor(color, COLOR_BG);
    tft.drawString(text, x, y);
}

void DisplayUi::drawBoldText(int16_t x, int16_t y, const String& text, uint16_t color, uint16_t bg, uint8_t font) {
    tft.setFreeFont(nullptr);
    tft.setTextFont(font);
    tft.setTextSize(1);
    tft.setTextColor(color, bg);
    tft.drawString(text, x, y);
    tft.drawString(text, x + 1, y);
}

String DisplayUi::formatFloat(float value, uint8_t digits) const {
    if (value == DEVICE_DISCONNECTED_C) {
        return "DISCONNECTED";
    }

    return String(value, static_cast<unsigned int>(digits));
}

void DisplayUi::drawOverview(const DeviceState& state) {
    if (fullRedraw) {
        tft.drawFastVLine(160, 28, 84, COLOR_LINE);
        tft.drawFastVLine(160, 126, 82, COLOR_LINE);
        tft.drawFastHLine(10, 119, 144, COLOR_LINE);
        tft.drawFastHLine(166, 119, 144, COLOR_LINE);
    }

    const GFXfont* labelFont = &FreeMono9pt7b;
    const GFXfont* valueFont = &FreeMonoBold9pt7b;
    DeviceMode shownMode = state.controllerState.mode;
    float shownSetTemp = state.environment.targetIndoorTempC;
    bool shownAcPower = state.ac.powerOn;
    uint8_t shownAcMode = state.ac.mode;
    uint8_t shownAcTemp = state.ac.temperature;
    uint8_t shownAcFan = state.ac.fanMode;
    bool shownVfdPower = state.vfd.running || (state.vfd.hasActualFrequency && state.vfd.actualFrequencyHz > 1.0f);
    uint8_t shownVfdStep = vfdStep(state.vfd);

    if (interactionMode == InteractionMode::Edit) {
        switch (selectedParam) {
            case OverviewParam::Mode:
                shownMode = editValue == 0 ? DeviceMode::Auto : (editValue == 1 ? DeviceMode::Manual : DeviceMode::Disabled);
                break;
            case OverviewParam::SetTemp:
                shownSetTemp = editValue / 2.0f;
                break;
            case OverviewParam::AcPower:
                shownAcPower = editValue != 0;
                break;
            case OverviewParam::AcMode:
                shownAcMode = acModeFromListIndex((uint8_t)editValue);
                break;
            case OverviewParam::AcTemp:
                shownAcTemp = (uint8_t)editValue;
                break;
            case OverviewParam::AcFan:
                shownAcFan = (uint8_t)editValue;
                break;
            case OverviewParam::VfdPower:
                shownVfdPower = editValue != 0;
                break;
            case OverviewParam::VfdStep:
                shownVfdStep = (uint8_t)editValue;
                break;
            case OverviewParam::Count:
                break;
        }
    }

    drawFreeTextBox(0, 14, 36, 58, 18, "MODE:", labelFont, COLOR_MUTED);
    drawTextBox(1, 78, 32, 72, deviceModeName(shownMode), deviceModeColor(shownMode), 4);
    drawFreeTextBox(2, 14, 70, 70, 18, "State:", labelFont, COLOR_MUTED);
    drawFreeTextBox(3, 14, 88, 104, 18, activityName(state.controllerState.activity), valueFont, activityColor(state.controllerState.activity));
    drawActivityIcon(136, 96, state.controllerState.activity);

    drawFontTextBox(4, 168, 32, 44, 16, "Hood", 2, COLOR_ACCENT);
    drawFreeTextBox(5, 214, 34, 20, 16, String(state.environment.kitchenHoodLevel), valueFont, state.environment.kitchenHoodLevel > 0 ? COLOR_OK : COLOR_TEXT);
    drawFontTextBox(6, 250, 32, 34, 16, "EXH", 2, COLOR_ACCENT);
    drawFreeTextBox(7, 286, 34, 34, 16, state.environment.exhaustVentEnabled ? "ON  " : "OFF ", valueFont, state.environment.exhaustVentEnabled ? COLOR_OK : COLOR_MUTED);
    drawFreeTextBox(8, 168, 54, 132, 18, "Temperature", labelFont, COLOR_MUTED);

    const String indoorText = state.environment.hasIndoorTemp ? formatFloat(state.environment.indoorTempC, 1) + "C" : "--.-C";
    const String outdoorText = state.environment.hasOutdoorTemp ? formatFloat(state.environment.outdoorTempC, 1) + "C" : "--.-C";
    const float delta = state.environment.hasIndoorTemp
        ? state.environment.indoorTempC - shownSetTemp
        : 0.0f;
    const String deltaText = state.environment.hasIndoorTemp
        ? String(delta >= 0.0f ? "+" : "") + String(delta, 1) + "C"
        : String("--.-C");
    const bool deltaInNeutralZone = state.environment.hasIndoorTemp
        && delta < state.environment.coolingStartDeltaC
        && delta > -state.environment.heatingStartDeltaC;

    drawFontTextBox(9, 168, 76, 22, 16, "In", 2, COLOR_ACCENT);
    drawFontTextBox(10, 190, 76, 52, 16, indoorText, 2, state.environment.hasIndoorTemp ? COLOR_TEXT : COLOR_WARN);
    drawFontTextBox(11, 244, 76, 28, 16, "Out", 2, COLOR_ACCENT);
    drawFontTextBox(12, 274, 76, 42, 16, outdoorText, 2, state.environment.hasOutdoorTemp ? COLOR_TEXT : COLOR_WARN);
    drawFontTextBox(13, 168, 96, 28, 16, "Set", 2, COLOR_ACCENT);
    drawFontTextBox(14, 198, 96, 50, 16, String(shownSetTemp, 1) + "C", 2, COLOR_TEXT);
    drawFontTextBox(15, 250, 96, 32, 16, "Dlt", 2, COLOR_ACCENT);
    drawFontTextBox(16, 282, 96, 34, 16, deltaText, 2, deltaInNeutralZone ? COLOR_OK : COLOR_WARN);

    const char* acLink = "Wait";
    uint16_t acLinkColor = COLOR_WARN;
    if (state.ac.bound) {
        acLink = "Linked";
        acLinkColor = COLOR_OK;
    } else if (state.ac.hasReceivedFrame && state.ac.lastFrameAgeMs > 5000) {
        acLink = "Error";
        acLinkColor = COLOR_DANGER;
    }

    drawFreeTextBox(17, 14, 128, 34, 18, "AC:", labelFont, COLOR_ACCENT);
    drawFreeTextBox(18, 52, 128, 96, 18, acLink, valueFont, acLinkColor);
    drawFontTextBox(19, 14, 150, 50, 16, "Power", 2, COLOR_MUTED);
    drawFreeTextBox(20, 68, 150, 36, 18, shownAcPower ? "ON " : "OFF", valueFont, shownAcPower ? COLOR_OK : COLOR_MUTED);
    drawFontTextBox(21, 14, 170, 42, 16, "Mode", 2, COLOR_MUTED);
    drawFreeTextBox(22, 62, 170, 82, 18, acModeTitle(shownAcMode), valueFont, shownAcPower ? activityColor(ControllerActivity::AcCool) : COLOR_MUTED);
    drawFontTextBox(23, 14, 194, 24, 16, "Set", 2, COLOR_MUTED);
    drawFreeTextBox(24, 42, 192, 36, 18, String(shownAcTemp) + "C", valueFont, COLOR_TEXT);
    drawFontTextBox(25, 82, 194, 24, 16, "Fan", 2, COLOR_MUTED);
    drawFreeTextBox(26, 110, 192, 44, 18, acFanTitle(shownAcFan), valueFont, COLOR_TEXT);

    const char* vfdLink = "Wait";
    uint16_t vfdLinkColor = COLOR_WARN;
    if (state.vfd.communicationError) {
        vfdLink = "Error";
        vfdLinkColor = COLOR_DANGER;
    } else if (state.vfd.everOnline || state.vfd.online) {
        vfdLink = "Linked";
        vfdLinkColor = COLOR_OK;
    }

    drawFreeTextBox(27, 168, 128, 46, 18, "VFD:", labelFont, COLOR_ACCENT);
    drawFreeTextBox(28, 218, 128, 90, 18, vfdLink, valueFont, vfdLinkColor);
    drawFontTextBox(29, 168, 150, 50, 16, "Power", 2, COLOR_MUTED);
    drawFreeTextBox(30, 224, 150, 40, 18, shownVfdPower ? "ON " : "OFF", valueFont, shownVfdPower ? COLOR_OK : COLOR_MUTED);
    drawFontTextBox(31, 168, 170, 40, 16, "Step", 2, COLOR_MUTED);
    drawFreeTextBox(32, 212, 170, 38, 18, String(shownVfdStep) + "/6", valueFont, shownVfdStep > 0 ? COLOR_OK : COLOR_MUTED);
    drawFontTextBox(33, 168, 194, 38, 16, "Freq", 2, COLOR_MUTED);
    const bool hasVfdFrequency = state.vfd.hasActualFrequency;
    const float vfdFrequency = state.vfd.actualFrequencyHz;
    drawFreeTextBox(34, 212, 192, 72, 18, hasVfdFrequency ? String(vfdFrequency, 0) + " Hz" : "-- Hz", valueFont, hasVfdFrequency ? COLOR_TEXT : COLOR_MUTED);
    drawOverviewSelection(state);
}

void DisplayUi::drawTemperatures(const TemperatureStateSnapshot& temperatures) {
    if (fullRedraw) {
        tft.drawRoundRect(8, 28, 304, 182, 6, COLOR_ACCENT);
        tempPageCacheValid = false;
    }

    drawTemperatureList(temperatures);
}

void DisplayUi::drawTemperatureList(const TemperatureStateSnapshot& temperatures) {
    const uint8_t count = temperatures.sensorCount;
    if (count > 0 && selectedTempSensor >= count) {
        selectedTempSensor = count - 1;
    } else if (count == 0) {
        selectedTempSensor = 0;
    }

    if (selectedTempSensor < tempSensorScroll) {
        tempSensorScroll = selectedTempSensor;
    } else if (selectedTempSensor >= tempSensorScroll + TEMP_VISIBLE_ROWS) {
        tempSensorScroll = selectedTempSensor - TEMP_VISIBLE_ROWS + 1;
    }

    const bool modeChanged = !tempPageCacheValid || lastTempPageMode != tempPageMode;
    const bool rangeChanged = !tempPageCacheValid || lastTempVisibleStart != tempSensorScroll || lastTempCount != count;
    const bool selectionChanged = !tempPageCacheValid || lastTempSelectedIndex != selectedTempSensor || lastTempMenuIndex != selectedTempMenu || lastTempRoleIndex != selectedTempRole;

    if (modeChanged || rangeChanged) {
        tft.fillRect(12, 32, 296, 180, COLOR_BG);
        for (uint8_t row = 0; row < TEMP_VISIBLE_ROWS; row++) {
            lastTempRowIndex[row] = 255;
            lastTempRowText[row] = "";
            lastTempRowSelected[row] = false;
        }
        lastTempInfoText = "";
        lastTempHintText = "";
    }

    tft.setFreeFont(nullptr);
    tft.setTextSize(1);
    tft.setTextFont(2);
    tft.setTextColor(COLOR_TITLE, COLOR_BG);
    if (modeChanged || rangeChanged || selectionChanged) {
        tft.fillRect(14, 32, 292, 18, COLOR_BG);
        const char* title = tempPageMode == TempPageMode::Config ? "TEMP CONFIG"
            : (tempPageMode == TempPageMode::Menu ? "SENSOR MENU"
            : (tempPageMode == TempPageMode::EditRole ? "ASSIGN ROLE"
            : (tempPageMode == TempPageMode::ConfirmForget ? "FORGET SENSOR?" : "TEMP SENSORS")));
        tft.drawString(title, 18, 32);
        tft.setTextColor(COLOR_WARN, COLOR_BG);
        tft.drawRightString(String(count) + " known", 302, 32, 2);
    }

    if (count == 0) {
        const String line = "No sensors";
        if (!tempPageCacheValid || lastTempRowText[0] != line) {
            tft.fillRect(12, 74, 296, 24, COLOR_BG);
            tft.setTextFont(2);
            tft.setTextColor(COLOR_WARN, COLOR_BG);
            tft.drawString(line, 20, 76);
            lastTempRowText[0] = line;
        }

        const String hint = "LEFT/RIGHT page  OK scan";
        if (!tempPageCacheValid || lastTempHintText != hint) {
            lastTempHintText = hint;
            tft.fillRect(14, 198, 292, 14, COLOR_BG);
            tft.setTextFont(1);
            tft.setTextColor(COLOR_WARN, COLOR_BG);
            tft.drawString(hint, 18, 201);
        }

        lastTempPageMode = tempPageMode;
        lastTempVisibleStart = tempSensorScroll;
        lastTempSelectedIndex = selectedTempSensor;
        lastTempCount = count;
        tempPageCacheValid = true;
        return;
    }

    if (tempPageMode == TempPageMode::Menu) {
        static constexpr const char* MENU_ITEMS[] = {"Assign role", "Force read", "Scan bus", "Swap IN/OUT", "Forget sensor", "Back"};
        for (uint8_t row = 0; row < 6; row++) {
            const int y = 52 + row * 22;
            const bool selected = row == selectedTempMenu;
            const String rowText = String(selected ? "> " : "  ") + MENU_ITEMS[row];
            if (!tempPageCacheValid || modeChanged || selectionChanged || lastTempRowText[row] != rowText || lastTempRowSelected[row] != selected) {
                lastTempRowText[row] = rowText;
                lastTempRowSelected[row] = selected;
                tft.fillRect(10, y - 5, 300, 26, COLOR_BG);
                tft.setTextFont(2);
                tft.setTextColor(selected ? COLOR_TEXT : COLOR_MUTED, COLOR_BG);
                tft.drawString(rowText, 22, y);
            }
        }
    } else if (tempPageMode == TempPageMode::EditRole) {
        static constexpr TempSensorRole ROLES[] = {TempSensorRole::Indoor, TempSensorRole::Outdoor, TempSensorRole::Unknown, TempSensorRole::Unused};
        for (uint8_t row = 0; row < 4; row++) {
            const int y = 64 + row * 28;
            const bool selected = row == selectedTempRole;
            const String rowText = String(selected ? "> " : "  ") + tempRoleTitle(ROLES[row]);
            if (!tempPageCacheValid || modeChanged || selectionChanged || lastTempRowText[row] != rowText || lastTempRowSelected[row] != selected) {
                lastTempRowText[row] = rowText;
                lastTempRowSelected[row] = selected;
                tft.fillRect(10, y - 7, 300, 30, COLOR_BG);
                tft.setTextFont(2);
                tft.setTextColor(selected ? COLOR_TITLE : COLOR_MUTED, COLOR_BG);
                tft.drawString(rowText, 22, y);
            }
        }
    } else if (tempPageMode == TempPageMode::ConfirmForget) {
        const TemperatureStateSnapshot::Sensor& sensor = temperatures.sensors[selectedTempSensor];
        const String rowText = "Forget " + tempShortAddress(sensor.address) + "?";
        if (!tempPageCacheValid || modeChanged || lastTempRowText[0] != rowText) {
            lastTempRowText[0] = rowText;
            tft.fillRect(12, 82, 296, 42, COLOR_BG);
            tft.setTextFont(2);
            tft.setTextColor(COLOR_DANGER, COLOR_BG);
            tft.drawString(rowText, 22, 86);
            tft.setTextColor(COLOR_WARN, COLOR_BG);
            tft.drawString("OK: yes   BACK: no", 22, 112);
        }
    } else {
        for (uint8_t row = 0; row < TEMP_VISIBLE_ROWS; row++) {
            const uint8_t index = tempSensorScroll + row;
            const int y = 52 + row * 20;

            if (index >= count) {
                if (!tempPageCacheValid || lastTempRowIndex[row] != 255) {
                    tft.fillRect(12, y - 2, 296, 20, COLOR_BG);
                    lastTempRowIndex[row] = 255;
                    lastTempRowText[row] = "";
                    lastTempRowSelected[row] = false;
                }
                continue;
            }

            const TemperatureStateSnapshot::Sensor& sensor = temperatures.sensors[index];
            const bool selected = tempPageMode == TempPageMode::Config && index == selectedTempSensor;
            const char* status = !sensor.enabled ? "OFF" : (sensor.connected && sensor.hasTemperature ? "OK" : (sensor.connected ? "ERR" : "LOST"));
            const String tempText = sensor.hasTemperature ? String(sensor.temperatureC, 1) + "C" : "--.-C";
            const String rowText = String(index) + " " + tempRoleShort(sensor.role) + " " + tempText + " " + status;
            const bool rowChanged = rangeChanged || selectionChanged || !tempPageCacheValid || lastTempRowIndex[row] != index || lastTempRowText[row] != rowText || lastTempRowSelected[row] != selected;
            if (!rowChanged) continue;

            lastTempRowIndex[row] = index;
            lastTempRowText[row] = rowText;
            lastTempRowSelected[row] = selected;

            tft.fillRect(10, y - 5, 300, 26, COLOR_BG);
            tft.setTextFont(2);
            tft.setTextColor(selected ? COLOR_TEXT : (sensor.enabled ? COLOR_MUTED : COLOR_MUTED), COLOR_BG);
            tft.drawString(selected ? ">" : " ", 18, y);
            tft.setTextColor(sensor.role == TempSensorRole::Unknown ? COLOR_WARN : (sensor.role == TempSensorRole::Unused ? COLOR_MUTED : COLOR_OK), COLOR_BG);
            tft.drawString(String(index) + " " + tempRoleShort(sensor.role), 34, y);
            tft.setTextColor(sensor.hasTemperature ? COLOR_TEXT : COLOR_WARN, COLOR_BG);
            tft.drawString(tempText, 122, y);
            tft.setTextColor((sensor.connected && sensor.hasTemperature) ? COLOR_OK : COLOR_WARN, COLOR_BG);
            tft.drawRightString(status, 300, y, 2);
            if (selected) {
                tft.drawFastHLine(16, y + 17, 288, COLOR_ACCENT);
            }
        }
    }

    const TemperatureStateSnapshot::Sensor& selectedSensor = temperatures.sensors[selectedTempSensor];
    const String info = tempShortAddress(selectedSensor.address) + "  miss=" + String(selectedSensor.missedScanCount) + " fail=" + String(selectedSensor.failedReadCount);
    if (!tempPageCacheValid || lastTempInfoText != info || modeChanged || selectionChanged) {
        lastTempInfoText = info;
        tft.fillRect(14, 180, 292, 14, COLOR_BG);
        tft.setTextFont(1);
        tft.setTextColor(COLOR_TEXT, COLOR_BG);
        tft.drawString(info, 18, 183);
    }

    const char* hint = tempPageMode == TempPageMode::Menu ? "LEFT/RIGHT item  OK run  BACK list"
        : (tempPageMode == TempPageMode::EditRole ? "LEFT/RIGHT role  OK apply  BACK cancel"
        : (tempPageMode == TempPageMode::ConfirmForget ? "OK yes  BACK no"
        : (tempPageMode == TempPageMode::Config ? "LEFT/RIGHT sensor  OK menu  BACK view" : "LEFT/RIGHT page  OK config")));
    if (!tempPageCacheValid || lastTempHintText != hint || modeChanged) {
        lastTempHintText = hint;
        tft.fillRect(14, 198, 292, 16, COLOR_BG);
        tft.setTextFont(1);
        tft.setTextColor(COLOR_WARN, COLOR_BG);
        tft.drawString(hint, 18, 200);
    }

    lastTempPageMode = tempPageMode;
    lastTempVisibleStart = tempSensorScroll;
    lastTempSelectedIndex = selectedTempSensor;
    lastTempMenuIndex = selectedTempMenu;
    lastTempRoleIndex = selectedTempRole;
    lastTempCount = count;
    tempPageCacheValid = true;
}

void DisplayUi::drawAirConditioner(const AcStateSnapshot& ac) {
    if (fullRedraw) {
        drawPanel(10, 40, 145, 78, COLOR_ACCENT);
        drawPanel(165, 40, 145, 78, ac.bound ? COLOR_OK : COLOR_WARN);
        drawPanel(10, 128, 145, 78, ac.powerOn ? COLOR_OK : COLOR_MUTED);
        drawPanel(165, 128, 145, 78, COLOR_WARN);
    }

    drawTextBox(0, 20, 50, 125, "SETPOINT", COLOR_MUTED, 1);
    drawTextBox(1, 20, 72, 125, String(ac.temperature) + " C", COLOR_ACCENT, 4);

    drawTextBox(2, 175, 50, 125, "LINK", COLOR_MUTED, 1);
    drawTextBox(3, 175, 72, 125, ac.bound ? "BOUND" : "WAITING", ac.bound ? COLOR_OK : COLOR_WARN, 4);

    drawTextBox(4, 20, 138, 125, "POWER / MODE", COLOR_MUTED, 1);
    drawTextBox(
        5,
        20,
        160,
        125,
        String(ac.powerOn ? "ON" : "OFF") + "  " + acModeName(ac.mode),
        ac.powerOn ? COLOR_OK : COLOR_WARN,
        2
    );

    drawTextBox(6, 175, 138, 125, "FAN / ROLE", COLOR_MUTED, 1);
    drawTextBox(
        7,
        175,
        160,
        125,
        String(acFanName(ac.fanMode)) + "  " + (ac.primaryController ? "PRI" : "SEC"),
        COLOR_TEXT,
        2
    );

    if (ac.hasReceivedFrame) {
        const uint16_t frameColor = ac.lastFrameAgeMs < 3000 ? COLOR_OK : COLOR_DANGER;
        drawTextBox(8, 20, 186, 125, "Frame: " + String(ac.lastFrameAgeMs) + " ms", frameColor, 1);
    } else {
        drawTextBox(8, 20, 186, 125, "Frame: never", COLOR_WARN, 1);
    }

    drawTextBox(
        9,
        175,
        186,
        125,
        String("Addr ") + String(ac.controllerAddress) + "  Mask 0x" + String(ac.updateFields, HEX),
        COLOR_MUTED,
        1
    );
}

void DisplayUi::drawVentilation(const DeviceState& state) {
    if (fullRedraw) {
        drawPanel(10, 42, 300, 64, state.environment.exhaustVentEnabled ? COLOR_OK : COLOR_MUTED);
        drawPanel(10, 116, 300, 92, COLOR_ACCENT);
    }

    drawTextBox(0, 20, 52, 275, "VENTILATION", COLOR_MUTED, 1);
    drawTextBox(1, 20, 72, 275, state.environment.exhaustVentEnabled ? "exhaust enabled" : "exhaust off", statusColor(state.environment.exhaustVentEnabled), 2);
    drawTextBox(2, 20, 128, 275, "Supply control is available on Overview", COLOR_TEXT, 2);
    drawTextBox(3, 20, 154, 275, "Manual VFD page will be expanded later", COLOR_MUTED, 2);
}

void DisplayUi::drawSettings(const DeviceState& state, const AutoControlSettings& autoSettings) {
    if (fullRedraw) {
        tft.drawRoundRect(8, 28, 304, 182, 6, COLOR_ACCENT);
        autoPageCacheValid = false;
    }

    (void)state;
    drawAutoSettingsList(autoSettings);
}

void DisplayUi::drawAutoSettingsList(const AutoControlSettings& autoSettings) {
    if (selectedAutoSetting >= AUTO_SETTINGS_COUNT) {
        selectedAutoSetting = AUTO_SETTINGS_COUNT - 1;
    }

    if (selectedAutoSetting < autoSettingsScroll) {
        autoSettingsScroll = selectedAutoSetting;
    } else if (selectedAutoSetting >= autoSettingsScroll + AUTO_VISIBLE_ROWS) {
        autoSettingsScroll = selectedAutoSetting - AUTO_VISIBLE_ROWS + 1;
    }

    const AutoSettingDescriptor& selected = AUTO_SETTINGS[selectedAutoSetting];
    const bool editMode = interactionMode == InteractionMode::Edit;
    const bool rangeChanged = !autoPageCacheValid || lastAutoVisibleStart != autoSettingsScroll;
    const bool selectionChanged = !autoPageCacheValid || lastAutoSelectedIndex != selectedAutoSetting || lastAutoEditMode != editMode;

    if (!autoPageCacheValid || strncmp(lastAutoGroup, selected.group, sizeof(lastAutoGroup)) != 0) {
        strncpy(lastAutoGroup, selected.group, sizeof(lastAutoGroup));
        lastAutoGroup[sizeof(lastAutoGroup) - 1] = '\0';
        tft.fillRect(14, 32, 292, 18, COLOR_BG);
        tft.setFreeFont(nullptr);
        tft.setTextFont(2);
        tft.setTextSize(1);
        tft.setTextColor(COLOR_TITLE, COLOR_BG);
        tft.drawString(selected.group, 18, 32);
    }

    if (rangeChanged || selectionChanged) {
        tft.fillRect(258, 32, 48, 18, COLOR_BG);
        tft.setTextFont(2);
        tft.setTextColor(COLOR_WARN, COLOR_BG);
        tft.drawString(String(selectedAutoSetting + 1) + "/" + String(AUTO_SETTINGS_COUNT), 262, 32);
    }

    for (uint8_t row = 0; row < AUTO_VISIBLE_ROWS; row++) {
        const uint8_t index = autoSettingsScroll + row;
        const int16_t y = 52 + row * 18;

        if (index >= AUTO_SETTINGS_COUNT) {
            if (!autoPageCacheValid || lastAutoRowIndex[row] != 255) {
                tft.fillRect(12, y - 2, 296, 18, COLOR_BG);
                lastAutoRowIndex[row] = 255;
                lastAutoRowValue[row] = "";
                lastAutoRowSelected[row] = false;
                lastAutoRowEdit[row] = false;
            }
            continue;
        }

        const AutoSettingDescriptor& descriptor = AUTO_SETTINGS[index];
        const bool selectedRow = interactionMode != InteractionMode::View && index == selectedAutoSetting;
        const bool editRow = selectedRow && interactionMode == InteractionMode::Edit;
        const uint16_t markerColor = editRow ? COLOR_TITLE : (selectedRow ? COLOR_ACCENT : COLOR_MUTED);

        const String value = editRow
            ? (descriptor.type == AutoSettingType::Bool
                ? (autoEditValue >= 0.5f ? "ON" : "OFF")
                : (descriptor.type == AutoSettingType::UInt || descriptor.type == AutoSettingType::Milliseconds
                    ? String((unsigned long)roundf(autoEditValue)) + (descriptor.unit[0] ? String(" ") + descriptor.unit : String(""))
                    : String(autoEditValue, static_cast<unsigned int>(descriptor.decimals)) + (descriptor.unit[0] ? String(" ") + descriptor.unit : String(""))))
            : formatAutoSettingValue(autoSettings, descriptor);

        const bool rowChanged = rangeChanged
            || !autoPageCacheValid
            || lastAutoRowIndex[row] != index
            || lastAutoRowSelected[row] != selectedRow
            || lastAutoRowEdit[row] != editRow
            || lastAutoRowValue[row] != value;
        if (!rowChanged) {
            continue;
        }

        lastAutoRowIndex[row] = index;
        lastAutoRowSelected[row] = selectedRow;
        lastAutoRowEdit[row] = editRow;
        lastAutoRowValue[row] = value;

        tft.fillRect(12, y - 2, 296, 18, COLOR_BG);

        if (selectedRow) {
            tft.drawRect(12, y - 2, 296, 18, markerColor);
            tft.fillTriangle(18, y + 4, 18, y + 10, 24, y + 7, markerColor);
        }

        tft.setTextFont(1);
        tft.setTextColor(markerColor, COLOR_BG);
        tft.drawString(descriptor.group, 28, y + 2);

        tft.setTextFont(2);
        tft.setTextColor(selectedRow ? COLOR_TEXT : COLOR_MUTED, COLOR_BG);
        tft.drawString(descriptor.label, 82, y);

        tft.setTextColor(editRow ? COLOR_TITLE : COLOR_OK, COLOR_BG);
        tft.drawRightString(value, 302, y, 2);
    }

    const char* hint = interactionMode == InteractionMode::Edit
        ? "LEFT/RIGHT change  OK apply  BACK cancel"
        : (interactionMode == InteractionMode::Select ? "LEFT/RIGHT select  OK edit  BACK exit" : "OK config  BACK overview");
    if (!autoPageCacheValid || strcmp(lastAutoHint, hint) != 0) {
        strncpy(lastAutoHint, hint, sizeof(lastAutoHint));
        lastAutoHint[sizeof(lastAutoHint) - 1] = '\0';
        tft.fillRect(14, 198, 292, 14, COLOR_BG);
        tft.setTextFont(1);
        tft.setTextColor(COLOR_WARN, COLOR_BG);
        tft.drawString(hint, 18, 201);
    }

    lastAutoVisibleStart = autoSettingsScroll;
    lastAutoSelectedIndex = selectedAutoSetting;
    lastAutoEditMode = editMode;
    autoPageCacheValid = true;
}

void DisplayUi::drawDiagnostics(const DeviceState& state) {
    if (fullRedraw) {
        drawPanel(10, 42, 300, 64, state.controllerState.errorCount == 0 ? COLOR_OK : COLOR_DANGER);
        drawPanel(10, 116, 300, 92, COLOR_WARN);
    }

    drawTextBox(0, 20, 52, 275, "DIAGNOSTICS", COLOR_MUTED, 1);
    drawTextBox(1, 20, 72, 275, "Warn " + String(state.controllerState.warningCount) + "  Err " + String(state.controllerState.errorCount), COLOR_TEXT, 2);
    drawTextBox(2, 20, 128, 275, "AC frame " + String(state.ac.hasReceivedFrame ? state.ac.lastFrameAgeMs : 0) + " ms", state.ac.hasReceivedFrame ? COLOR_TEXT : COLOR_WARN, 2);
    drawTextBox(3, 20, 154, 275, "VFD ok " + String(state.vfd.okCount) + "  err " + String(state.vfd.errorCount), COLOR_MUTED, 2);
}

void DisplayUi::drawPlaceholder(const char* title, const char* line1, const char* line2) {
    if (fullRedraw) {
        drawPanel(10, 56, 300, 118, COLOR_ACCENT);
    }

    drawTextBox(0, 24, 72, 260, title, COLOR_ACCENT, 2);
    drawTextBox(1, 24, 104, 260, line1, COLOR_TEXT, 2);
    drawTextBox(2, 24, 130, 260, line2, COLOR_MUTED, 2);
}

const char* DisplayUi::getPageName(Page page) const {
    switch (page) {
        case Page::Overview:
            return "Overview";
        case Page::AirConditioner:
            return "AC";
        case Page::Ventilation:
            return "Vent";
        case Page::Temperatures:
            return "Temp";
        case Page::Settings:
            return "Auto";
        case Page::Diagnostics:
            return "Diag";
        case Page::Count:
            return "?";
    }

    return "?";
}

uint16_t DisplayUi::statusColor(bool ok) const {
    return ok ? COLOR_OK : COLOR_WARN;
}

void DisplayUi::drawPanel(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    tft.fillRoundRect(x, y, w, h, 8, COLOR_CARD);
    tft.drawRoundRect(x, y, w, h, 8, color);
}

void DisplayUi::drawStatusDot(int16_t x, int16_t y, bool ok, const char* label) {
    const uint16_t color = ok ? COLOR_OK : COLOR_DANGER;
    tft.fillCircle(x, y, 5, color);
    tft.drawCircle(x, y, 6, COLOR_PANEL);
    tft.setTextFont(1);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_MUTED, COLOR_PANEL);
    tft.drawString(label, x - 12, y + 8);
}

void DisplayUi::drawWifiIcon(int16_t x, int16_t y, uint16_t color) {
    tft.fillRect(x - 14, y - 9, 24, 18, COLOR_PANEL);
    tft.drawLine(x - 10, y - 2, x - 6, y - 6, color);
    tft.drawFastHLine(x - 6, y - 6, 13, color);
    tft.drawLine(x + 6, y - 6, x + 10, y - 2, color);
    tft.drawLine(x - 7, y + 1, x - 4, y - 2, color);
    tft.drawFastHLine(x - 4, y - 2, 9, color);
    tft.drawLine(x + 4, y - 2, x + 7, y + 1, color);
    tft.drawLine(x - 4, y + 4, x - 2, y + 2, color);
    tft.drawFastHLine(x - 2, y + 2, 5, color);
    tft.drawLine(x + 2, y + 2, x + 4, y + 4, color);
    tft.fillCircle(x, y + 6, 2, color);
}

void DisplayUi::drawHomeAssistantIcon(int16_t x, int16_t y, uint16_t color) {
    tft.fillRect(x - 18, y - 10, 32, 18, COLOR_PANEL);
    tft.fillCircle(x - 9, y + 2, 6, color);
    tft.fillCircle(x - 1, y - 3, 8, color);
    tft.fillCircle(x + 8, y + 2, 6, color);
    tft.fillRoundRect(x - 14, y + 1, 28, 8, 3, color);
    tft.drawArc(x - 9, y + 2, 7, 5, 180, 330, COLOR_PANEL, color);
    tft.drawArc(x - 1, y - 3, 9, 7, 185, 330, COLOR_PANEL, color);
    tft.drawArc(x + 8, y + 2, 7, 5, 210, 20, COLOR_PANEL, color);
}

void DisplayUi::drawWarningIcon(int16_t x, int16_t y, uint8_t count) {
    tft.fillRect(x - 11, y - 10, 42, 20, COLOR_PANEL);
    tft.drawTriangle(x, y - 10, x - 10, y + 8, x + 10, y + 8, COLOR_WARN);
    tft.drawTriangle(x, y - 8, x - 8, y + 7, x + 8, y + 7, COLOR_WARN);
    tft.drawLine(x, y - 10, x - 11, y + 8, COLOR_WARN);
    tft.drawLine(x, y - 10, x + 11, y + 8, COLOR_WARN);
    tft.drawFastVLine(x, y - 4, 7, COLOR_WARN);
    tft.drawFastVLine(x + 1, y - 4, 7, COLOR_WARN);
    tft.fillCircle(x, y + 6, 1, COLOR_WARN);
    tft.fillCircle(x + 1, y + 6, 1, COLOR_WARN);
    tft.setFreeFont(&FreeMono9pt7b);
    tft.setTextColor(COLOR_WARN, COLOR_PANEL);
    tft.drawString(String(count), x + 14, y - 7);
}

void DisplayUi::drawErrorIcon(int16_t x, int16_t y, uint8_t count) {
    tft.fillRect(x - 11, y - 10, 42, 20, COLOR_PANEL);
    tft.drawTriangle(x, y - 10, x - 10, y + 8, x + 10, y + 8, COLOR_DANGER);
    tft.drawTriangle(x, y - 8, x - 8, y + 7, x + 8, y + 7, COLOR_DANGER);
    tft.drawLine(x, y - 10, x - 11, y + 8, COLOR_DANGER);
    tft.drawLine(x, y - 10, x + 11, y + 8, COLOR_DANGER);
    tft.drawLine(x - 4, y - 2, x + 4, y + 6, COLOR_DANGER);
    tft.drawLine(x - 3, y - 2, x + 5, y + 6, COLOR_DANGER);
    tft.drawLine(x + 4, y - 2, x - 4, y + 6, COLOR_DANGER);
    tft.drawLine(x + 5, y - 2, x - 3, y + 6, COLOR_DANGER);
    tft.setFreeFont(&FreeMono9pt7b);
    tft.setTextColor(COLOR_DANGER, COLOR_PANEL);
    tft.drawString(String(count), x + 14, y - 7);
}

void DisplayUi::drawActivityIcon(int16_t x, int16_t y, ControllerActivity activity) {
    tft.fillRect(x - 12, y - 12, 24, 24, COLOR_BG);

    switch (activity) {
        case ControllerActivity::Start:
            tft.drawArc(x, y, 8, 6, 35, 325, COLOR_WARN, COLOR_BG);
            tft.drawFastVLine(x, y - 10, 8, COLOR_WARN);
            tft.drawFastVLine(x + 1, y - 10, 8, COLOR_WARN);
            break;
        case ControllerActivity::Normal:
            drawCheckIcon(x, y, COLOR_OK);
            break;
        case ControllerActivity::VentCool:
            tft.fillCircle(x, y, 2, COLOR_ACCENT);
            tft.fillTriangle(x, y - 2, x - 8, y - 8, x - 5, y + 1, COLOR_ACCENT);
            tft.fillTriangle(x + 2, y, x + 8, y - 8, x + 1, y - 5, COLOR_ACCENT);
            tft.fillTriangle(x, y + 2, x + 8, y + 8, x + 5, y - 1, COLOR_ACCENT);
            tft.fillTriangle(x - 2, y, x - 8, y + 8, x - 1, y + 5, COLOR_ACCENT);
            break;
        case ControllerActivity::AcCool:
            drawSnowflakeIcon(x, y, COLOR_ACCENT);
            break;
        case ControllerActivity::Heat:
            drawHeatIcon(x, y, COLOR_DANGER);
            break;
        case ControllerActivity::Vent:
            tft.fillCircle(x, y, 2, COLOR_MUTED);
            tft.fillTriangle(x, y - 2, x - 8, y - 8, x - 5, y + 1, COLOR_MUTED);
            tft.fillTriangle(x + 2, y, x + 8, y - 8, x + 1, y - 5, COLOR_MUTED);
            tft.fillTriangle(x, y + 2, x + 8, y + 8, x + 5, y - 1, COLOR_MUTED);
            tft.fillTriangle(x - 2, y, x - 8, y + 8, x - 1, y + 5, COLOR_MUTED);
            break;
        case ControllerActivity::Error:
            tft.drawLine(x - 6, y - 6, x + 6, y + 6, COLOR_DANGER);
            tft.drawLine(x + 6, y - 6, x - 6, y + 6, COLOR_DANGER);
            break;
        case ControllerActivity::Hold:
            tft.drawFastHLine(x - 6, y - 3, 12, COLOR_WARN);
            tft.drawFastHLine(x - 6, y + 3, 12, COLOR_WARN);
            break;
        case ControllerActivity::Idle:
            drawEyeIcon(x, y, COLOR_WARN);
            break;
    }
}

void DisplayUi::drawCheckIcon(int16_t x, int16_t y, uint16_t color) {
    tft.drawLine(x - 7, y, x - 2, y + 5, color);
    tft.drawLine(x - 2, y + 5, x + 8, y - 6, color);
    tft.drawLine(x - 7, y + 1, x - 2, y + 6, color);
    tft.drawLine(x - 2, y + 6, x + 8, y - 5, color);
}

void DisplayUi::drawSnowflakeIcon(int16_t x, int16_t y, uint16_t color) {
    tft.drawFastHLine(x - 8, y, 17, color);
    tft.drawFastVLine(x, y - 8, 17, color);
    tft.drawLine(x - 6, y - 6, x + 6, y + 6, color);
    tft.drawLine(x + 6, y - 6, x - 6, y + 6, color);
    tft.drawLine(x - 8, y, x - 5, y - 3, color);
    tft.drawLine(x - 8, y, x - 5, y + 3, color);
    tft.drawLine(x + 8, y, x + 5, y - 3, color);
    tft.drawLine(x + 8, y, x + 5, y + 3, color);
    tft.drawLine(x, y - 8, x - 3, y - 5, color);
    tft.drawLine(x, y - 8, x + 3, y - 5, color);
    tft.drawLine(x, y + 8, x - 3, y + 5, color);
    tft.drawLine(x, y + 8, x + 3, y + 5, color);
}

void DisplayUi::drawHeatIcon(int16_t x, int16_t y, uint16_t color) {
    for (int8_t row = -5; row <= 5; row += 5) {
        tft.drawLine(x - 8, y + row, x - 5, y + row - 2, color);
        tft.drawLine(x - 5, y + row - 2, x - 2, y + row, color);
        tft.drawLine(x - 2, y + row, x + 1, y + row + 2, color);
        tft.drawLine(x + 1, y + row + 2, x + 4, y + row, color);
        tft.drawLine(x + 4, y + row, x + 8, y + row - 2, color);
        tft.drawLine(x - 8, y + row + 1, x - 5, y + row - 1, color);
        tft.drawLine(x - 5, y + row - 1, x - 2, y + row + 1, color);
        tft.drawLine(x - 2, y + row + 1, x + 1, y + row + 3, color);
        tft.drawLine(x + 1, y + row + 3, x + 4, y + row + 1, color);
        tft.drawLine(x + 4, y + row + 1, x + 8, y + row - 1, color);
    }
}

void DisplayUi::drawEyeIcon(int16_t x, int16_t y, uint16_t color) {
    tft.drawLine(x - 10, y, x - 6, y - 4, color);
    tft.drawLine(x - 6, y - 4, x, y - 6, color);
    tft.drawLine(x, y - 6, x + 6, y - 4, color);
    tft.drawLine(x + 6, y - 4, x + 10, y, color);
    tft.drawLine(x - 10, y, x - 6, y + 4, color);
    tft.drawLine(x - 6, y + 4, x, y + 6, color);
    tft.drawLine(x, y + 6, x + 6, y + 4, color);
    tft.drawLine(x + 6, y + 4, x + 10, y, color);
    tft.fillCircle(x, y, 2, color);
}

void DisplayUi::drawOverviewSelection(const DeviceState& state) {
    if (interactionMode == InteractionMode::View) {
        return;
    }

    if (!isOverviewParamAvailable(state, selectedParam)) {
        return;
    }

    drawParamFrame(selectedParam, interactionMode == InteractionMode::Edit ? COLOR_TITLE : COLOR_ACCENT);
}

void DisplayUi::drawParamFrame(OverviewParam param, uint16_t color) {
    switch (param) {
        case OverviewParam::Mode:
            tft.drawRect(76, 30, 78, 34, color);
            break;
        case OverviewParam::SetTemp:
            tft.drawRect(196, 94, 54, 22, color);
            break;
        case OverviewParam::AcPower:
            tft.drawRect(66, 148, 42, 22, color);
            break;
        case OverviewParam::AcMode:
            tft.drawRect(60, 168, 88, 22, color);
            break;
        case OverviewParam::AcTemp:
            tft.drawRect(40, 190, 40, 22, color);
            break;
        case OverviewParam::AcFan:
            tft.drawRect(108, 190, 46, 22, color);
            break;
        case OverviewParam::VfdPower:
            tft.drawRect(222, 148, 44, 22, color);
            break;
        case OverviewParam::VfdStep:
            tft.drawRect(210, 168, 44, 22, color);
            break;
        case OverviewParam::Count:
            break;
    }
}

void DisplayUi::enterSelectMode(DeviceState& state) {
    selectedParam = firstAvailableOverviewParam(state);
    interactionMode = InteractionMode::Select;
    dirty = true;
    fullRedraw = true;
    Logger::debugf(TAG_UI, "CONFIG mode: selected %s", overviewParamName(selectedParam));
}

void DisplayUi::enterEditMode(const DeviceState& state) {
    if (!isOverviewParamAvailable(state, selectedParam)) {
        Logger::warningf(TAG_UI, "Parameter %s is not available in current mode", overviewParamName(selectedParam));
        return;
    }

    switch (selectedParam) {
        case OverviewParam::Mode:
            editValue = state.controllerState.mode == DeviceMode::Manual ? 1 : (state.controllerState.mode == DeviceMode::Disabled ? 2 : 0);
            break;
        case OverviewParam::SetTemp:
            editValue = (int16_t)(state.environment.targetIndoorTempC * 2.0f + 0.5f);
            break;
        case OverviewParam::AcPower:
            editValue = state.ac.powerOn ? 1 : 0;
            break;
        case OverviewParam::AcMode:
            editValue = acModeListIndex(state.ac.mode);
            break;
        case OverviewParam::AcTemp:
            editValue = state.ac.temperature < 16 ? 22 : state.ac.temperature;
            break;
        case OverviewParam::AcFan:
            editValue = state.ac.fanMode > 4 ? 0 : state.ac.fanMode;
            break;
        case OverviewParam::VfdPower:
            editValue = state.settings.manualVfdPower ? 1 : 0;
            break;
        case OverviewParam::VfdStep:
            editValue = state.settings.manualVfdStep;
            break;
        case OverviewParam::Count:
            editValue = 0;
            break;
    }

    interactionMode = InteractionMode::Edit;
    dirty = true;
    fullRedraw = true;
    Logger::debugf(TAG_UI, "EDIT mode: %s", overviewParamName(selectedParam));
}

void DisplayUi::cancelEdit(DeviceState& state) {
    (void)state;
    interactionMode = InteractionMode::Select;
    dirty = true;
    fullRedraw = true;
    Logger::debugf(TAG_UI, "Edit canceled: %s", overviewParamName(selectedParam));
}

void DisplayUi::moveSelection(const DeviceState& state, int8_t direction) {
    uint8_t index = static_cast<uint8_t>(selectedParam);

    for (uint8_t i = 0; i < static_cast<uint8_t>(OverviewParam::Count); i++) {
        index = (index + static_cast<uint8_t>(OverviewParam::Count) + direction) % static_cast<uint8_t>(OverviewParam::Count);
        const OverviewParam candidate = static_cast<OverviewParam>(index);
        if (isOverviewParamAvailable(state, candidate)) {
            selectedParam = candidate;
            dirty = true;
            fullRedraw = true;
            Logger::debugf(TAG_UI, "Selected %s", overviewParamName(selectedParam));
            return;
        }
    }
}

void DisplayUi::changeEditValue(int8_t direction) {
    switch (selectedParam) {
        case OverviewParam::Mode:
            editValue = (editValue + 3 + direction) % 3;
            break;
        case OverviewParam::SetTemp:
            editValue += direction;
            if (editValue < 32) editValue = 60;
            if (editValue > 60) editValue = 32;
            break;
        case OverviewParam::AcPower:
        case OverviewParam::VfdPower:
            editValue = editValue == 0 ? 1 : 0;
            break;
        case OverviewParam::AcMode:
            editValue = (editValue + 4 + direction) % 4;
            break;
        case OverviewParam::AcTemp:
            editValue += direction;
            if (editValue < 16) editValue = 30;
            if (editValue > 30) editValue = 16;
            break;
        case OverviewParam::AcFan:
            editValue = (editValue + 5 + direction) % 5;
            break;
        case OverviewParam::VfdStep:
            editValue += direction;
            if (editValue < 0) editValue = 6;
            if (editValue > 6) editValue = 0;
            break;
        case OverviewParam::Count:
            break;
    }

    dirty = true;
    fullRedraw = true;
    Logger::debugf(TAG_UI, "Edit value changed: %s", overviewParamName(selectedParam));
}

DisplayUi::Action DisplayUi::applyEdit(DeviceState& state) {
    Action action;

    if (!isOverviewParamAvailable(state, selectedParam)) {
        Logger::warningf(TAG_UI, "Apply rejected: %s is not available", overviewParamName(selectedParam));
        interactionMode = InteractionMode::Select;
        dirty = true;
        fullRedraw = true;
        return action;
    }

    const bool manualMode = state.controllerState.mode == DeviceMode::Manual;

    switch (selectedParam) {
        case OverviewParam::Mode: {
            const DeviceMode oldMode = state.controllerState.mode;
            const DeviceMode newMode = editValue == 0 ? DeviceMode::Auto : (editValue == 1 ? DeviceMode::Manual : DeviceMode::Disabled);
            state.controllerState.mode = newMode;
            state.settings.mode = newMode;
            Logger::infof(TAG_UI, "Mode changed: %s -> %s", deviceModeName(oldMode), deviceModeName(newMode));
            action.settingsChanged = true;
            break;
        }
        case OverviewParam::SetTemp: {
            const float oldValue = state.environment.targetIndoorTempC;
            const float newValue = editValue / 2.0f;
            state.environment.targetIndoorTempC = newValue;
            state.settings.targetIndoorTempC = newValue;
            Logger::infof(TAG_UI, "Set temp changed: %.1f -> %.1f C", oldValue, newValue);
            action.settingsChanged = true;
            break;
        }
        case OverviewParam::AcPower:
            state.settings.manualAcPower = editValue != 0;
            Logger::infof(TAG_UI, "AC power setting: %s", state.settings.manualAcPower ? "ON" : "OFF");
            action.settingsChanged = true;
            if (manualMode) {
                action.type = ActionType::AcPower;
                action.boolValue = state.settings.manualAcPower;
            }
            break;
        case OverviewParam::AcMode:
            setAcModeTemperature(state.settings, state.settings.manualAcMode, state.settings.manualAcTemperature);
            state.settings.manualAcMode = acModeFromListIndex((uint8_t)editValue);
            state.settings.manualAcTemperature = getAcModeTemperature(state.settings, state.settings.manualAcMode);
            Logger::infof(TAG_UI, "AC mode setting: %s", acModeTitle(state.settings.manualAcMode));
            action.settingsChanged = true;
            if (manualMode) {
                action.type = ActionType::AcMode;
                action.uintValue = state.settings.manualAcMode;
            }
            break;
        case OverviewParam::AcTemp:
            state.settings.manualAcTemperature = (uint8_t)editValue;
            setAcModeTemperature(state.settings, state.settings.manualAcMode, state.settings.manualAcTemperature);
            Logger::infof(TAG_UI, "AC temp setting: %u C", state.settings.manualAcTemperature);
            action.settingsChanged = true;
            if (manualMode) {
                action.type = ActionType::AcTemperature;
                action.uintValue = state.settings.manualAcTemperature;
            }
            break;
        case OverviewParam::AcFan:
            state.settings.manualAcFanMode = (uint8_t)editValue;
            Logger::infof(TAG_UI, "AC fan setting: %s", acFanTitle(state.settings.manualAcFanMode));
            action.settingsChanged = true;
            if (manualMode) {
                action.type = ActionType::AcFan;
                action.uintValue = state.settings.manualAcFanMode;
            }
            break;
        case OverviewParam::VfdPower:
            state.settings.manualVfdPower = editValue != 0;
            if (state.settings.manualVfdPower && state.settings.manualVfdStep == 0) {
                state.settings.manualVfdStep = 1;
            }
            Logger::infof(TAG_UI, "VFD power setting: %s", state.settings.manualVfdPower ? "ON" : "OFF");
            action.settingsChanged = true;
            if (manualMode) {
                if (!state.settings.manualVfdPower) {
                    action.type = ActionType::VfdStop;
                } else {
                    action.type = ActionType::VfdForward;
                }
            }
            break;
        case OverviewParam::VfdStep:
            state.settings.manualVfdStep = (uint8_t)editValue;
            if (state.settings.manualVfdStep == 0) {
                state.settings.manualVfdPower = false;
            }
            Logger::infof(TAG_UI, "VFD step setting: %u", state.settings.manualVfdStep);
            action.settingsChanged = true;
            if (manualMode) {
                if (state.settings.manualVfdStep == 0) {
                    action.type = ActionType::VfdStop;
                } else {
                    action.type = ActionType::VfdSetFrequency;
                    action.uintValue = state.settings.manualVfdStep;
                    action.floatValue = vfdStepToHz(state.settings.manualVfdStep);
                }
            }
            break;
        case OverviewParam::Count:
            break;
    }

    interactionMode = InteractionMode::Select;
    dirty = true;
    fullRedraw = true;
    return action;
}

DisplayUi::Action DisplayUi::handleAutoSettingsButton(Button button, bool longPress, const AutoControlSettings& autoSettings) {
    Action action;

    if (interactionMode == InteractionMode::Select) {
        if (longPress) {
            return action;
        }

        switch (button) {
            case Button::Left:
                moveAutoSettingsSelection(-1);
                break;
            case Button::Right:
                moveAutoSettingsSelection(1);
                break;
            case Button::Ok:
                enterAutoSettingsEdit(autoSettings);
                break;
            case Button::Back:
                interactionMode = InteractionMode::View;
                dirty = true;
                fullRedraw = false;
                Logger::debug(TAG_UI, "Auto settings selection canceled");
                break;
        }
        return action;
    }

    if (interactionMode == InteractionMode::Edit) {
        switch (button) {
            case Button::Left:
                changeAutoSettingsValue(-1, longPress);
                break;
            case Button::Right:
                changeAutoSettingsValue(1, longPress);
                break;
            case Button::Ok:
                if (!longPress) {
                    return applyAutoSettingsEdit(autoSettings);
                }
                break;
            case Button::Back:
                if (!longPress) {
                    interactionMode = InteractionMode::Select;
                    dirty = true;
                    fullRedraw = false;
                    Logger::debug(TAG_UI, "Auto setting edit canceled");
                }
                break;
        }
        return action;
    }

    return action;
}

void DisplayUi::enterAutoSettingsSelect() {
    interactionMode = InteractionMode::Select;
    dirty = true;
    fullRedraw = false;
    Logger::debug(TAG_UI, "CONFIG mode: AUTO SETTINGS");
}

void DisplayUi::enterAutoSettingsEdit(const AutoControlSettings& autoSettings) {
    if (selectedAutoSetting >= AUTO_SETTINGS_COUNT) {
        selectedAutoSetting = 0;
    }

    const AutoSettingDescriptor& descriptor = AUTO_SETTINGS[selectedAutoSetting];
    autoEditValue = getAutoSettingValue(autoSettings, descriptor.id);
    interactionMode = InteractionMode::Edit;
    dirty = true;
    fullRedraw = false;
    Logger::debugf(TAG_UI, "EDIT auto setting: %s", descriptor.label);
}

void DisplayUi::moveAutoSettingsSelection(int8_t direction) {
    int16_t next = (int16_t)selectedAutoSetting + direction;
    if (next < 0) {
        next = AUTO_SETTINGS_COUNT - 1;
    } else if (next >= AUTO_SETTINGS_COUNT) {
        next = 0;
    }

    selectedAutoSetting = (uint8_t)next;
    dirty = true;
    fullRedraw = false;
    Logger::debugf(TAG_UI, "Selected auto setting: %s", AUTO_SETTINGS[selectedAutoSetting].label);
}

void DisplayUi::changeAutoSettingsValue(int8_t direction, bool fast) {
    if (selectedAutoSetting >= AUTO_SETTINGS_COUNT) {
        return;
    }

    const AutoSettingDescriptor& descriptor = AUTO_SETTINGS[selectedAutoSetting];
    if (descriptor.type == AutoSettingType::Bool) {
        autoEditValue = autoEditValue >= 0.5f ? 0.0f : 1.0f;
    } else {
        const float step = fast ? descriptor.fastStep : descriptor.step;
        autoEditValue = clampAutoValue(autoEditValue + direction * step, descriptor);
    }

    dirty = true;
    fullRedraw = false;
    Logger::debugf(TAG_UI, "Auto setting edit value changed: %s", descriptor.label);
}

DisplayUi::Action DisplayUi::applyAutoSettingsEdit(const AutoControlSettings& autoSettings) {
    Action action;
    if (selectedAutoSetting >= AUTO_SETTINGS_COUNT) {
        return action;
    }

    const AutoSettingDescriptor& descriptor = AUTO_SETTINGS[selectedAutoSetting];
    AutoControlSettings updated = autoSettings;
    const float oldValue = getAutoSettingValue(updated, descriptor.id);
    const float newValue = descriptor.type == AutoSettingType::Bool
        ? (autoEditValue >= 0.5f ? 1.0f : 0.0f)
        : clampAutoValue(autoEditValue, descriptor);

    if (fabsf(oldValue - newValue) > 0.001f) {
        setAutoSettingValue(updated, descriptor.id, newValue);
        action.type = ActionType::AutoSettings;
        action.autoSettings = updated;
        Logger::infof(TAG_UI, "Auto setting applied: %s", descriptor.label);
    }

    interactionMode = InteractionMode::Select;
    dirty = true;
    fullRedraw = false;
    return action;
}

DisplayUi::Action DisplayUi::handleTemperatureButton(Button button, bool longPress, const TemperatureStateSnapshot& temperatures) {
    Action action;

    if (longPress) {
        return action;
    }

    const uint8_t count = temperatures.sensorCount;
    if (count == 0) {
        switch (button) {
            case Button::Left:
                previousPage();
                Logger::debug(TAG_UI, "TEMP VIEW: page previous");
                break;
            case Button::Right:
                nextPage();
                Logger::debug(TAG_UI, "TEMP VIEW: page next");
                break;
            case Button::Ok:
                action.type = ActionType::TempScan;
                tempPageMode = TempPageMode::View;
                tempPageCacheValid = false;
                dirty = true;
                fullRedraw = false;
                Logger::info(TAG_UI, "Temperature scan requested from display");
                break;
            case Button::Back:
                currentPage = Page::Overview;
                tempPageMode = TempPageMode::View;
                dirty = true;
                fullRedraw = true;
                shellRedraw = true;
                Logger::debug(TAG_UI, "TEMP BACK: return to Overview");
                break;
        }
        return action;
    }

    if (selectedTempSensor >= count) {
        selectedTempSensor = count - 1;
    }

    switch (tempPageMode) {
        case TempPageMode::View:
            switch (button) {
                case Button::Left:
                    previousPage();
                    Logger::debug(TAG_UI, "TEMP VIEW: page previous");
                    break;
                case Button::Right:
                    nextPage();
                    Logger::debug(TAG_UI, "TEMP VIEW: page next");
                    break;
                case Button::Ok:
                    tempPageMode = TempPageMode::Config;
                    tempPageCacheValid = false;
                    dirty = true;
                    fullRedraw = false;
                    Logger::debug(TAG_UI, "TEMP CONFIG mode entered");
                    break;
                case Button::Back:
                    currentPage = Page::Overview;
                    dirty = true;
                    fullRedraw = true;
                    shellRedraw = true;
                    Logger::debug(TAG_UI, "TEMP VIEW BACK: return to Overview");
                    break;
            }
            break;

        case TempPageMode::Config:
            switch (button) {
                case Button::Left:
                    selectedTempSensor = selectedTempSensor == 0 ? count - 1 : selectedTempSensor - 1;
                    dirty = true;
                    fullRedraw = false;
                    Logger::debugf(TAG_UI, "Selected temp sensor: %u", selectedTempSensor);
                    break;
                case Button::Right:
                    selectedTempSensor = (selectedTempSensor + 1) % count;
                    dirty = true;
                    fullRedraw = false;
                    Logger::debugf(TAG_UI, "Selected temp sensor: %u", selectedTempSensor);
                    break;
                case Button::Ok:
                    tempPageMode = TempPageMode::Menu;
                    selectedTempMenu = 0;
                    tempPageCacheValid = false;
                    dirty = true;
                    fullRedraw = false;
                    Logger::debug(TAG_UI, "TEMP menu opened");
                    break;
                case Button::Back:
                    tempPageMode = TempPageMode::View;
                    tempPageCacheValid = false;
                    dirty = true;
                    fullRedraw = false;
                    Logger::debug(TAG_UI, "TEMP CONFIG mode canceled");
                    break;
            }
            break;

        case TempPageMode::Menu:
            switch (button) {
                case Button::Left:
                    selectedTempMenu = selectedTempMenu == 0 ? 5 : selectedTempMenu - 1;
                    dirty = true;
                    fullRedraw = false;
                    break;
                case Button::Right:
                    selectedTempMenu = (selectedTempMenu + 1) % 6;
                    dirty = true;
                    fullRedraw = false;
                    break;
                case Button::Ok:
                    return applyTemperatureMenuAction(temperatures);
                case Button::Back:
                    tempPageMode = TempPageMode::Config;
                    tempPageCacheValid = false;
                    dirty = true;
                    fullRedraw = false;
                    break;
            }
            break;

        case TempPageMode::EditRole:
            switch (button) {
                case Button::Left:
                    selectedTempRole = selectedTempRole == 0 ? 3 : selectedTempRole - 1;
                    dirty = true;
                    fullRedraw = false;
                    break;
                case Button::Right:
                    selectedTempRole = (selectedTempRole + 1) % 4;
                    dirty = true;
                    fullRedraw = false;
                    break;
                case Button::Ok:
                    return applyTemperatureRole(temperatures);
                case Button::Back:
                    tempPageMode = TempPageMode::Menu;
                    tempPageCacheValid = false;
                    dirty = true;
                    fullRedraw = false;
                    Logger::debug(TAG_UI, "TEMP role edit canceled");
                    break;
            }
            break;

        case TempPageMode::ConfirmForget:
            switch (button) {
                case Button::Ok:
                    action.type = ActionType::TempForget;
                    action.uintValue = selectedTempSensor;
                    tempPageMode = TempPageMode::Config;
                    tempPageCacheValid = false;
                    dirty = true;
                    fullRedraw = false;
                    Logger::infof(TAG_UI, "Temperature sensor forget requested: %u", selectedTempSensor);
                    break;
                case Button::Back:
                    tempPageMode = TempPageMode::Menu;
                    tempPageCacheValid = false;
                    dirty = true;
                    fullRedraw = false;
                    break;
                case Button::Left:
                case Button::Right:
                    break;
            }
            break;
    }

    return action;
}

DisplayUi::Action DisplayUi::applyTemperatureMenuAction(const TemperatureStateSnapshot& temperatures) {
    Action action;
    if (temperatures.sensorCount == 0) {
        return action;
    }

    switch (selectedTempMenu) {
        case 0:
            selectedTempRole = tempRoleToIndex(temperatures.sensors[selectedTempSensor].role);
            tempPageMode = TempPageMode::EditRole;
            tempPageCacheValid = false;
            dirty = true;
            fullRedraw = false;
            Logger::debug(TAG_UI, "TEMP role editor opened");
            break;
        case 1:
            action.type = ActionType::TempForceRead;
            tempPageMode = TempPageMode::Config;
            tempPageCacheValid = false;
            dirty = true;
            fullRedraw = false;
            Logger::info(TAG_UI, "Temperature force read requested from display");
            break;
        case 2:
            action.type = ActionType::TempScan;
            tempPageMode = TempPageMode::Config;
            tempPageCacheValid = false;
            dirty = true;
            fullRedraw = false;
            Logger::info(TAG_UI, "Temperature scan requested from display");
            break;
        case 3:
            action.type = ActionType::TempSwap;
            tempPageMode = TempPageMode::Config;
            tempPageCacheValid = false;
            dirty = true;
            fullRedraw = false;
            Logger::info(TAG_UI, "Temperature role swap requested from display");
            break;
        case 4:
            tempPageMode = TempPageMode::ConfirmForget;
            tempPageCacheValid = false;
            dirty = true;
            fullRedraw = false;
            break;
        case 5:
        default:
            tempPageMode = TempPageMode::Config;
            tempPageCacheValid = false;
            dirty = true;
            fullRedraw = false;
            break;
    }

    return action;
}

DisplayUi::Action DisplayUi::applyTemperatureRole(const TemperatureStateSnapshot& temperatures) {
    Action action;
    if (temperatures.sensorCount == 0 || selectedTempSensor >= temperatures.sensorCount) {
        return action;
    }

    const TempSensorRole role = tempRoleFromIndex(selectedTempRole);
    action.type = ActionType::TempAssignRole;
    action.uintValue = selectedTempSensor;
    action.tempRole = role;
    tempPageMode = TempPageMode::Config;
    tempPageCacheValid = false;
    dirty = true;
    fullRedraw = false;
    Logger::infof(TAG_UI, "Temperature role selected from display: sensor=%u role=%s", selectedTempSensor, tempRoleTitle(role));
    return action;
}

bool DisplayUi::isOverviewParamAvailable(const DeviceState& state, OverviewParam param) const {
    if (param == OverviewParam::Mode || param == OverviewParam::SetTemp) {
        return true;
    }

    return state.controllerState.mode == DeviceMode::Manual;
}

DisplayUi::OverviewParam DisplayUi::firstAvailableOverviewParam(const DeviceState& state) const {
    for (uint8_t i = 0; i < static_cast<uint8_t>(OverviewParam::Count); i++) {
        const OverviewParam param = static_cast<OverviewParam>(i);
        if (isOverviewParamAvailable(state, param)) {
            return param;
        }
    }

    return OverviewParam::Mode;
}

uint8_t DisplayUi::acModeListIndex(uint8_t mode) const {
    switch (mode) {
        case 3:
            return 1;
        case 4:
            return 2;
        case 1:
            return 3;
        case 5:
        default:
            return 0;
    }
}

uint8_t DisplayUi::acModeFromListIndex(uint8_t index) const {
    switch (index % 4) {
        case 1:
            return 3;
        case 2:
            return 4;
        case 3:
            return 1;
        case 0:
        default:
            return 5;
    }
}

float DisplayUi::vfdStepToHz(uint8_t step) const {
    if (step == 0) {
        return 0.0f;
    }

    if (step >= 6) {
        return 50.0f;
    }

    return 20.0f + (step - 1) * 6.0f;
}

const char* DisplayUi::overviewParamName(OverviewParam param) const {
    switch (param) {
        case OverviewParam::Mode:
            return "MODE";
        case OverviewParam::SetTemp:
            return "SET TEMP";
        case OverviewParam::AcPower:
            return "AC POWER";
        case OverviewParam::AcMode:
            return "AC MODE";
        case OverviewParam::AcTemp:
            return "AC TEMP";
        case OverviewParam::AcFan:
            return "AC FAN";
        case OverviewParam::VfdPower:
            return "VFD POWER";
        case OverviewParam::VfdStep:
            return "VFD STEP";
        case OverviewParam::Count:
            return "?";
    }

    return "?";
}

const char* DisplayUi::interactionLabel() const {
    switch (interactionMode) {
        case InteractionMode::Select:
            return "CONFIG";
        case InteractionMode::Edit:
            return "EDIT";
        case InteractionMode::View:
            return "";
    }

    return "";
}

const char* DisplayUi::acModeName(uint8_t mode) const {
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

const char* DisplayUi::acModeTitle(uint8_t mode) const {
    switch (mode) {
        case 1:
            return "Fan";
        case 2:
            return "Dry";
        case 3:
            return "Cool";
        case 4:
            return "Heat";
        case 5:
            return "Auto";
        default:
            return "Unk";
    }
}

const char* DisplayUi::acFanName(uint8_t fanMode) const {
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

const char* DisplayUi::acFanTitle(uint8_t fanMode) const {
    switch (fanMode) {
        case 0:
            return "Auto";
        case 1:
            return "Low";
        case 2:
            return "Mid";
        case 3:
            return "High";
        case 4:
            return "Max";
        default:
            return "Unk";
    }
}

const char* DisplayUi::tempRoleShort(TempSensorRole role) const {
    switch (role) {
        case TempSensorRole::Indoor:
            return "IN";
        case TempSensorRole::Outdoor:
            return "OUT";
        case TempSensorRole::Unused:
            return "OFF";
        case TempSensorRole::Unknown:
        default:
            return "UNK";
    }
}

const char* DisplayUi::tempRoleTitle(TempSensorRole role) const {
    switch (role) {
        case TempSensorRole::Indoor:
            return "Indoor";
        case TempSensorRole::Outdoor:
            return "Outdoor";
        case TempSensorRole::Unused:
            return "Unused";
        case TempSensorRole::Unknown:
        default:
            return "Unknown";
    }
}

String DisplayUi::tempShortAddress(const DeviceAddress& address) const {
    char buffer[16];
    snprintf(
        buffer,
        sizeof(buffer),
        "%02X%02X...%02X%02X",
        address[0],
        address[1],
        address[6],
        address[7]
    );
    return String(buffer);
}

const char* DisplayUi::vfdRunName(const char* lastAction) const {
    if (strcmp(lastAction, "forward") == 0) {
        return "fwd";
    }
    if (strcmp(lastAction, "reverse") == 0) {
        return "rev";
    }
    if (strcmp(lastAction, "stop") == 0) {
        return "stop";
    }

    return "idle";
}

const char* DisplayUi::deviceModeName(DeviceMode mode) const {
    switch (mode) {
        case DeviceMode::Auto:
            return "AUTO";
        case DeviceMode::Manual:
            return "MAN";
        case DeviceMode::Safe:
            return "SAFE";
        case DeviceMode::Disabled:
            return "OFF";
    }

    return "?";
}

const char* DisplayUi::activityName(ControllerActivity activity) const {
    switch (activity) {
        case ControllerActivity::Start:
            return "Start";
        case ControllerActivity::Normal:
            return "Normal";
        case ControllerActivity::VentCool:
            return "Vent cool";
        case ControllerActivity::AcCool:
            return "AC cool";
        case ControllerActivity::Heat:
            return "Heat";
        case ControllerActivity::Vent:
            return "Vent";
        case ControllerActivity::Error:
            return "Error";
        case ControllerActivity::Hold:
            return "StandBy";
        case ControllerActivity::Idle:
            return "Monitor";
    }

    return "?";
}

uint16_t DisplayUi::deviceModeColor(DeviceMode mode) const {
    switch (mode) {
        case DeviceMode::Auto:
        case DeviceMode::Manual:
            return COLOR_OK;
        case DeviceMode::Safe:
            return COLOR_WARN;
        case DeviceMode::Disabled:
            return COLOR_DANGER;
    }

    return COLOR_TEXT;
}

uint16_t DisplayUi::activityColor(ControllerActivity activity) const {
    switch (activity) {
        case ControllerActivity::Start:
            return COLOR_WARN;
        case ControllerActivity::Normal:
            return COLOR_OK;
        case ControllerActivity::VentCool:
        case ControllerActivity::AcCool:
            return COLOR_ACCENT;
        case ControllerActivity::Heat:
            return COLOR_DANGER;
        case ControllerActivity::Vent:
            return COLOR_TEXT;
        case ControllerActivity::Error:
            return COLOR_DANGER;
        case ControllerActivity::Hold:
            return COLOR_MUTED;
        case ControllerActivity::Idle:
            return COLOR_MUTED;
    }

    return COLOR_TEXT;
}

uint8_t DisplayUi::vfdStep(const VfdStateSnapshot& vfd) const {
    return vfd.hasActualFrequency ? vfd.actualStep : 0;
}
