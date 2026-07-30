#include "engine/core/Log.hpp"

#include <chrono>
#include <iostream>

namespace engine {
namespace {

const auto g_startTime = std::chrono::steady_clock::now();

double secondsSinceStart() {
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - g_startTime;
    return elapsed.count();
}

const char* levelTag(LogLevel level) {
    switch (level) {
        case LogLevel::Info: return "INFO ";
        case LogLevel::Warn: return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

} // namespace

void log(LogLevel level, std::string_view message) {
    std::ostream& out = (level == LogLevel::Error) ? std::cerr : std::cout;

    const std::streamsize savedPrecision = out.precision();
    out.setf(std::ios::fixed, std::ios::floatfield);
    out.precision(3);

    out << '[' << secondsSinceStart() << "] [" << levelTag(level) << "] " << message << '\n';

    out.precision(savedPrecision);
    out.unsetf(std::ios::floatfield);
}

} // namespace engine
