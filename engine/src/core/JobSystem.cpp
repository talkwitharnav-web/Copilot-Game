#include "engine/core/JobSystem.hpp"

#include "engine/core/Log.hpp"

#include <string>

namespace engine {

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
        job();
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

        job();

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            --m_outstanding;
        }
        m_idle.notify_all();
    }
}

} // namespace engine
