#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <TFT_eSPI.h>

#include "DeviceState.h"

class DisplayUi {
public:
    enum class Page : uint8_t {
        Overview = 0,
        Temperatures,
        AirConditioner,
        Network,
        FontTest1,
        FontTest2,
        FontTest3,
        Count
    };

    void begin();
    void update(const DeviceState& state);

    void nextPage();
    void previousPage();
    void setPage(Page page);

    bool isReady() const;
    uint8_t getPageIndex() const;
    const char* getPageName() const;

private:
    TFT_eSPI tft;
    Page currentPage = Page::Overview;
    bool ready = false;
    bool dirty = true;
    bool fullRedraw = true;
    unsigned long lastRenderMs = 0;
    static constexpr unsigned long RENDER_INTERVAL_MS = 1000;
    static constexpr uint8_t LINE_CACHE_SIZE = 40;
    String lineCache[LINE_CACHE_SIZE];

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
    void drawNetwork(const DeviceState& state);
    void drawFontTest1();
    void drawFontTest2();
    void drawFontTest3();
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
