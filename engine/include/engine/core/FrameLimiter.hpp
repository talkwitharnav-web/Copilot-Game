#pragma once

#include <chrono>

namespace engine {

/// Paces the main loop to a target frame rate.
///
/// Without this the loop runs as fast as the GPU allows, which burns power and
/// pins a CPU core for frames nobody sees.
class FrameLimiter {
public:
    /// A target of 0 (or less) disables limiting entirely.
    explicit FrameLimiter(double targetFps);
    ~FrameLimiter();

    FrameLimiter(const FrameLimiter&) = delete;
    FrameLimiter& operator=(const FrameLimiter&) = delete;
    FrameLimiter(FrameLimiter&&) = delete;
    FrameLimiter& operator=(FrameLimiter&&) = delete;

    void setTargetFps(double targetFps);
    double targetFps() const { return m_targetFps; }

    /// Blocks until the next frame is due. Call once per frame, at the very end.
    void waitForNextFrame();

private:
    using Clock = std::chrono::steady_clock;

    double m_targetFps = 0.0;
    Clock::duration m_frameInterval{};
    Clock::time_point m_nextFrameTime{};

    /// Win32 HANDLE, kept as void* so this header stays platform-free.
    void* m_timer = nullptr;
};

} // namespace engine
