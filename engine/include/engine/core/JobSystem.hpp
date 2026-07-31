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
    /// what a player asking for minimum resource use gets, and it makes
    /// single-threaded debugging possible without a second code path.
    explicit JobSystem(unsigned threadCount);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    void submit(std::function<void()> job);

    /// Blocks until every submitted job has finished. Startup uses this; a
    /// running frame never should.
    void waitForIdle();

    unsigned threadCount() const { return static_cast<unsigned>(m_workers.size()); }

    /// Submitted but not yet finished, including jobs currently running.
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
