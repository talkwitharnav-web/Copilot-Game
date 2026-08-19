#include "engine/core/JobSystem.hpp"

#include "engine/core/Log.hpp"

#include <cstdio>
#include <exception>
#include <string>

namespace engine {
namespace {

/// Runs one job and swallows anything it throws.
///
/// **A job that throws must not take the process with it**, and there are two
/// callers because there are two ways a job runs. On a worker thread an escaping
/// exception unwinds straight out of the thread's entry point, which calls
/// `std::terminate`: the game vanishes mid-frame with no log line, no window
/// message and nothing to debug from. Inline on the main thread it is no better
/// - it unwinds out of `submit`, past every caller that had no reason to expect
/// one, and out to the session-wide catch that ends the run.
///
/// The jobs are chunk generation and meshing, which allocate heavily, so
/// `std::bad_alloc` at a large render distance is the realistic trigger rather
/// than a theoretical one - and one lost chunk is a far better outcome than a
/// lost session, whichever thread it was going to happen on.
///
/// One function rather than two `try` blocks **because the two paths drifting
/// apart is exactly what happened**: the inline path had no catch at all for as
/// long as it existed, while the worker path had one and a comment explaining
/// why it mattered.
/// **Nothing in either handler may allocate**, and that is a correctness
/// requirement rather than a tidiness one. `std::bad_alloc` is the realistic
/// trigger named above, so a handler that allocates fails precisely when it is
/// needed: a second `bad_alloc` thrown from inside the `catch` escapes this
/// function, and on a worker thread it unwinds out of the thread entry point to
/// `std::terminate` - the exact outcome the guard exists to prevent, with the
/// promise two paragraphs up going unkept for its own stated cause. Hence the
/// stack buffer: `std::snprintf` formats into it without touching the
/// allocator, truncating rather than growing, and `logError` takes a
/// `std::string_view` so passing it on allocates nothing either. **Do not
/// "simplify" this back to `std::string(...) + error.what()`** - that reads
/// better and is the bug.
void runGuarded(const std::function<void()>& job) {
    try {
        job();
    } catch (const std::exception& error) {
        char message[256];
        std::snprintf(message, sizeof(message), "A job threw and was abandoned: %s", error.what());
        logError(message);
    } catch (...) {
        logError("A job threw something that is not a std::exception and was abandoned");
    }
}

} // namespace

unsigned defaultWorkerThreadCount() {
    const unsigned hardware = std::thread::hardware_concurrency();
    return hardware > 1 ? hardware / 2 : 1; // Zero means "unknown", not "none".
}

JobSystem::JobSystem(unsigned threadCount) {
    m_workers.reserve(threadCount);
    for (unsigned i = 0; i < threadCount; ++i) {
        m_workers.emplace_back([this] { workerLoop(); });
    }

    if (threadCount == 0) {
        logInfo("Job system: no worker threads, running everything on the main thread");
    } else {
        logInfo("Job system: " + std::to_string(threadCount) + " worker threads");
    }
}

JobSystem::~JobSystem() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = true;
        // Queued-but-unstarted work is abandoned; its results would be thrown
        // away during shutdown anyway. Running jobs are always allowed to finish.
        m_outstanding -= m_queue.size();
        m_queue.clear();
    }
    m_wake.notify_all();
    m_idle.notify_all();

    for (std::thread& worker : m_workers) {
        worker.join();
    }
}

void JobSystem::submit(std::function<void()> job) {
    if (m_workers.empty()) {
        // Zero workers: the job runs here and now, under the same guard a worker
        // thread would give it. See `runGuarded`.
        runGuarded(job);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push_back(std::move(job));
        ++m_outstanding;
    }
    m_wake.notify_one();
}

void JobSystem::waitForIdle() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_idle.wait(lock, [this] { return m_outstanding == 0; });
}

std::size_t JobSystem::outstandingJobs() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_outstanding;
}

void JobSystem::workerLoop() {
    for (;;) {
        std::function<void()> job;

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [this] { return m_stopping || !m_queue.empty(); });

            if (m_queue.empty()) {
                return; // Only reachable when stopping.
            }

            job = std::move(m_queue.front());
            m_queue.pop_front();
        }

        runGuarded(job);

        // **Outside the guard, always.** `waitForIdle` blocks until this reaches
        // zero, so a decrement skipped by a throwing job would hang startup for
        // ever - a second failure, with a completely different symptom, caused
        // by the first.
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            --m_outstanding;
        }
        m_idle.notify_all();
    }
}

} // namespace engine
