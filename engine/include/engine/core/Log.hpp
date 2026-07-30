#pragma once

#include <string_view>

namespace engine {

enum class LogLevel {
    Info,
    Warn,
    Error,
};

void log(LogLevel level, std::string_view message);

inline void logInfo(std::string_view message) { log(LogLevel::Info, message); }
inline void logWarn(std::string_view message) { log(LogLevel::Warn, message); }
inline void logError(std::string_view message) { log(LogLevel::Error, message); }

} // namespace engine
