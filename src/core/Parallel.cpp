#include "pch.h"
#include "Parallel.h"

#include <tlhelp32.h>

#include <atomic>
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
    std::atomic<DWORD_PTR> g_gameMask{};

    void PinThreads(DWORD_PTR mask)
    {
        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return;

        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry))
        {
            if (entry.th32OwnerProcessID != GetCurrentProcessId())
                continue;
            if (const HANDLE thread = OpenThread(THREAD_SET_INFORMATION | THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID))
            {
                SetThreadAffinityMask(thread, mask);
                CloseHandle(thread);
            }
        }
        CloseHandle(snapshot);
    }

    void Work(int index, unsigned seen, DWORD_PTR mask)
    {
        if (mask)
            SetThreadAffinityMask(GetCurrentThread(), mask);

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
            DWORD_PTR process = 1, system = 1;
            GetProcessAffinityMask(GetCurrentProcess(), &process, &system);
            const int threads = std::min({ std::popcount(system), g_pool.requested, kMaxRenderThreads });
            DWORD_PTR workerMask = 0;
            if (threads > 1 && process != system)
            {
                g_gameMask = process;
                SetProcessAffinityMask(GetCurrentProcess(), system);
                PinThreads(process);
                workerMask = system & ~process ? system & ~process : system;
            }
            for (int index = 1; index < threads; ++index)
                g_pool.workers.emplace_back(Work, index, g_pool.generation, workerMask);
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
    if (const DWORD_PTR mask = g_gameMask.exchange(0))
        SetProcessAffinityMask(GetCurrentProcess(), mask);
}

void PinNewThread()
{
    if (const DWORD_PTR mask = g_gameMask.load())
        SetThreadAffinityMask(GetCurrentThread(), mask);
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
