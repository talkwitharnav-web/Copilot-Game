#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace engine {

/// Half of this machine's hardware threads, at least one.
///
/// Half rather than all: the game should be a good citizen on a machine that is
/// also doing other things, and the main thread still has a frame to draw.
unsigned defaultWorkerThreadCount();

/// A pool of worker threads and a queue of work for them.
///
/// Deliberately knows nothing about what a job *is*. It takes a callable and
/// runs it somewhere else; deciding what may safely run in parallel is the
/// caller's problem, because only the caller knows what its data is doing.
///
/// A job must be self-contained: it should own everything it touches, or touch
/// only things guaranteed to outlive the pool. Nothing here makes shared state
/// safe.
class JobSystem {
public:
    /// The pool is **fixed for its lifetime**. Changing the worker count means
    /// building a new one, which in practice means restarting the game: growing
    /// or shrinking a pool while jobs are in flight is a whole class of bugs
    /// bought for no benefit.
    ///
    /// `threadCount` of zero creates no threads at all and runs every job inline
    /// on the calling thread. That is a supported mode, not a failure: it is
    /// what a player asking for minimum resource use gets, and it puts every job
    /// on one thread for debugging.
    ///
    /// **It is a second code path, and the only promise made about it is that it
    /// keeps the same contract**: `submit` runs the job through the same guard a
    /// worker thread would, so a job that throws is logged and abandoned either
    /// way rather than ending the session. An earlier version of this comment
    /// claimed there was no second path at all, and while that was written the
    /// inline one was the single place where a `std::bad_alloc` out of meshing
    /// could kill the game.
    explicit JobSystem(unsigned threadCount);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    void submit(std::function<void()> job);

    /// Blocks until every submitted job has finished.
    ///
    /// **No caller as of 2026-08-19, and the previous sentence here - "Startup
    /// uses this" - was true until minutes before that date.** Its only two
    /// callers were both inside `World::loadImmediately`, twice per pass across
    /// sixteen passes, so restoring that function would restore 32 blocking
    /// barriers on the main thread. It was deleted; this is left behind.
    ///
    /// **Shutdown does not need it and must not be given it.** `~JobSystem` is
    /// self-sufficient: it sets `m_stopping` under the lock, abandons queued
    /// work, notifies both condition variables and joins every worker. The GPU
    /// side is `vkDeviceWaitIdle` in the renderer and unrelated to this class.
    /// So there is currently no honest use to cite, and inventing one would be
    /// worse than the empty API.
    ///
    /// Kept rather than deleted because deleting it is not the four-line change
    /// it looks like: this is the **only waiter** on `m_idle`, so removing it
    /// leaves a condition variable with two `notify_all` sites and nobody
    /// listening, and tidying that means editing `workerLoop`'s notify path -
    /// the one piece of threading in the engine, audited clean for races and
    /// lost wakeups. Not a trade worth making for four dead lines.
    ///
    /// Falsified by: this gaining a caller, or `m_idle` gaining a second waiter.
    void waitForIdle();

    unsigned threadCount() const { return static_cast<unsigned>(m_workers.size()); }

    /// Submitted but not yet finished, including jobs currently running.
    ///
    /// **No caller as of 2026-08-19, and this one is a gap rather than surplus
    /// API.** It is the number a debug overlay wants during a stutter
    /// investigation: a frame that hitches while this sits high is waiting on
    /// generation or meshing, and one that hitches while it sits at zero is not.
    /// Nothing else in the engine can distinguish those two. Kept unwired rather
    /// than deleted because the diagnostic is worth having the day it is needed.
    ///
    /// **The caller belongs in `game/src/hud/DebugOverlay.cpp`**, which already
    /// displays mesh count, retired count, GPU milliseconds, draw calls,
    /// triangles and VRAM - queue depth is the one number it lacks, and it is
    /// the one that separates "waiting on a worker" from "waiting on something
    /// else". Re-verified 2026-08-19 by a bare-name search over `engine/` and
    /// `game/src`: two hits, this declaration and the definition. Falsified by
    /// a third.
    std::size_t outstandingJobs() const;

private:
    void workerLoop();

    std::vector<std::thread> m_workers;
    std::deque<std::function<void()>> m_queue;

    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::condition_variable m_idle;

    std::size_t m_outstanding = 0;
    bool m_stopping = false;
};

} // namespace engine
