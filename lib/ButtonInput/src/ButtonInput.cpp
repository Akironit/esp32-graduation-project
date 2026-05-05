#include "ButtonInput.h"

void ButtonInput::begin(bool activeLow, unsigned long debounceMs, unsigned long longPressMs) {
    this->activeLow = activeLow;
    this->debounceMs = debounceMs;
    this->longPressMs = longPressMs;

    rawState = false;
    debouncedState = false;
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
            return Event::None;
        }

        const unsigned long pressDurationMs = nowMs - pressedSinceMs;
        return pressDurationMs >= longPressMs ? Event::LongPress : Event::ShortPress;
    }

    return Event::None;
}

bool ButtonInput::isPressed() const {
    return debouncedState;
}

bool ButtonInput::isActive() const {
    return rawState || debouncedState;
}
