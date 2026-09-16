#pragma once

#include <cstddef>
#include <cstdint>

#include <windows.h>

namespace MemoryProbe
{
    // One cached region per pointer kind a walk follows, so a unit, a vtable
    // and a game function never evict each other's region.
    struct Region
    {
        uintptr_t start{ 0 };
        uintptr_t end{ 0 };
    };

    inline constexpr DWORD kReadable =
        PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
        PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    inline constexpr DWORD kWritable =
        PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;

    // Same shape as GameDllHooks::is_valid_ptr, which is private to that class.
    inline bool IsReadable(const void* p, size_t bytes,
                           uintptr_t* regionStart = nullptr, uintptr_t* regionEnd = nullptr,
                           DWORD protect = kReadable)
    {
        if (!p || bytes == 0)
            return false;

        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(p, &mbi, sizeof(mbi)))
            return false;
        if (mbi.State != MEM_COMMIT)
            return false;
        if (!(mbi.Protect & protect))
            return false;

        // The range must not run off the end of the queried region.
        const auto start = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const auto end = start + mbi.RegionSize;
        const auto first = reinterpret_cast<uintptr_t>(p);
        if (first < start || first + bytes > end)
            return false;

        if (regionStart && regionEnd)
        {
            *regionStart = start;
            *regionEnd = end;
        }
        return true;
    }

    inline bool IsReadableCached(Region& region, const void* p, size_t bytes)
    {
        const auto first = reinterpret_cast<uintptr_t>(p);
        if (region.end && first >= region.start && first + bytes <= region.end)
            return true;

        return IsReadable(p, bytes, &region.start, &region.end);
    }
}
