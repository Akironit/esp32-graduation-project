#pragma once

#include <Arduino.h>

enum class LogLevel : uint8_t {
    Error = 0,
    Warning,
    Info,
    Debug,
    Trace
};

class Logger {
public:
    static void begin(Print& output);
    static void setLevel(LogLevel level);

    static void error(const char* tag, const char* message);
    static void warning(const char* tag, const char* message);
    static void info(const char* tag, const char* message);
    static void debug(const char* tag, const char* message);
    static void trace(const char* tag, const char* message);

    static void raw(const char* message);
    static void rawf(const char* format, ...);

    static void errorf(const char* tag, const char* format, ...);
    static void warningf(const char* tag, const char* format, ...);
    static void infof(const char* tag, const char* format, ...);
    static void debugf(const char* tag, const char* format, ...);
    static void tracef(const char* tag, const char* format, ...);

private:
    static Print* output;
    static LogLevel currentLevel;

    static bool shouldLog(LogLevel level);
    static const char* levelName(LogLevel level);
    static void writeTimestamp();
    static void write(LogLevel level, const char* tag, const char* message);
    static void writef(LogLevel level, const char* tag, const char* format, va_list args);
};
