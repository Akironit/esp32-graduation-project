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
constexpr uint16_t COLOR_DANGER = TFT_RED;
constexpr uint16_t COLOR_CARD = 0x0861;
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

    fullRedraw = false;
}

void DisplayUi::drawHeader(const char* title) {
    tft.fillRect(0, 0, 320, 32, COLOR_PANEL);
    tft.setTextColor(COLOR_ACCENT, COLOR_PANEL);
    tft.setTextFont(4);
    tft.setTextSize(1);
    tft.drawString(title, 10, 3);
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
    drawTextBox(slot, x, y, 320 - x, text, color, font);
}

void DisplayUi::drawTextBox(uint8_t slot, int16_t x, int16_t y, int16_t w, const String& text, uint16_t color, uint8_t font) {
    if (slot >= LINE_CACHE_SIZE && !fullRedraw) {
        return;
    }

    if (slot < LINE_CACHE_SIZE && lineCache[slot] == text) {
        return;
    }

    if (slot < LINE_CACHE_SIZE) {
        lineCache[slot] = text;
    }

    const int16_t lineHeight = font >= 4 ? 30 : (font == 1 ? 14 : 22);
    const bool cardLine = y >= 40 && y < 210;
    const uint16_t bgColor = cardLine ? COLOR_CARD : COLOR_BG;

    tft.fillRect(x, y, w, lineHeight, bgColor);
    tft.setTextFont(font);
    tft.setTextSize(1);
    tft.setTextColor(color, bgColor);
    tft.drawString(text, x, y);
}

String DisplayUi::formatFloat(float value, uint8_t digits) const {
    if (value == DEVICE_DISCONNECTED_C) {
        return "DISCONNECTED";
    }

    return String(value, static_cast<unsigned int>(digits));
}

void DisplayUi::drawOverview(const DeviceState& state) {
    if (fullRedraw) {
        drawPanel(10, 42, 145, 50, state.wifiConnected ? COLOR_OK : COLOR_DANGER);
        drawPanel(165, 42, 145, 50, state.homeAssistant.connected ? COLOR_OK : COLOR_DANGER);
        drawPanel(10, 100, 145, 50, state.ac.bound ? COLOR_OK : COLOR_WARN);
        drawPanel(165, 100, 145, 50, COLOR_ACCENT);
        drawPanel(10, 158, 145, 50, COLOR_WARN);
        drawPanel(165, 158, 145, 50, COLOR_MUTED);
    }

    drawStatusDot(266, 16, state.wifiConnected, "WiFi");
    drawStatusDot(302, 16, state.homeAssistant.connected, "HA");

    drawTextBox(0, 20, 50, 125, "NETWORK", COLOR_MUTED, 1);
    drawTextBox(1, 20, 68, 125, state.wifiConnected ? state.ip.toString() : "offline", statusColor(state.wifiConnected), 2);

    drawTextBox(2, 175, 50, 125, "MQTT / HA", COLOR_MUTED, 1);
    drawTextBox(3, 175, 68, 125, state.homeAssistant.connected ? "connected" : "offline", statusColor(state.homeAssistant.connected), 2);

    drawTextBox(4, 20, 108, 125, "AIR CONDITIONER", COLOR_MUTED, 1);
    drawTextBox(5, 20, 126, 125, String(state.ac.powerOn ? "ON " : "OFF ") + acModeName(state.ac.mode), state.ac.powerOn ? COLOR_OK : COLOR_WARN, 2);

    drawTextBox(6, 175, 108, 125, "TEMPERATURES", COLOR_MUTED, 1);
    String tempSummary = String(state.temperatures.sensorCount) + " sensors";
    if (state.temperatures.sensorCount > 0) {
        tempSummary += "  ";
        tempSummary += formatFloat(state.temperatures.values[0], 1);
        tempSummary += " C";
    }
    drawTextBox(7, 175, 126, 125, tempSummary, COLOR_ACCENT, 2);

    drawTextBox(8, 20, 166, 125, "VFD", COLOR_MUTED, 1);
    drawTextBox(
        9,
        20,
        184,
        125,
        String(vfdRunName(state.vfd.lastAction)) + "  " + String(state.vfd.requestedFrequencyHz, 1) + " Hz",
        COLOR_WARN,
        2
    );

    drawTextBox(10, 175, 166, 125, "UPTIME", COLOR_MUTED, 1);
    drawTextBox(11, 175, 184, 125, state.uptimeText, COLOR_TEXT, 2);
}

void DisplayUi::drawTemperatures(const TemperatureStateSnapshot& temperatures) {
    const uint8_t count = temperatures.sensorCount;

    if (fullRedraw) {
        drawPanel(10, 42, 300, 42, COLOR_ACCENT);
        drawPanel(10, 94, 300, 114, COLOR_OK);
    }

    drawTextBox(0, 22, 52, 135, "DS18B20 BUS", COLOR_MUTED, 1);
    drawTextBox(1, 170, 52, 125, String(count) + " sensor(s)", count > 0 ? COLOR_OK : COLOR_WARN, 2);

    if (count == 0) {
        drawTextBox(2, 22, 108, 260, "No DS18B20 sensors found", COLOR_WARN, 2);
        return;
    }

    for (uint8_t i = 0; i < 4; i++) {
        const int y = 106 + i * 24;

        if (i >= count) {
            drawTextBox(2 + i, 22, y, 260, String("Sensor ") + String(i) + ": not present", COLOR_MUTED, 2);
            continue;
        }

        const float value = temperatures.values[i];
        const uint16_t color = value == DEVICE_DISCONNECTED_C ? COLOR_WARN : COLOR_TEXT;
        drawTextBox(2 + i, 22, y, 260, "Sensor " + String(i) + ": " + formatFloat(value, 2) + " C", color, 2);
    }
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

void DisplayUi::drawNetwork(const DeviceState& state) {
    if (fullRedraw) {
        drawPanel(10, 42, 145, 64, state.wifiConnected ? COLOR_OK : COLOR_DANGER);
        drawPanel(165, 42, 145, 64, state.homeAssistant.connected ? COLOR_OK : COLOR_DANGER);
        drawPanel(10, 116, 300, 42, COLOR_ACCENT);
        drawPanel(10, 168, 300, 40, COLOR_MUTED);
    }

    drawStatusDot(266, 16, state.wifiConnected, "WiFi");
    drawStatusDot(302, 16, state.homeAssistant.connected, "HA");

    drawTextBox(0, 20, 52, 125, "WI-FI", COLOR_MUTED, 1);
    drawTextBox(1, 20, 72, 125, state.wifiConnected ? "connected" : "offline", statusColor(state.wifiConnected), 2);

    drawTextBox(2, 175, 52, 125, "HOME ASSISTANT", COLOR_MUTED, 1);
    drawTextBox(3, 175, 72, 125, state.homeAssistant.connected ? "MQTT online" : "MQTT offline", statusColor(state.homeAssistant.connected), 2);

    drawTextBox(4, 20, 126, 275, "IP: " + (state.wifiConnected ? state.ip.toString() : String("none")), COLOR_TEXT, 2);
    drawTextBox(5, 20, 178, 275, "OTA ready  Uptime " + String(state.uptimeText), COLOR_MUTED, 2);
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
