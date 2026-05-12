#pragma once

#include <Arduino.h>

class ButtonInput {
public:
    enum class Event : uint8_t {
        None,
        ShortPress,
        LongPress
    };

    void begin(bool activeLow = true, unsigned long debounceMs = 50, unsigned long longPressMs = 500);
    Event update(bool rawPressed, unsigned long nowMs);
    bool isPressed() const;
    bool isActive() const;

private:
    bool activeLow = true;
    bool rawState = false;
    bool debouncedState = false;
    bool longPressFired = false;

    unsigned long debounceMs = 50;
    unsigned long longPressMs = 500;
    unsigned long lastRawChangeMs = 0;
    unsigned long pressedSinceMs = 0;
};
