#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <TFT_eSPI.h>

#include "AutoControlSettings.h"
#include "DeviceState.h"

class DisplayUi {
public:
    enum class Page : uint8_t {
        Overview = 0,
        AirConditioner,
        Ventilation,
        Temperatures,
        Settings,
        SystemSettings,
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
        VfdSetFrequency,
        AutoSettings,
        TempAssignRole,
        TempForget,
        TempForceRead,
        TempScan,
        TempSwap,
        SystemSettings,
        SystemSaveNow,
        SystemReboot
    };

    struct Action {
        ActionType type = ActionType::None;
        bool settingsChanged = false;
        bool boolValue = false;
        uint8_t uintValue = 0;
        float floatValue = 0.0f;
        AutoControlSettings autoSettings;
        TempSensorRole tempRole = TempSensorRole::Unknown;
    };

    void begin();
    void update(const DeviceState& state, const AutoControlSettings& autoSettings);

    void nextPage();
    void previousPage();
    void setPage(Page page);

    bool isReady() const;
    uint8_t getPageIndex() const;
    const char* getPageName() const;
    Action handleButton(Button button, bool longPress, DeviceState& state, const AutoControlSettings& autoSettings);

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
    uint8_t selectedAutoSetting = 0;
    uint8_t autoSettingsScroll = 0;
    float autoEditValue = 0.0f;
    enum class SystemPageMode : uint8_t {
        View,
        Select,
        Edit,
        ConfirmReboot
    };
    SystemPageMode systemPageMode = SystemPageMode::View;
    uint8_t selectedSystemSetting = 0;
    uint8_t systemSettingsScroll = 0;
    bool systemEditValue = false;
    bool systemRebootYes = false;
    enum class TempPageMode : uint8_t {
        View,
        Config,
        Menu,
        EditRole,
        ConfirmForget
    };
    TempPageMode tempPageMode = TempPageMode::View;
    uint8_t selectedTempSensor = 0;
    uint8_t tempSensorScroll = 0;
    uint8_t selectedTempMenu = 0;
    uint8_t selectedTempRole = 0;
    bool ready = false;
    bool dirty = true;
    bool fullRedraw = true;
    bool shellRedraw = true;
    bool lastHeaderWifiConnected = false;
    bool lastHeaderHaConnected = false;
    bool lastHeaderWifiEnabled = false;
    bool lastHeaderHaEnabled = false;
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
    static constexpr uint8_t AUTO_VISIBLE_ROWS = 8;
    static constexpr uint8_t SYSTEM_VISIBLE_ROWS = 6;
    static constexpr uint8_t TEMP_VISIBLE_ROWS = 6;
    static constexpr uint8_t DIAG_VISIBLE_ROWS = 12;
    String lineCache[LINE_CACHE_SIZE];
    uint16_t lineColorCache[LINE_CACHE_SIZE] = {};
    bool autoPageCacheValid = false;
    uint8_t lastAutoVisibleStart = 255;
    uint8_t lastAutoSelectedIndex = 255;
    bool lastAutoEditMode = false;
    char lastAutoGroup[16] = "";
    char lastAutoHint[56] = "";
    uint8_t lastAutoRowIndex[AUTO_VISIBLE_ROWS] = {};
    bool lastAutoRowSelected[AUTO_VISIBLE_ROWS] = {};
    bool lastAutoRowEdit[AUTO_VISIBLE_ROWS] = {};
    String lastAutoRowValue[AUTO_VISIBLE_ROWS];
    bool systemPageCacheValid = false;
    uint8_t lastSystemVisibleStart = 255;
    uint8_t lastSystemSelectedIndex = 255;
    SystemPageMode lastSystemPageMode = SystemPageMode::View;
    uint8_t lastSystemRowIndex[SYSTEM_VISIBLE_ROWS] = {};
    bool lastSystemRowSelected[SYSTEM_VISIBLE_ROWS] = {};
    String lastSystemRowValue[SYSTEM_VISIBLE_ROWS];
    bool tempPageCacheValid = false;
    TempPageMode lastTempPageMode = TempPageMode::View;
    uint8_t lastTempVisibleStart = 255;
    uint8_t lastTempSelectedIndex = 255;
    uint8_t lastTempMenuIndex = 255;
    uint8_t lastTempRoleIndex = 255;
    uint8_t lastTempCount = 255;
    uint8_t lastTempRowIndex[TEMP_VISIBLE_ROWS] = {};
    bool lastTempRowSelected[TEMP_VISIBLE_ROWS] = {};
    String lastTempRowText[TEMP_VISIBLE_ROWS];
    String lastTempInfoText;
    String lastTempHintText;
    bool diagnosticsScrollMode = false;
    uint8_t diagnosticsScrollOffset = 0;
    bool diagnosticsPageCacheValid = false;
    uint8_t lastDiagnosticsScrollOffset = 255;
    bool lastDiagnosticsScrollMode = false;
    uint8_t lastDiagnosticsLineCount = 255;

    void render(const DeviceState& state, const AutoControlSettings& autoSettings);

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
    void drawSettings(const DeviceState& state, const AutoControlSettings& autoSettings);
    void drawSystemSettings(const DeviceState& state);
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
    void drawAutoSettingsList(const AutoControlSettings& autoSettings);
    void drawTemperatureList(const TemperatureStateSnapshot& temperatures);

    void enterSelectMode(DeviceState& state);
    void enterEditMode(const DeviceState& state);
    void cancelEdit(DeviceState& state);
    void moveSelection(const DeviceState& state, int8_t direction);
    void changeEditValue(int8_t direction);
    Action applyEdit(DeviceState& state);
    Action handleAutoSettingsButton(Button button, bool longPress, const AutoControlSettings& autoSettings);
    Action handleSystemSettingsButton(Button button, bool longPress, DeviceState& state);
    Action handleTemperatureButton(Button button, bool longPress, const TemperatureStateSnapshot& temperatures);
    Action handleDiagnosticsButton(Button button, bool longPress, const DeviceState& state);
    void enterAutoSettingsSelect();
    void enterAutoSettingsEdit(const AutoControlSettings& autoSettings);
    void moveAutoSettingsSelection(int8_t direction);
    void changeAutoSettingsValue(int8_t direction, bool fast);
    Action applyAutoSettingsEdit(const AutoControlSettings& autoSettings);
    Action applySystemSettingsEdit(DeviceState& state);
    Action applyTemperatureMenuAction(const TemperatureStateSnapshot& temperatures);
    Action applyTemperatureRole(const TemperatureStateSnapshot& temperatures);
    const char* tempRoleShort(TempSensorRole role) const;
    const char* tempRoleTitle(TempSensorRole role) const;
    String tempShortAddress(const DeviceAddress& address) const;
    bool isOverviewParamAvailable(const DeviceState& state, OverviewParam param) const;
    OverviewParam firstAvailableOverviewParam(const DeviceState& state) const;
    uint8_t acModeListIndex(uint8_t mode) const;
    uint8_t acModeFromListIndex(uint8_t index) const;
    float vfdStepToHz(uint8_t step) const;
    uint8_t diagnosticsLineCount(const DeviceState& state) const;
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
