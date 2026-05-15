#pragma once

#include <Arduino.h>

namespace AppConfig {
constexpr unsigned long BUTTON_POLL_INTERVAL_MS = 10;

constexpr unsigned long VFD_STATUS_POLL_INTERVAL_MS = 1000;
constexpr unsigned long VFD_COMMAND_SYNC_INTERVAL_MS = 500;
constexpr unsigned long VFD_POLL_AFTER_COMMAND_GUARD_MS = 250;
constexpr uint8_t VFD_COMMAND_SYNC_MAX_ATTEMPTS = 10;

constexpr unsigned long USER_SETTINGS_SAVE_DELAY_MS = 2000;
constexpr const char* USER_SETTINGS_NAMESPACE = "climate";

constexpr unsigned long AUTO_SETTINGS_SAVE_DELAY_MS = 7000;
constexpr unsigned long AUTO_COMMAND_RETRY_INTERVAL_MS = 5000;
constexpr unsigned long AUTO_SAFE_RETRY_INTERVAL_MS = 10000;
}
