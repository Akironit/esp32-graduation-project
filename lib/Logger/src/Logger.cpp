#include "Logger.h"

#include <stdarg.h>
#include <stdio.h>

Print* Logger::output = &Serial;
LogLevel Logger::currentLevel = LogLevel::Info;
char Logger::history[Logger::HISTORY_CAPACITY][Logger::HISTORY_LINE_LENGTH] = {};
uint8_t Logger::historyHead = 0;
uint8_t Logger::historyCount = 0;

void Logger::begin(Print& output) {
    Logger::output = &output;
}

void Logger::setLevel(LogLevel level) {
    currentLevel = level;
}

void Logger::error(const char* tag, const char* message) {
    write(LogLevel::Error, tag, message);
}

void Logger::warning(const char* tag, const char* message) {
    write(LogLevel::Warning, tag, message);
}

void Logger::info(const char* tag, const char* message) {
    write(LogLevel::Info, tag, message);
}

void Logger::debug(const char* tag, const char* message) {
    write(LogLevel::Debug, tag, message);
}

void Logger::trace(const char* tag, const char* message) {
    write(LogLevel::Trace, tag, message);
}

void Logger::raw(const char* message) {
    if (output == nullptr) {
        return;
    }

    output->print(message);
}

void Logger::rawf(const char* format, ...) {
    if (output == nullptr) {
        return;
    }

    char message[160];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    output->print(message);
}

void Logger::replay(Print& target) {
    const uint8_t firstIndex = (historyHead + HISTORY_CAPACITY - historyCount) % HISTORY_CAPACITY;

    for (uint8_t i = 0; i < historyCount; i++) {
        const uint8_t index = (firstIndex + i) % HISTORY_CAPACITY;
        target.println(history[index]);
    }
}

void Logger::errorf(const char* tag, const char* format, ...) {
    va_list args;
    va_start(args, format);
    writef(LogLevel::Error, tag, format, args);
    va_end(args);
}

void Logger::warningf(const char* tag, const char* format, ...) {
    va_list args;
    va_start(args, format);
    writef(LogLevel::Warning, tag, format, args);
    va_end(args);
}

void Logger::infof(const char* tag, const char* format, ...) {
    va_list args;
    va_start(args, format);
    writef(LogLevel::Info, tag, format, args);
    va_end(args);
}

void Logger::debugf(const char* tag, const char* format, ...) {
    va_list args;
    va_start(args, format);
    writef(LogLevel::Debug, tag, format, args);
    va_end(args);
}

void Logger::tracef(const char* tag, const char* format, ...) {
    va_list args;
    va_start(args, format);
    writef(LogLevel::Trace, tag, format, args);
    va_end(args);
}

bool Logger::shouldLog(LogLevel level) {
    return static_cast<uint8_t>(level) <= static_cast<uint8_t>(currentLevel);
}

const char* Logger::levelName(LogLevel level) {
    switch (level) {
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Warning:
            return "WARN";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Trace:
            return "TRACE";
        default:
            return "LOG";
    }
}

void Logger::writeTimestamp() {
    const unsigned long totalMs = millis();
    const unsigned long totalSeconds = totalMs / 1000;
    const unsigned int hours = (totalSeconds / 3600) % 100;
    const unsigned int minutes = (totalSeconds / 60) % 60;
    const unsigned int seconds = totalSeconds % 60;
    const unsigned int milliseconds = totalMs % 1000;

    output->print("[");

    if (hours < 10) {
        output->print("0");
    }
    output->print(hours);
    output->print(":");

    if (minutes < 10) {
        output->print("0");
    }
    output->print(minutes);
    output->print(":");

    if (seconds < 10) {
        output->print("0");
    }
    output->print(seconds);
    output->print(".");

    if (milliseconds < 100) {
        output->print("0");
    }
    if (milliseconds < 10) {
        output->print("0");
    }
    output->print(milliseconds);
    output->print("]");
}

void Logger::write(LogLevel level, const char* tag, const char* message) {
    if (output == nullptr || !shouldLog(level)) {
        return;
    }

    const unsigned long totalMs = millis();
    const unsigned long totalSeconds = totalMs / 1000;
    const unsigned int hours = (totalSeconds / 3600) % 100;
    const unsigned int minutes = (totalSeconds / 60) % 60;
    const unsigned int seconds = totalSeconds % 60;
    const unsigned int milliseconds = totalMs % 1000;

    char line[HISTORY_LINE_LENGTH];
    snprintf(
        line,
        sizeof(line),
        "[%02u:%02u:%02u.%03u][%s][%s]: %s",
        hours,
        minutes,
        seconds,
        milliseconds,
        levelName(level),
        tag,
        message
    );

    output->println(line);
    storeHistory(line);
}

void Logger::writef(LogLevel level, const char* tag, const char* format, va_list args) {
    char message[160];
    vsnprintf(message, sizeof(message), format, args);
    write(level, tag, message);
}

void Logger::storeHistory(const char* line) {
    strlcpy(history[historyHead], line, HISTORY_LINE_LENGTH);
    historyHead = (historyHead + 1) % HISTORY_CAPACITY;

    if (historyCount < HISTORY_CAPACITY) {
        historyCount++;
    }
}
