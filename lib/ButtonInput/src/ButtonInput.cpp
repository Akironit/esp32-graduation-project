#include "ButtonInput.h"

void ButtonInput::begin(bool activeLow, unsigned long debounceMs, unsigned long longPressMs) {
    this->activeLow = activeLow;
    this->debounceMs = debounceMs;
    this->longPressMs = longPressMs;

    rawState = false;
    debouncedState = false;
    longPressFired = false;
    lastRawChangeMs = 0;
    pressedSinceMs = 0;
}

ButtonInput::Event ButtonInput::update(bool rawPressed, unsigned long nowMs) {
    if (rawPressed != rawState) {
        rawState = rawPressed;
        lastRawChangeMs = nowMs;
    }

    if (nowMs - lastRawChangeMs < debounceMs) {
        return Event::None;
    }

    if (rawState != debouncedState) {
        debouncedState = rawState;

        if (debouncedState) {
            pressedSinceMs = nowMs;
            longPressFired = false;
            return Event::None;
        }

        const unsigned long pressDurationMs = nowMs - pressedSinceMs;
        return pressDurationMs >= longPressMs || longPressFired ? Event::None : Event::ShortPress;
    }

    if (debouncedState && !longPressFired && nowMs - pressedSinceMs >= longPressMs) {
        longPressFired = true;
        return Event::LongPress;
    }

    return Event::None;
}

bool ButtonInput::isPressed() const {
    return debouncedState;
}

bool ButtonInput::isActive() const {
    return rawState || debouncedState;
}
