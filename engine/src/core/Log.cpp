#include "engine/core/Log.hpp"

#include <chrono>
#include <iostream>
#include <mutex>

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
    // **Held for the whole body, and one lock for both streams.**
    //
    // The standard's guarantee about `std::cout` covers concurrent *insertions*
    // only - it says nothing about concurrently modifying a stream's **format
    // state**, which is exactly what the `setf`/`precision` pair below does. So
    // without this the three lines that follow are formally undefined
    // behaviour, and observably produce timestamps at the wrong precision, in
    // scientific notation, or two messages spliced through each other.
    //
    // This is the engine's only shared sink and every thread reaches it: the
    // main thread, and every job worker by way of whatever the game loads on
    // one. A worker that rejects a file logs once per file, so a directory of
    // bad ones is a warning storm from every worker at once. One mutex covers
    // both streams because a warning and an error interleaved is the same
    // unreadable line.
    static std::mutex sink;
    const std::lock_guard<std::mutex> lock(sink);

    std::ostream& out = (level == LogLevel::Error) ? std::cerr : std::cout;

    // **Both halves of the pair, or neither.** The precision was saved and
    // restored; the float format was set and then cleared to the *default*
    // instead of to whatever was there, in the next line down. `unsetf` on the
    // float field selects `defaultfloat`, which is only the right answer if the
    // caller happened to be in the default state - so the first log line after
    // anything set `std::scientific` or `std::hexfloat` silently undid it.
    // Latent today, because nothing else in the engine touches these flags, and
    // exactly the state this function exists to protect.
    const std::ios::fmtflags savedFlags = out.flags();
    const std::streamsize savedPrecision = out.precision();
    out.setf(std::ios::fixed, std::ios::floatfield);
    out.precision(3);

    out << '[' << secondsSinceStart() << "] [" << levelTag(level) << "] " << message << '\n';

    out.precision(savedPrecision);
    out.flags(savedFlags);
}

} // namespace engine
