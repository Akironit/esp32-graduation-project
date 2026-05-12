#include "DisplayUi.h"

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
    interactionMode = InteractionMode::View;
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

DisplayUi::Action DisplayUi::handleButton(Button button, bool longPress, DeviceState& state) {
    if (longPress && button == Button::Back) {
        currentPage = Page::Overview;
        interactionMode = InteractionMode::View;
        dirty = true;
        fullRedraw = true;
        Logger::debug(TAG_UI, "Long BACK: return to Overview");
        return {};
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
                } else {
                    Logger::debug(TAG_UI, "OK ignored: page has no editable parameters yet");
                }
                break;
            case Button::Back:
                if (currentPage != Page::Overview) {
                    currentPage = Page::Overview;
                    dirty = true;
                    fullRedraw = true;
                    Logger::debug(TAG_UI, "BACK: return to Overview");
                }
                break;
        }
        return {};
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

void DisplayUi::render(const DeviceState& state) {
    if (fullRedraw) {
        tft.fillScreen(COLOR_BG);
        resetLineCache();
        headerStatusCached = false;
        lastInteractionLabel = nullptr;
        lastFooterText[0] = '\0';
        lastUptimeText[0] = '\0';
        lastWarningCount = 255;
        lastErrorCount = 255;
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
            drawSettings(state);
            break;
        case Page::Diagnostics:
            drawDiagnostics(state);
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

    const char* label = interactionLabel();
    const bool editLabel = interactionMode == InteractionMode::Edit;
    if (fullRedraw || label != lastInteractionLabel || editLabel != lastInteractionEdit) {
        lastInteractionLabel = label;
        lastInteractionEdit = editLabel;
        tft.fillRect(174, 1, 74, 18, COLOR_PANEL);
        if (label[0] != '\0') {
            tft.setFreeFont(&FreeMono9pt7b);
            tft.setTextColor(editLabel ? COLOR_TITLE : COLOR_ACCENT, COLOR_PANEL);
            tft.drawString(label, 176, 4);
        }
    }

    if (fullRedraw || !headerStatusCached || state.wifiConnected != lastHeaderWifiConnected) {
        lastHeaderWifiConnected = state.wifiConnected;
        drawWifiIcon(268, 10, state.wifiConnected ? COLOR_OK : COLOR_DANGER);
    }

    if (fullRedraw || !headerStatusCached || state.homeAssistant.connected != lastHeaderHaConnected) {
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

    if (fullRedraw) {
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
    DeviceMode shownMode = state.controllerState.mode;
    float shownSetTemp = state.environment.targetIndoorTempC;
    bool shownAcPower = state.ac.powerOn;
    uint8_t shownAcMode = state.ac.mode;
    uint8_t shownAcTemp = state.ac.temperature;
    uint8_t shownAcFan = state.ac.fanMode;
    bool shownVfdPower = state.settings.manualVfdPower;
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
    drawFreeTextBox(7, 286, 34, 30, 16, state.environment.exhaustVentEnabled ? "ON " : "OFF", valueFont, state.environment.exhaustVentEnabled ? COLOR_OK : COLOR_MUTED);
    drawFreeTextBox(8, 168, 54, 132, 18, "Temperature", labelFont, COLOR_MUTED);

    const String indoorText = state.environment.hasIndoorTemp ? formatFloat(state.environment.indoorTempC, 1) + "C" : "--.-C";
    const String outdoorText = state.environment.hasOutdoorTemp ? formatFloat(state.environment.outdoorTempC, 1) + "C" : "--.-C";
    const float delta = state.environment.hasIndoorTemp
        ? state.environment.indoorTempC - shownSetTemp
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
    drawFontTextBox(14, 198, 96, 50, 16, String(shownSetTemp, 1) + "C", 2, COLOR_TEXT);
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

void DisplayUi::drawSettings(const DeviceState& state) {
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
            return "Settings";
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
            state.settings.manualAcMode = acModeFromListIndex((uint8_t)editValue);
            Logger::infof(TAG_UI, "AC mode setting: %s", acModeTitle(state.settings.manualAcMode));
            action.settingsChanged = true;
            if (manualMode) {
                action.type = ActionType::AcMode;
                action.uintValue = state.settings.manualAcMode;
            }
            break;
        case OverviewParam::AcTemp:
            state.settings.manualAcTemperature = (uint8_t)editValue;
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
    return vfd.hasActualFrequency ? vfd.actualStep : 0;
}
