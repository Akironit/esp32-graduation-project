#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <TFT_eSPI.h>

#include "FujiHeatPump.h"
#include "TemperatureSensors.h"

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
    void update(
        bool networkConnected,
        const IPAddress& ip,
        FujiHeatPump& heatPump,
        TemperatureSensors& temperatures
    );

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
    String lineCache[10];

    static constexpr unsigned long RENDER_INTERVAL_MS = 1000;
    static constexpr uint8_t LINE_CACHE_SIZE = 10;

    void render(
        bool networkConnected,
        const IPAddress& ip,
        FujiHeatPump& heatPump,
        TemperatureSensors& temperatures
    );

    void drawHeader(const char* title);
    void drawFooter();
    void resetLineCache();
    void drawLine(uint8_t slot, int16_t x, int16_t y, const String& text, uint16_t color = TFT_WHITE, uint8_t font = 2);
    String formatFloat(float value, uint8_t digits) const;
    void drawOverview(bool networkConnected, const IPAddress& ip, FujiHeatPump& heatPump, TemperatureSensors& temperatures);
    void drawTemperatures(TemperatureSensors& temperatures);
    void drawAirConditioner(FujiHeatPump& heatPump);
    void drawNetwork(bool networkConnected, const IPAddress& ip);

    const char* getPageName(Page page) const;
    uint16_t statusColor(bool ok) const;
};
