#include "DisplayUi.h"

#include "Logger.h"

namespace {
constexpr const char* TAG_DISPLAY = "DISPLAY";

constexpr uint16_t COLOR_BG = TFT_BLACK;
constexpr uint16_t COLOR_PANEL = 0x1082;
constexpr uint16_t COLOR_TEXT = TFT_WHITE;
constexpr uint16_t COLOR_MUTED = 0x8410;
constexpr uint16_t COLOR_ACCENT = TFT_CYAN;
constexpr uint16_t COLOR_OK = TFT_GREEN;
constexpr uint16_t COLOR_WARN = TFT_ORANGE;
}

void DisplayUi::begin() {
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(COLOR_BG);
    tft.setTextDatum(TL_DATUM);

    ready = true;
    dirty = true;
    fullRedraw = true;

    Logger::info(TAG_DISPLAY, "ST7789 display initialized");
}

void DisplayUi::update(const DeviceState& state) {
    if (!ready) {
        return;
    }

    const unsigned long now = millis();

    if (!dirty && now - lastRenderMs < RENDER_INTERVAL_MS) {
        return;
    }

    lastRenderMs = now;
    dirty = false;

    render(state);
}

void DisplayUi::nextPage() {
    const uint8_t next = (getPageIndex() + 1) % static_cast<uint8_t>(Page::Count);
    currentPage = static_cast<Page>(next);
    dirty = true;
    fullRedraw = true;
}

void DisplayUi::previousPage() {
    const uint8_t count = static_cast<uint8_t>(Page::Count);
    const uint8_t previous = (getPageIndex() + count - 1) % count;
    currentPage = static_cast<Page>(previous);
    dirty = true;
    fullRedraw = true;
}

void DisplayUi::setPage(Page page) {
    if (page >= Page::Count) {
        return;
    }

    currentPage = page;
    dirty = true;
    fullRedraw = true;
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

void DisplayUi::render(const DeviceState& state) {
    if (fullRedraw) {
        tft.fillScreen(COLOR_BG);
        resetLineCache();
        drawHeader(getPageName());
        drawFooter();
        fullRedraw = false;
    }

    switch (currentPage) {
        case Page::Overview:
            drawOverview(state);
            break;
        case Page::Temperatures:
            drawTemperatures(state.temperatures);
            break;
        case Page::AirConditioner:
            drawAirConditioner(state.ac);
            break;
        case Page::Network:
            drawNetwork(state);
            break;
        case Page::Count:
            break;
    }
}

void DisplayUi::drawHeader(const char* title) {
    tft.fillRect(0, 0, 320, 30, COLOR_PANEL);
    tft.setTextColor(COLOR_ACCENT, COLOR_PANEL);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.drawString(title, 10, 7);
}

void DisplayUi::drawFooter() {
    char footer[32];
    snprintf(
        footer,
        sizeof(footer),
        "%u/%u  %s",
        getPageIndex() + 1,
        static_cast<uint8_t>(Page::Count),
        getPageName()
    );

    tft.fillRect(0, 218, 320, 22, COLOR_PANEL);
    tft.setTextColor(COLOR_MUTED, COLOR_PANEL);
    tft.setTextFont(1);
    tft.setTextSize(1);
    tft.drawString(footer, 10, 225);
}

void DisplayUi::resetLineCache() {
    for (uint8_t i = 0; i < LINE_CACHE_SIZE; i++) {
        lineCache[i] = "";
    }
}

void DisplayUi::drawLine(uint8_t slot, int16_t x, int16_t y, const String& text, uint16_t color, uint8_t font) {
    if (slot >= LINE_CACHE_SIZE && !fullRedraw) {
        return;
    }

    if (slot < LINE_CACHE_SIZE && lineCache[slot] == text) {
        return;
    }

    if (slot < LINE_CACHE_SIZE) {
        lineCache[slot] = text;
    }

    tft.fillRect(x, y, 320 - x, 22, COLOR_BG);
    tft.setTextFont(font);
    tft.setTextSize(1);
    tft.setTextColor(color, COLOR_BG);
    tft.drawString(text, x, y);
}

String DisplayUi::formatFloat(float value, uint8_t digits) const {
    if (value == DEVICE_DISCONNECTED_C) {
        return "DISCONNECTED";
    }

    return String(value, static_cast<unsigned int>(digits));
}

void DisplayUi::drawOverview(const DeviceState& state) {
    drawLine(0, 14, 46, state.wifiConnected ? "Wi-Fi: connected" : "Wi-Fi: offline", statusColor(state.wifiConnected));
    drawLine(1, 14, 70, state.wifiConnected ? state.ip.toString() : "IP: none");
    drawLine(2, 14, 102, state.ac.bound ? "AC: bound" : "AC: waiting", statusColor(state.ac.bound));
    drawLine(3, 14, 126, state.ac.powerOn ? "Power: ON" : "Power: OFF");
    drawLine(4, 14, 150, "Set: " + String(state.ac.temperature) + " C  Mode: " + String(state.ac.mode));
    drawLine(5, 14, 182, "DS18B20 sensors: " + String(state.temperatures.sensorCount));
}

void DisplayUi::drawTemperatures(const TemperatureStateSnapshot& temperatures) {
    const uint8_t count = temperatures.sensorCount;

    if (count == 0) {
        drawLine(0, 14, 54, "No DS18B20 sensors found");
        return;
    }

    for (uint8_t i = 0; i < count && i < 6; i++) {
        const float value = temperatures.values[i];
        const int y = 48 + i * 26;
        drawLine(i, 14, y, "Sensor " + String(i) + ": " + formatFloat(value, 2) + " C");
    }
}

void DisplayUi::drawAirConditioner(const AcStateSnapshot& ac) {
    drawLine(0, 14, 48, ac.bound ? "Link: bound" : "Link: not bound", statusColor(ac.bound));
    drawLine(1, 14, 76, String("Power: ") + (ac.powerOn ? "ON" : "OFF"));
    drawLine(2, 14, 102, "Temp: " + String(ac.temperature) + " C");
    drawLine(3, 14, 128, "Mode: " + String(ac.mode) + "  Fan: " + String(ac.fanMode));
    drawLine(4, 14, 154, String("Role: ") + (ac.primaryController ? "PRIMARY" : "SECONDARY"));

    if (ac.hasReceivedFrame) {
        drawLine(5, 14, 180, "Last frame: " + String(ac.lastFrameAgeMs) + " ms");
    } else {
        drawLine(5, 14, 180, "Last frame: never");
    }
}

void DisplayUi::drawNetwork(const DeviceState& state) {
    drawLine(0, 14, 52, state.wifiConnected ? "Wi-Fi connected" : "Wi-Fi disconnected", statusColor(state.wifiConnected));
    drawLine(1, 14, 88, "IP address:");
    drawLine(2, 14, 114, state.wifiConnected ? state.ip.toString() : "none");
    drawLine(3, 14, 154, "OTA: enabled when Wi-Fi is up");
}

const char* DisplayUi::getPageName(Page page) const {
    switch (page) {
        case Page::Overview:
            return "Overview";
        case Page::Temperatures:
            return "Temp";
        case Page::AirConditioner:
            return "AC";
        case Page::Network:
            return "Network";
        case Page::Count:
            return "?";
    }

    return "?";
}

uint16_t DisplayUi::statusColor(bool ok) const {
    return ok ? COLOR_OK : COLOR_WARN;
}
