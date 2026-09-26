#pragma once

#include "ImportHooks.h"

#include <mutex>
#include <string>
#include <string_view>
#include <utility>

// The map players pick is known only by its file name: nothing inside the
// .smm names it, and its description (mis_desc) is free text that maps copy
// from each other. The menu opens the chosen file last, to unpack it into
// XCHNG\ToGame when the match starts, so the last map it opened is the one
// being played.
namespace MapFile
{
    inline HANDLE(WINAPI* g_originalCreateFileA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES,
                                                 DWORD, DWORD, HANDLE) = nullptr;

    inline std::mutex g_mutex;
    inline std::string g_lastOpened;   // file name without folder and extension, ANSI

    // "Maps\mplay\(RWG3.45) Katon 2x2 1.0.smm" -> "(RWG3.45) Katon 2x2 1.0"
    inline std::string MapName(const char* path)
    {
        if (path == nullptr)
            return {};

        const std::string_view full(path);

        const size_t slash = full.find_last_of("\\/");
        const std::string_view name = slash == std::string_view::npos ? full : full.substr(slash + 1);

        if (name.size() <= 4)
            return {};

        const std::string_view extension = name.substr(name.size() - 4);
        if (_strnicmp(extension.data(), ".smm", 4) != 0 && _strnicmp(extension.data(), ".ssm", 4) != 0)
            return {};

        return std::string(name.substr(0, name.size() - 4));
    }

    inline HANDLE WINAPI CreateFileAHook(LPCSTR path, DWORD access, DWORD share,
                                         LPSECURITY_ATTRIBUTES security, DWORD disposition,
                                         DWORD flags, HANDLE templateFile)
    {
        if (g_originalCreateFileA == nullptr)
            return INVALID_HANDLE_VALUE;

        const HANDLE file = g_originalCreateFileA(path, access, share, security, disposition, flags, templateFile);

        // Only a map that was actually read; the editor saving one does not count.
        if (file != INVALID_HANDLE_VALUE && (access & GENERIC_READ) != 0 && disposition == OPEN_EXISTING
            && !MapName(path).empty())
        {
            // The menu opens maps by an uppercased path; the name on disk keeps
            // the case players see.
            WIN32_FIND_DATAA found{};
            const HANDLE search = FindFirstFileA(path, &found);
            if (search != INVALID_HANDLE_VALUE)
            {
                FindClose(search);

                std::lock_guard lock(g_mutex);
                g_lastOpened = MapName(found.cFileName);
            }
        }

        return file;
    }

    inline void Install(const uintptr_t menuBase)
    {
        // Capture once: a table already patched would chain the hook to itself.
        if (void* const previous = ImportHooks::Redirect(menuBase, "kernel32.dll", "CreateFileA", &CreateFileAHook))
        {
            if (g_originalCreateFileA == nullptr)
                g_originalCreateFileA = reinterpret_cast<decltype(g_originalCreateFileA)>(previous);
        }
    }

    // Empty unless the menu opened a map since the last call, so a match the
    // hook missed reports no map rather than the previous one.
    inline std::string TakeLastOpened()
    {
        std::lock_guard lock(g_mutex);
        return std::exchange(g_lastOpened, {});
    }
}
