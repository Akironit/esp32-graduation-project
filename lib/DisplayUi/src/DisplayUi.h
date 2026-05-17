#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <TFT_eSPI.h>

#include "DeviceState.h"

class DisplayUi {
public:
    enum class Page : uint8_t {
        Overview = 0,
        AirConditioner,
        Ventilation,
        Temperatures,
        Settings,
        Diagnostics,
        Count
    };

    enum class Button : uint8_t {
        Back,
        Left,
        Right,
        Ok
    };

    enum class ActionType : uint8_t {
        None,
        AcPower,
        AcMode,
        AcTemperature,
        AcFan,
        VfdStop,
        VfdForward,
        VfdSetFrequency
    };

    struct Action {
        ActionType type = ActionType::None;
        bool settingsChanged = false;
        bool boolValue = false;
        uint8_t uintValue = 0;
        float floatValue = 0.0f;
    };

    void begin();
    void update(const DeviceState& state);

    void nextPage();
    void previousPage();
    void setPage(Page page);

    bool isReady() const;
    uint8_t getPageIndex() const;
    const char* getPageName() const;
    Action handleButton(Button button, bool longPress, DeviceState& state);

private:
    enum class InteractionMode : uint8_t {
        View,
        Select,
        Edit
    };

    enum class OverviewParam : uint8_t {
        Mode = 0,
        SetTemp,
        AcPower,
        AcMode,
        AcTemp,
        AcFan,
        VfdPower,
        VfdStep,
        Count
    };

    TFT_eSPI tft;
    Page currentPage = Page::Overview;
    InteractionMode interactionMode = InteractionMode::View;
    OverviewParam selectedParam = OverviewParam::Mode;
    int16_t editValue = 0;
    bool ready = false;
    bool dirty = true;
    bool fullRedraw = true;
    bool lastHeaderWifiConnected = false;
    bool lastHeaderHaConnected = false;
    bool headerStatusCached = false;
    const char* lastInteractionLabel = nullptr;
    bool lastInteractionEdit = false;
    char lastFooterText[8] = "";
    char lastUptimeText[16] = "";
    uint8_t lastWarningCount = 255;
    uint8_t lastErrorCount = 255;
    unsigned long lastRenderMs = 0;
    static constexpr unsigned long RENDER_INTERVAL_MS = 1000;
    static constexpr uint8_t LINE_CACHE_SIZE = 40;
    String lineCache[LINE_CACHE_SIZE];
    uint16_t lineColorCache[LINE_CACHE_SIZE] = {};

    void render(const DeviceState& state);

    void drawHeader(const DeviceState& state, const char* title);
    void drawFooter(const DeviceState& state);
    void resetLineCache();
    void drawLine(uint8_t slot, int16_t x, int16_t y, const String& text, uint16_t color = TFT_WHITE, uint8_t font = 2);
    void drawTextBox(uint8_t slot, int16_t x, int16_t y, int16_t w, const String& text, uint16_t color = TFT_WHITE, uint8_t font = 2);
    void drawLabel(uint8_t slot, int16_t x, int16_t y, int16_t w, const String& text);
    void drawFreeTextBox(uint8_t slot, int16_t x, int16_t y, int16_t w, int16_t h, const String& text, const GFXfont* font, uint16_t color);
    void drawFontTextBox(uint8_t slot, int16_t x, int16_t y, int16_t w, int16_t h, const String& text, uint8_t font, uint16_t color);
    void drawBoldText(int16_t x, int16_t y, const String& text, uint16_t color, uint16_t bg, uint8_t font);
    String formatFloat(float value, uint8_t digits) const;
    void drawOverview(const DeviceState& state);
    void drawTemperatures(const TemperatureStateSnapshot& temperatures);
    void drawAirConditioner(const AcStateSnapshot& ac);
    void drawVentilation(const DeviceState& state);
    void drawSettings(const DeviceState& state);
    void drawDiagnostics(const DeviceState& state);
    void drawPlaceholder(const char* title, const char* line1, const char* line2);
    void drawPanel(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void drawStatusDot(int16_t x, int16_t y, bool ok, const char* label);
    void drawWifiIcon(int16_t x, int16_t y, uint16_t color);
    void drawHomeAssistantIcon(int16_t x, int16_t y, uint16_t color);
    void drawWarningIcon(int16_t x, int16_t y, uint8_t count);
    void drawErrorIcon(int16_t x, int16_t y, uint8_t count);
    void drawActivityIcon(int16_t x, int16_t y, ControllerActivity activity);
    void drawCheckIcon(int16_t x, int16_t y, uint16_t color);
    void drawSnowflakeIcon(int16_t x, int16_t y, uint16_t color);
    void drawHeatIcon(int16_t x, int16_t y, uint16_t color);
    void drawEyeIcon(int16_t x, int16_t y, uint16_t color);
    void drawOverviewSelection(const DeviceState& state);
    void drawParamFrame(OverviewParam param, uint16_t color);

    void enterSelectMode(DeviceState& state);
    void enterEditMode(const DeviceState& state);
    void cancelEdit(DeviceState& state);
    void moveSelection(const DeviceState& state, int8_t direction);
    void changeEditValue(int8_t direction);
    Action applyEdit(DeviceState& state);
    bool isOverviewParamAvailable(const DeviceState& state, OverviewParam param) const;
    OverviewParam firstAvailableOverviewParam(const DeviceState& state) const;
    uint8_t acModeListIndex(uint8_t mode) const;
    uint8_t acModeFromListIndex(uint8_t index) const;
    float vfdStepToHz(uint8_t step) const;
    const char* overviewParamName(OverviewParam param) const;
    const char* interactionLabel() const;

    const char* acModeName(uint8_t mode) const;
    const char* acModeTitle(uint8_t mode) const;
    const char* acFanName(uint8_t fanMode) const;
    const char* acFanTitle(uint8_t fanMode) const;
    const char* vfdRunName(const char* lastAction) const;
    const char* deviceModeName(DeviceMode mode) const;
    const char* activityName(ControllerActivity activity) const;
    const char* getPageName(Page page) const;
    uint16_t statusColor(bool ok) const;
    uint16_t deviceModeColor(DeviceMode mode) const;
    uint16_t activityColor(ControllerActivity activity) const;
    uint8_t vfdStep(const VfdStateSnapshot& vfd) const;
};
