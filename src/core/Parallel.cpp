#include "pch.h"
#include "Parallel.h"

#include <bit>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
    constexpr int kMinBandRows = 64;

    struct Pool
    {
        std::mutex mutex;
        std::condition_variable wake;
        std::condition_variable done;
        std::vector<std::thread> workers;
        const std::function<void(int, int)>* job{};
        int begin{};
        int end{};
        int band{};
        unsigned generation{};
        int pending{};
        bool stopping{};
        int startedWith{};
        int requested{ 1 };
    };

    Pool& g_pool = *new Pool;

    void Work(int index, unsigned seen)
    {
        std::unique_lock lock(g_pool.mutex);
        for (;;)
        {
            g_pool.wake.wait(lock, [&] { return g_pool.stopping || g_pool.generation != seen; });
            if (g_pool.stopping)
                return;

            seen = g_pool.generation;
            const auto* job = g_pool.job;
            const int first = g_pool.begin + index * g_pool.band;
            const int last = std::min(g_pool.end, first + g_pool.band);
            lock.unlock();
            if (first < last)
                (*job)(first, last);
            lock.lock();
            if (--g_pool.pending == 0)
                g_pool.done.notify_one();
        }
    }

    int Threads()
    {
        if (g_pool.startedWith != g_pool.requested)
        {
            StopRenderThreads();
            g_pool.startedWith = g_pool.requested;
            DWORD_PTR process = 0, system = 0;
            const int cores = GetProcessAffinityMask(GetCurrentProcess(), &process, &system)
                ? std::popcount(process) : 1;
            const int threads = std::min({ cores, g_pool.requested, kMaxRenderThreads });
            for (int index = 1; index < threads; ++index)
                g_pool.workers.emplace_back(Work, index, g_pool.generation);
        }
        return static_cast<int>(g_pool.workers.size()) + 1;
    }
}

void SetRenderThreads(int threads)
{
    g_pool.requested = threads;
}

void StopRenderThreads()
{
    {
        std::lock_guard lock(g_pool.mutex);
        g_pool.stopping = true;
    }
    g_pool.wake.notify_all();
    for (std::thread& worker : g_pool.workers)
        worker.join();
    g_pool.workers.clear();
    g_pool.stopping = false;
    g_pool.startedWith = 0;
}

void ParallelRows(int begin, int end, int grain, const std::function<void(int first, int last)>& fn)
{
    const int threads = Threads();
    const int rows = end - begin;
    const int share = (rows + threads - 1) / threads;
    const int band = std::max(kMinBandRows, (share + grain - 1) / grain * grain);
    if (threads < 2 || band >= rows)
    {
        fn(begin, end);
        return;
    }

    {
        std::lock_guard lock(g_pool.mutex);
        g_pool.job = &fn;
        g_pool.begin = begin;
        g_pool.end = end;
        g_pool.band = band;
        g_pool.pending = static_cast<int>(g_pool.workers.size());
        ++g_pool.generation;
    }
    g_pool.wake.notify_all();
    fn(begin, begin + band);

    std::unique_lock lock(g_pool.mutex);
    g_pool.done.wait(lock, [] { return g_pool.pending == 0; });
}
