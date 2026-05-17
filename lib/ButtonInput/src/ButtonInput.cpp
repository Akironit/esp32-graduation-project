#include "ButtonInput.h"

void ButtonInput::begin(bool activeLow, unsigned long debounceMs, unsigned long longPressMs, unsigned long repeatMs) {
    this->activeLow = activeLow;
    this->debounceMs = debounceMs;
    this->longPressMs = longPressMs;
    this->repeatMs = repeatMs;

    rawState = false;
    debouncedState = false;
    longPressFired = false;
    lastRawChangeMs = 0;
    pressedSinceMs = 0;
    lastLongPressMs = 0;
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
            lastLongPressMs = 0;
            return Event::None;
        }

        const unsigned long pressDurationMs = nowMs - pressedSinceMs;
        return pressDurationMs >= longPressMs || longPressFired ? Event::None : Event::ShortPress;
    }

    if (debouncedState && nowMs - pressedSinceMs >= longPressMs) {
        if (!longPressFired || nowMs - lastLongPressMs >= repeatMs) {
            longPressFired = true;
            lastLongPressMs = nowMs;
            return Event::LongPress;
        }
    }

    return Event::None;
}

bool ButtonInput::isPressed() const {
    return debouncedState;
}

bool ButtonInput::isActive() const {
    return rawState || debouncedState;
}
