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
    static constexpr uint8_t LINE_CACHE_SIZE = 18;
    String lineCache[LINE_CACHE_SIZE];

    void render(const DeviceState& state);

    void drawHeader(const char* title);
    void drawFooter();
    void resetLineCache();
    void drawLine(uint8_t slot, int16_t x, int16_t y, const String& text, uint16_t color = TFT_WHITE, uint8_t font = 2);
    void drawTextBox(uint8_t slot, int16_t x, int16_t y, int16_t w, const String& text, uint16_t color = TFT_WHITE, uint8_t font = 2);
    String formatFloat(float value, uint8_t digits) const;
    void drawOverview(const DeviceState& state);
    void drawTemperatures(const TemperatureStateSnapshot& temperatures);
    void drawAirConditioner(const AcStateSnapshot& ac);
    void drawNetwork(const DeviceState& state);
    void drawPanel(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void drawStatusDot(int16_t x, int16_t y, bool ok, const char* label);

    const char* acModeName(uint8_t mode) const;
    const char* acFanName(uint8_t fanMode) const;
    const char* vfdRunName(const char* lastAction) const;
    const char* getPageName(Page page) const;
    uint16_t statusColor(bool ok) const;
};
