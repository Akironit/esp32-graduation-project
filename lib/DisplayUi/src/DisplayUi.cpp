#include "DisplayUi.h"

#include "Logger.h"

namespace {
constexpr const char* TAG_DISPLAY = "DISPLAY";

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
    }

    drawHeader(state, getPageName());
    drawFooter(state);

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
        case Page::FontTest1:
            drawFontTest1();
            break;
        case Page::FontTest2:
            drawFontTest2();
            break;
        case Page::FontTest3:
            drawFontTest3();
            break;
        case Page::Count:
            break;
    }

    fullRedraw = false;
}

void DisplayUi::drawHeader(const DeviceState& state, const char* title) {
    if (fullRedraw) {
        tft.fillRect(0, 0, 320, HEADER_H, COLOR_PANEL);
        String headerTitle(title);
        headerTitle.toUpperCase();
        tft.setFreeFont(&FreeMonoBold9pt7b);
        tft.setTextColor(COLOR_TITLE, COLOR_PANEL);
        tft.drawString(headerTitle, 8, 2);
        tft.drawFastHLine(0, HEADER_H, 320, COLOR_TITLE);
    }

    drawWifiIcon(268, 10, state.wifiConnected ? COLOR_OK : COLOR_DANGER);
    drawHomeAssistantIcon(304, 10, state.homeAssistant.connected ? COLOR_OK : COLOR_DANGER);
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

    if (fullRedraw) {
        tft.fillRect(0, FOOTER_Y, 320, FOOTER_H, COLOR_PANEL);
        tft.setFreeFont(nullptr);
        tft.setTextFont(1);
        tft.setTextSize(1);
        tft.setTextColor(COLOR_MUTED, COLOR_PANEL);
        tft.drawString(footer, 8, FOOTER_Y + 8);
    }

    drawWarningIcon(52, FOOTER_Y + 12, state.controllerState.warningCount);
    drawErrorIcon(98, FOOTER_Y + 12, state.controllerState.errorCount);

    String uptime = String("Up ") + state.uptimeText;
    uptime.toUpperCase();
    tft.fillRect(224, FOOTER_Y + 6, 88, 12, COLOR_PANEL);
    tft.setFreeFont(nullptr);
    tft.setTextFont(1);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_MUTED, COLOR_PANEL);
    tft.drawString(uptime, 226, FOOTER_Y + 8);
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

    if (slot < LINE_CACHE_SIZE && lineCache[slot] == text) {
        return;
    }

    if (slot < LINE_CACHE_SIZE) {
        lineCache[slot] = text;
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

    if (slot < LINE_CACHE_SIZE && lineCache[slot] == text) {
        return;
    }

    if (slot < LINE_CACHE_SIZE) {
        lineCache[slot] = text;
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

    if (slot < LINE_CACHE_SIZE && lineCache[slot] == text) {
        return;
    }

    if (slot < LINE_CACHE_SIZE) {
        lineCache[slot] = text;
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

    drawFreeTextBox(0, 14, 36, 58, 18, "MODE:", labelFont, COLOR_MUTED);
    drawTextBox(1, 78, 32, 72, deviceModeName(state.controllerState.mode), deviceModeColor(state.controllerState.mode), 4);
    drawFreeTextBox(2, 14, 70, 70, 18, "State:", labelFont, COLOR_MUTED);
    drawFreeTextBox(3, 14, 88, 104, 18, activityName(state.controllerState.activity), valueFont, activityColor(state.controllerState.activity));
    drawActivityIcon(136, 96, state.controllerState.activity);

    drawFontTextBox(4, 168, 32, 44, 16, "Hood", 2, COLOR_ACCENT);
    drawFreeTextBox(5, 214, 34, 20, 16, String(state.environment.kitchenHoodLevel), valueFont, COLOR_TEXT);
    drawFontTextBox(6, 250, 32, 34, 16, "EXH", 2, COLOR_ACCENT);
    drawFreeTextBox(7, 286, 34, 30, 16, state.environment.exhaustVentEnabled ? "ON " : "OFF", valueFont, state.environment.exhaustVentEnabled ? COLOR_OK : COLOR_MUTED);
    drawFreeTextBox(8, 168, 54, 132, 18, "Temperature", labelFont, COLOR_MUTED);

    const String indoorText = state.environment.hasIndoorTemp ? formatFloat(state.environment.indoorTempC, 1) + "C" : "--.-C";
    const String outdoorText = state.environment.hasOutdoorTemp ? formatFloat(state.environment.outdoorTempC, 1) + "C" : "--.-C";
    const float delta = state.environment.hasIndoorTemp
        ? state.environment.indoorTempC - state.environment.targetIndoorTempC
        : 0.0f;
    const float deltaAbs = delta < 0.0f ? -delta : delta;
    const String deltaText = state.environment.hasIndoorTemp
        ? String(delta >= 0.0f ? "+" : "") + String(delta, 1) + "C"
        : String("--.-C");

    drawFontTextBox(9, 168, 76, 22, 16, "In", 2, COLOR_ACCENT);
    drawFontTextBox(10, 190, 76, 52, 16, indoorText, 2, state.environment.hasIndoorTemp ? COLOR_TEXT : COLOR_WARN);
    drawFontTextBox(11, 244, 76, 28, 16, "Out", 2, COLOR_ACCENT);
    drawFontTextBox(12, 274, 76, 42, 16, outdoorText, 2, state.environment.hasOutdoorTemp ? COLOR_TEXT : COLOR_WARN);
    drawFontTextBox(13, 168, 96, 28, 16, "Set", 2, COLOR_ACCENT);
    drawFontTextBox(14, 198, 96, 50, 16, String(state.environment.targetIndoorTempC, 1) + "C", 2, COLOR_TEXT);
    drawFontTextBox(15, 250, 96, 32, 16, "Dlt", 2, COLOR_ACCENT);
    drawFontTextBox(16, 282, 96, 34, 16, deltaText, 2, deltaAbs <= state.environment.targetToleranceC ? COLOR_OK : COLOR_WARN);

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
    drawFreeTextBox(20, 68, 150, 36, 18, state.ac.powerOn ? "ON " : "OFF", valueFont, state.ac.powerOn ? COLOR_OK : COLOR_MUTED);
    drawFontTextBox(21, 14, 170, 42, 16, "Mode", 2, COLOR_MUTED);
    drawFreeTextBox(22, 62, 170, 82, 18, acModeTitle(state.ac.mode), valueFont, state.ac.powerOn ? activityColor(ControllerActivity::AcCool) : COLOR_MUTED);
    drawFontTextBox(23, 14, 194, 24, 16, "Set", 2, COLOR_MUTED);
    drawFreeTextBox(24, 42, 192, 36, 18, String(state.ac.temperature) + "C", valueFont, COLOR_TEXT);
    drawFontTextBox(25, 82, 194, 24, 16, "Fan", 2, COLOR_MUTED);
    drawFreeTextBox(26, 110, 192, 44, 18, acFanTitle(state.ac.fanMode), valueFont, COLOR_TEXT);

    const uint8_t step = vfdStep(state.vfd);
    const bool vfdRunning = step > 0 && strcmp(state.vfd.lastAction, "stop") != 0;
    const char* vfdLink = state.vfd.initialized ? "Linked" : "Wait";
    const uint16_t vfdLinkColor = state.vfd.initialized ? COLOR_OK : COLOR_WARN;

    drawFreeTextBox(27, 168, 128, 46, 18, "VFD:", labelFont, COLOR_ACCENT);
    drawFreeTextBox(28, 218, 128, 90, 18, vfdLink, valueFont, vfdLinkColor);
    drawFontTextBox(29, 168, 150, 50, 16, "Power", 2, COLOR_MUTED);
    drawFreeTextBox(30, 224, 150, 40, 18, vfdRunning ? "ON " : "OFF", valueFont, vfdRunning ? COLOR_OK : COLOR_MUTED);
    drawFontTextBox(31, 168, 170, 40, 16, "Step", 2, COLOR_MUTED);
    drawFreeTextBox(32, 212, 170, 38, 18, String(step) + "/6", valueFont, step > 0 ? COLOR_OK : COLOR_MUTED);
    drawFontTextBox(33, 168, 194, 38, 16, "Freq", 2, COLOR_MUTED);
    drawFreeTextBox(34, 212, 192, 72, 18, state.vfd.hasRequestedFrequency ? String(state.vfd.requestedFrequencyHz, 0) + " Hz" : "-- Hz", valueFont, state.vfd.hasRequestedFrequency ? COLOR_TEXT : COLOR_MUTED);
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

    drawTextBox(0, 20, 52, 125, "WI-FI", COLOR_MUTED, 1);
    drawTextBox(1, 20, 72, 125, state.wifiConnected ? "connected" : "offline", statusColor(state.wifiConnected), 2);

    drawTextBox(2, 175, 52, 125, "HOME ASSISTANT", COLOR_MUTED, 1);
    drawTextBox(3, 175, 72, 125, state.homeAssistant.connected ? "MQTT online" : "MQTT offline", statusColor(state.homeAssistant.connected), 2);

    drawTextBox(4, 20, 126, 275, "IP: " + (state.wifiConnected ? state.ip.toString() : String("none")), COLOR_TEXT, 2);
    drawTextBox(5, 20, 178, 275, "OTA ready  Uptime " + String(state.uptimeText), COLOR_MUTED, 2);
}

void DisplayUi::drawFontTest1() {
    if (!fullRedraw) {
        return;
    }

    if (fullRedraw) {
        tft.drawFastHLine(8, 48, 304, COLOR_LINE);
        tft.drawFastHLine(8, 98, 304, COLOR_LINE);
        tft.drawFastHLine(8, 154, 304, COLOR_LINE);
    }

    drawTextBox(0, 10, 28, 300, "F1 regular: ABC abc 123 +-*/", COLOR_TEXT, 1);
    drawLabel(1, 10, 44, 140, "F1 bold simulated");

    drawTextBox(2, 10, 62, 300, "F2 regular: ABC abc 123 +-*/", COLOR_TEXT, 2);
    drawBoldText(10, 84, "F2 bold simulated", COLOR_TITLE, COLOR_BG, 2);

    drawTextBox(3, 10, 112, 300, "F4 regular: ABC 123", COLOR_TEXT, 4);
    drawBoldText(10, 138, "F4 bold", COLOR_OK, COLOR_BG, 4);

    drawTextBox(4, 10, 184, 300, "Native italic/underline/strike: no", COLOR_WARN, 2);
}

void DisplayUi::drawFontTest2() {
    if (!fullRedraw) {
        return;
    }

    if (fullRedraw) {
        tft.drawFastHLine(8, 70, 304, COLOR_LINE);
        tft.drawFastHLine(8, 150, 304, COLOR_LINE);
    }

    drawTextBox(0, 10, 30, 300, "F6: 123", COLOR_TEXT, 6);
    drawTextBox(1, 130, 30, 180, "big digits", COLOR_MUTED, 2);

    drawTextBox(2, 10, 86, 300, "F7: 12:34", COLOR_ACCENT, 7);
    drawTextBox(3, 10, 166, 300, "F8: 123", COLOR_TITLE, 8);
}

void DisplayUi::drawFontTest3() {
    if (!fullRedraw) {
        return;
    }

    tft.drawFastHLine(8, 58, 304, COLOR_LINE);
    tft.drawFastHLine(8, 112, 304, COLOR_LINE);
    tft.drawFastHLine(8, 166, 304, COLOR_LINE);

    tft.setTextColor(COLOR_ACCENT, COLOR_BG);
    tft.setFreeFont(&FreeSans9pt7b);
    tft.drawString("FF17 Sans9 regular", 10, 30);
    tft.setFreeFont(&FreeSansBold9pt7b);
    tft.drawString("FF21 Sans9 bold", 170, 30);

    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.setFreeFont(&FreeSansOblique9pt7b);
    tft.drawString("FF25 Sans9 oblique", 10, 82);
    tft.setFreeFont(&FreeSansBoldOblique9pt7b);
    tft.drawString("FF29 bold oblique", 170, 82);

    tft.setTextColor(COLOR_TITLE, COLOR_BG);
    tft.setFreeFont(&FreeMono9pt7b);
    tft.drawString("FF1 Mono9", 10, 136);
    tft.setFreeFont(&FreeMonoBold9pt7b);
    tft.drawString("FF5 Mono9 bold", 170, 136);

    tft.setTextColor(COLOR_OK, COLOR_BG);
    tft.setFreeFont(&FreeSerif9pt7b);
    tft.drawString("FF33 Serif9", 10, 190);
    tft.setFreeFont(&FreeSerifBold9pt7b);
    tft.drawString("FF41 Serif9 bold", 170, 190);

    tft.setFreeFont(nullptr);
    tft.setTextFont(2);
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
        case Page::FontTest1:
            return "Font 1";
        case Page::FontTest2:
            return "Font 2";
        case Page::FontTest3:
            return "Font 3";
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
            return "Hold";
        case ControllerActivity::Idle:
            return "Idle";
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
            return COLOR_WARN;
        case ControllerActivity::Idle:
            return COLOR_WARN;
    }

    return COLOR_TEXT;
}

uint8_t DisplayUi::vfdStep(const VfdStateSnapshot& vfd) const {
    if (!vfd.hasRequestedFrequency || strcmp(vfd.lastAction, "stop") == 0 || strcmp(vfd.lastAction, "none") == 0) {
        return 0;
    }

    if (vfd.requestedFrequencyHz <= 20.0f) {
        return 1;
    }

    if (vfd.requestedFrequencyHz >= 50.0f) {
        return 6;
    }

    return 1 + (uint8_t)((vfd.requestedFrequencyHz - 20.0f) / 6.0f);
}
