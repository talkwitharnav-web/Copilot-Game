#include "engine/core/FrameLimiter.hpp"

#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace engine {
namespace {

// Windows' ordinary sleep only resolves to about 15.6 ms, so a naive 8.3 ms
// wait for 120 fps would overshoot and settle near 64 fps instead. Wake up
// slightly early and burn the remainder in a spin, which is accurate to well
// under a millisecond at the cost of a few hundred microseconds of CPU.
constexpr auto kSpinMargin = std::chrono::microseconds(500);

void sleepUntil(void* timer, std::chrono::steady_clock::time_point deadline) {
    const auto remaining = deadline - std::chrono::steady_clock::now();

    if (remaining > kSpinMargin) {
        const auto coarse = remaining - kSpinMargin;
        bool waited = false;

#if defined(_WIN32)
        if (timer != nullptr) {
            LARGE_INTEGER due{};
            // Negative values mean relative time, expressed in 100 ns units.
            due.QuadPart = -(std::chrono::duration_cast<std::chrono::nanoseconds>(coarse).count() / 100);
            if (::SetWaitableTimerEx(static_cast<HANDLE>(timer), &due, 0, nullptr, nullptr, nullptr, 0) != 0) {
                ::WaitForSingleObject(static_cast<HANDLE>(timer), INFINITE);
                waited = true;
            }
        }
#else
        (void)timer;
#endif

        if (!waited) {
            std::this_thread::sleep_for(coarse);
        }
    }

    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
}

} // namespace

FrameLimiter::FrameLimiter(double targetFps) {
#if defined(_WIN32)
    // Sub-millisecond waits without timeBeginPeriod's process-wide side
    // effects. Requires Windows 10 1803+; a null handle falls back to sleeping.
    m_timer = ::CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
#endif
    setTargetFps(targetFps);
}

FrameLimiter::~FrameLimiter() {
#if defined(_WIN32)
    if (m_timer != nullptr) {
        ::CloseHandle(static_cast<HANDLE>(m_timer));
    }
#endif
}

void FrameLimiter::setTargetFps(double targetFps) {
    m_targetFps = targetFps > 0.0 ? targetFps : 0.0;
    m_frameInterval = m_targetFps > 0.0 ? std::chrono::duration_cast<Clock::duration>(
                                              std::chrono::duration<double>(1.0 / m_targetFps))
                                        : Clock::duration::zero();
    m_nextFrameTime = Clock::now() + m_frameInterval;
}

void FrameLimiter::waitForNextFrame() {
    if (m_frameInterval == Clock::duration::zero()) {
        return;
    }

    const auto now = Clock::now();

    // A long stall (debugger breakpoint, dragging the window) must not queue up
    // a burst of catch-up frames, so the schedule restarts instead of accruing
    // debt it would then try to repay all at once.
    if (now > m_nextFrameTime + m_frameInterval) {
        m_nextFrameTime = now + m_frameInterval;
        return;
    }

    sleepUntil(m_timer, m_nextFrameTime);
    m_nextFrameTime += m_frameInterval;
}

} // namespace engine
